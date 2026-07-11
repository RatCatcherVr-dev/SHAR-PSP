"""Offline geometry-decimation pass for the PSP port.

Levels are the RAM wall for geometry (~2-2.6 MB resident per .p3d at 2x), and
~98% of level triangles live in triangle STRIPS. This pass, per static (non-
skinned) PRIMGROUP:
  1. reads positions + normals/uv(s)/colours + indices,
  2. builds a triangle-list face array (converting strips -> lists),
  3. decimates with fast_simplification (quadric edge-collapse), replaying the
     collapses to get an old->new vertex mapping,
  4. carries per-vertex attributes across the mapping (averaged; normals
     renormalized; colours averaged in BGRA),
  5. rewrites the lists and PRIMGROUP header as an indexed TRIANGLE LIST
     (primType=0 — the GU backend renders it via indexed sceGuDrawArray),
  6. recomputes the MESH BOX/SPHERE bounds from the new positions.

Skinned prim groups, non-triangle prims, and tiny meshes are left untouched.
Every output is re-parsed before being trusted.
"""
import argparse, os, struct
import numpy as np
import fast_simplification as fs
from p3d import load, dump
import geo


def _carry(attr, mapping, nnew, weight=None):
    """Average per-vertex `attr` (old order) into `nnew` new slots via mapping."""
    dim = attr.shape[1] if attr.ndim > 1 else 1
    acc = np.zeros((nnew, dim), np.float64)
    cnt = np.zeros(nnew, np.float64)
    a = attr.reshape(len(attr), dim).astype(np.float64)
    np.add.at(acc, mapping, a)
    np.add.at(cnt, mapping, 1.0)
    cnt[cnt == 0] = 1.0
    return acc / cnt[:, None]


def _faces_from_primgroup(h, idx):
    if h["primType"] == geo.PRIM_TRIANGLES:
        n = (len(idx) // 3) * 3
        return idx[:n].astype(np.int64).reshape(-1, 3)
    if h["primType"] == geo.PRIM_TRISTRIP:
        return geo.strip_to_list(idx)
    return None


def decimate_primgroup(pg, reduction, min_tris, stats):
    h = geo.parse_primgroup_header(pg.payload)
    skinned = geo.is_skinned(pg)
    if h["primType"] not in (geo.PRIM_TRIANGLES, geo.PRIM_TRISTRIP):
        stats["skip_prim"] += 1; return None

    pos_c = pg.find(geo.POSITIONLIST)
    idx_c = pg.find(geo.INDEXLIST)
    if not pos_c or not idx_c:
        stats["skip_nodata"] += 1; return None
    pos = geo.read_vec3_list(pos_c[0])
    idx = geo.read_u32_list(idx_c[0])
    faces = _faces_from_primgroup(h, idx)
    if faces is None or len(faces) < min_tris:
        stats["skip_small"] += 1
        return pos if len(pos) else None  # still counts toward bbox

    try:
        _, _, collapses = fs.simplify(pos.astype(np.float32), faces,
                                      target_reduction=reduction, return_collapses=True)
        npos, nfaces, mapping = fs.replay_simplification(pos.astype(np.float32),
                                                         faces, collapses)
    except Exception as e:
        stats["errors"] += 1
        print(f"    ! primgroup '{h['shader'].decode('latin-1','replace')}': {e}")
        return pos if len(pos) else None
    npos = np.asarray(npos, np.float32); nfaces = np.asarray(nfaces, np.int64)
    mapping = np.asarray(mapping, np.int64); nnew = len(npos)
    if nnew == 0 or len(nfaces) == 0 or nnew >= len(pos):
        stats["skip_nogain"] += 1
        return pos if len(pos) else None

    stats["tris_in"] += len(faces); stats["tris_out"] += len(nfaces)
    stats["verts_in"] += len(pos); stats["verts_out"] += nnew
    stats["decimated"] += 1

    # --- rewrite child lists ---
    pos_c[0].payload = geo.write_vec3_list(npos)
    for nc in pg.find(geo.NORMALLIST):
        nrm = _carry(geo.read_vec3_list(nc), mapping, nnew)
        ln = np.linalg.norm(nrm, axis=1, keepdims=True); ln[ln == 0] = 1.0
        nc.payload = geo.write_vec3_list((nrm / ln).astype(np.float32))
    for uc in pg.find(geo.UVLIST):
        ch, uv = geo.read_uv_list(uc)
        uc.payload = geo.write_uv_list(ch, _carry(uv, mapping, nnew).astype(np.float32))
    for cc in pg.find(geo.COLOURLIST):
        col = _carry(geo.read_colour_list(cc), mapping, nnew)
        cc.payload = geo.write_colour_list(np.clip(col + 0.5, 0, 255).astype("u1"))

    # Skinned prim groups: the WEIGHT/MATRIXIDX lists are per-vertex and must
    # match the new vertex count. Bone indices can't be averaged, so each new
    # vertex inherits the weight+matrix-index of a representative old vertex (the
    # lowest old index collapsing into it). The MATRIXPALETTE is unchanged (the
    # kept indices still reference the same joints). This is exact while CPU
    # skinning is disabled (bind-pose VBO); with skinning on it's a minor weight
    # approximation on collapsed vertices.
    if skinned:
        rep = np.full(nnew, len(pos), np.int64)
        np.minimum.at(rep, mapping, np.arange(len(pos), dtype=np.int64))
        rep = np.clip(rep, 0, len(pos) - 1)
        for wc in pg.find(geo.WEIGHTLIST):
            w = geo.read_vec3_list(wc)
            wc.payload = geo.write_vec3_list(w[rep])
        for mc in pg.find(geo.MATRIXLIST):
            m = geo.read_u32_list(mc)
            mc.payload = geo.write_u32_list(m[rep])
        stats["skinned_decimated"] += 1

    new_idx = nfaces.reshape(-1).astype(np.uint32)
    idx_c[0].payload = geo.write_u32_list(new_idx)
    h["primType"] = geo.PRIM_TRIANGLES
    h["nvert"] = nnew
    h["nindex"] = len(new_idx)
    pg.payload = geo.build_primgroup_header(h)
    return npos


def _recompute_bounds(mesh, all_pos):
    if not all_pos:
        return
    p = np.concatenate(all_pos, 0)
    lo = p.min(0); hi = p.max(0)
    for b in mesh.find(geo.BOX):
        b.payload = struct.pack("<6f", *lo, *hi)
    for s in mesh.find(geo.SPHERE):
        c = (lo + hi) * 0.5
        rad = float(np.sqrt(((p - c) ** 2).sum(1).max()))
        s.payload = struct.pack("<4f", *c, rad)


def decimate_root(root, reduction, min_tris):
    """Mutate an already-loaded chunk tree in place; return geometry stats."""
    stats = dict(decimated=0, skinned_decimated=0, skip_prim=0, skip_nodata=0,
                 skip_small=0, skip_nogain=0, errors=0,
                 tris_in=0, tris_out=0, verts_in=0, verts_out=0)
    for mesh in [c for c in root.walk() if c.id in (geo.MESH, geo.SKIN)]:
        all_pos = []
        for pg in mesh.find(geo.PRIMGROUP):
            np_pos = decimate_primgroup(pg, reduction, min_tris, stats)
            if np_pos is not None and len(np_pos):
                all_pos.append(np.asarray(np_pos, np.float32))
        _recompute_bounds(mesh, all_pos)
    return stats


def decimate_file(src, dst, reduction, min_tris):
    root = load(src)
    stats = decimate_root(root, reduction, min_tris)
    data = dump(root)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    load(dst)  # re-parse; raises if the rewrite corrupted the tree
    return stats, os.path.getsize(src), len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--reduction", type=float, default=0.5,
                    help="fraction of triangles to REMOVE (0.5 = halve)")
    ap.add_argument("--min-tris", type=int, default=64,
                    help="leave prim groups with fewer triangles untouched")
    a = ap.parse_args()
    s, osz, nsz = decimate_file(a.src, a.dst, a.reduction, a.min_tris)
    resid_in = s["verts_in"] * 72
    resid_out = s["verts_out"] * 72 + (s["verts_in"] - s["verts_out"]) * 0  # informational
    print(f"{a.src} -> {a.dst}")
    print(f"  file: {osz/1e6:.2f} -> {nsz/1e6:.2f} MB")
    print(f"  primgroups decimated={s['decimated']} (skinned={s['skinned_decimated']}) "
          f"nonTri={s['skip_prim']} small={s['skip_small']} noGain={s['skip_nogain']} "
          f"err={s['errors']}")
    print(f"  triangles: {s['tris_in']} -> {s['tris_out']} "
          f"({100*(1-s['tris_out']/max(s['tris_in'],1)):.0f}% fewer)")
    print(f"  vertices:  {s['verts_in']} -> {s['verts_out']} "
          f"({100*(1-s['verts_out']/max(s['verts_in'],1)):.0f}% fewer)")
    print(f"  est resident geom (verts*72, decimated groups only): "
          f"{s['verts_in']*72/1e6:.2f} -> {s['verts_out']*72/1e6:.2f} MB")


if __name__ == "__main__":
    main()
