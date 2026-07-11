"""Offline prim-group MERGE pass for the PSP port.

The sceGU backend pays a real per-prim-group (per-draw) cost every frame, and
PC-format models are split into many small prim groups that only differ by which
part of the mesh they are — a character is ~12 prim groups across 2 shaders, a
level room ~280. Fewer draws = higher fps everywhere.

This pass, per MESH (rigid tGeometry — scenery + rigid props), groups the
non-skinned PRIMGROUPs by (shader, vertex format, attribute set) and concatenates
each group into ONE indexed TRIANGLE-LIST prim group:
  * positions / normals / uv(per channel) / colours are concatenated,
  * indices are converted strip->list, offset by the running vertex base, and
    concatenated,
  * a merged group is split whenever it would exceed 65535 vertices (the engine
    narrows indices to 16-bit on load).

Skinned prim groups (weights/matrix palette) and SKIN containers are left
untouched in v1 — merging those needs matrix-palette unification. Every output
is re-parsed before being trusted (batch.py does the reload).
"""
import argparse, os, struct
import numpy as np
from p3d import load, dump, Chunk, Reader, pack_pstr
import geo

VERT_CAP = 65535   # engine uses 16-bit indices; keep a merged group under this


def _uv_channels(pg):
    return tuple(sorted(geo.read_uv_list(c)[0] for c in pg.find(geo.UVLIST)))


def _faces(h, idx):
    if h["primType"] == geo.PRIM_TRIANGLES:
        n = (len(idx) // 3) * 3
        return idx[:n].astype(np.int64).reshape(-1, 3)
    if h["primType"] == geo.PRIM_TRISTRIP:
        return geo.strip_to_list(idx)
    return None


def _mergeable(pg):
    """A rigid, triangle-based prim group we can concatenate."""
    if geo.is_skinned(pg):
        return None
    h = geo.parse_primgroup_header(pg.payload)
    if h["primType"] not in (geo.PRIM_TRIANGLES, geo.PRIM_TRISTRIP):
        return None
    if not pg.find(geo.POSITIONLIST) or not pg.find(geo.INDEXLIST):
        return None
    return h


def _merge_key(pg, h):
    # Only groups with identical shader + format + attribute set can be one draw.
    return (bytes(h["shader"]), h["vfmt"], h["version"],
            bool(pg.find(geo.NORMALLIST)), bool(pg.find(geo.COLOURLIST)),
            _uv_channels(pg))


def _build_merged(items, key):
    """items: list of (pg, header) sharing a merge key. Yields merged Chunk(s),
    each kept under VERT_CAP vertices."""
    shader, vfmt, version, has_n, has_c, uvchans = key
    out = []

    def flush(pos, nrm, uvs, col, faces, base_used):
        h = dict(version=version, shader=shader, primType=geo.PRIM_TRIANGLES,
                 vfmt=vfmt, nvert=len(pos), nindex=len(faces) * 3, nmatrix=0)
        pg = Chunk(geo.PRIMGROUP, geo.build_primgroup_header(h))
        pg.children.append(Chunk(geo.POSITIONLIST, geo.write_vec3_list(np.concatenate(pos))))
        if has_n:
            pg.children.append(Chunk(geo.NORMALLIST, geo.write_vec3_list(np.concatenate(nrm))))
        for ch in uvchans:
            pg.children.append(Chunk(geo.UVLIST, geo.write_uv_list(ch, np.concatenate(uvs[ch]))))
        if has_c:
            pg.children.append(Chunk(geo.COLOURLIST, geo.write_colour_list(np.concatenate(col))))
        idx = np.concatenate(faces).reshape(-1).astype(np.uint32)
        pg.children.append(Chunk(geo.INDEXLIST, geo.write_u32_list(idx)))
        out.append(pg)

    pos, nrm, col = [], [], []
    uvs = {ch: [] for ch in uvchans}
    faces = []
    vbase = 0
    for pg, h in items:
        p = geo.read_vec3_list(pg.find(geo.POSITIONLIST)[0])
        f = _faces(h, geo.read_u32_list(pg.find(geo.INDEXLIST)[0]))
        if f is None or len(f) == 0:
            continue
        # split before overflowing the 16-bit index space
        if vbase and vbase + len(p) > VERT_CAP:
            flush(pos, nrm, uvs, col, faces, vbase)
            pos, nrm, col = [], [], []
            uvs = {ch: [] for ch in uvchans}
            faces, vbase = [], 0
        pos.append(p.astype(np.float32))
        if has_n:
            nrm.append(geo.read_vec3_list(pg.find(geo.NORMALLIST)[0]).astype(np.float32))
        uvmap = {geo.read_uv_list(c)[0]: geo.read_uv_list(c)[1] for c in pg.find(geo.UVLIST)}
        for ch in uvchans:
            uvs[ch].append(uvmap[ch].astype(np.float32))
        if has_c:
            col.append(geo.read_colour_list(pg.find(geo.COLOURLIST)[0]))
        faces.append((f + vbase).astype(np.uint32))
        vbase += len(p)
    if vbase:
        flush(pos, nrm, uvs, col, faces, vbase)
    return out


def merge_mesh(mesh, stats):
    pgs = mesh.find(geo.PRIMGROUP)
    if not pgs:
        return
    groups, kept, order = {}, [], []
    for pg in pgs:
        h = _mergeable(pg)
        if h is None:
            kept.append(pg); continue
        k = _merge_key(pg, h)
        if k not in groups:
            groups[k] = []; order.append(k)
        groups[k].append((pg, h))

    merged = []
    for k in order:
        items = groups[k]
        if len(items) == 1:
            merged.append(items[0][0])          # nothing to merge
        else:
            new = _build_merged(items, k)
            merged.extend(new)
            stats["groups_merged"] += 1
            stats["pg_in"] += len(items)
            stats["pg_out"] += len(new)

    new_pgs = merged + kept
    if len(new_pgs) == len(pgs):
        return                                  # no change

    # rebuild MESH children: prim groups first (loader reads numPrimGroups of
    # them), then the remaining chunks (BOX/SPHERE/RENDERSTATUS...) in order.
    non_pg = [c for c in mesh.children if c.id != geo.PRIMGROUP]
    mesh.children = new_pgs + non_pg

    # patch the MESH header's numPrimGroups field (pstr name, u32 ver, u32 nPG)
    r = Reader(mesh.payload)
    name = r.pstr(); ver = r.u32(); _old_npg = r.u32()
    mesh.payload = pack_pstr(name) + struct.pack("<II", ver, len(new_pgs)) + r.rest()
    stats["meshes_changed"] += 1


def merge_root(root, stats=None):
    if stats is None:
        stats = dict(meshes_changed=0, groups_merged=0, pg_in=0, pg_out=0)
    # v1: rigid MESH only (SKIN/skinned merge needs palette unification).
    for mesh in [c for c in root.walk() if c.id == geo.MESH]:
        merge_mesh(mesh, stats)
    return stats


def merge_file(src, dst, quiet=False):
    root = load(src)
    stats = merge_root(root)
    data = dump(root)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    load(dst)  # re-parse; raises if the rewrite corrupted the tree
    if not quiet:
        print(f"{src} -> {dst}")
        print(f"  meshes changed={stats['meshes_changed']} "
              f"groups merged={stats['groups_merged']} "
              f"prim groups {stats['pg_in']} -> {stats['pg_out']} "
              f"(net {stats['pg_in'] - stats['pg_out']} fewer draws)")
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    a = ap.parse_args()
    merge_file(a.src, a.dst)


if __name__ == "__main__":
    main()
