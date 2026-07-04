"""Batch asset optimizer for the PSP port.

Mirrors SRC into DST: every *.p3d is loaded once, optionally texture-optimized
and/or geometry-decimated, then written; every other file is copied verbatim.
So a deployable PSP/GAME/SHAR tree comes out the other end.

  # frontend (textures only) — option (a)
  python3 batch.py content/art/frontend psp/dist/art/frontend --paletteize --drop-mips

  # levels (textures + geometry) — option (b)
  python3 batch.py content/art psp/dist/art --paletteize --drop-mips --decimate --reduction 0.5

Run with the venv python: tools/p3dopt/.venv/bin/python batch.py ...
"""
import argparse, os, shutil, sys
from p3d import load, dump
from optimize import optimize_root
from decimate import decimate_root


def process(src, dst, args):
    root = load(src)
    tex = geo = None
    if args.paletteize or args.maxdim < 100000 or args.drop_mips:
        tex = optimize_root(root, args.maxdim, args.paletteize, args.drop_mips)
    if args.decimate:
        geo = decimate_root(root, args.reduction, args.min_tris)
    data = dump(root)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    load(dst)  # re-parse; raises if a rewrite corrupted the tree
    return tex, geo, os.path.getsize(src), len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--maxdim", type=int, default=128)
    ap.add_argument("--paletteize", action="store_true")
    ap.add_argument("--drop-mips", action="store_true")
    ap.add_argument("--decimate", action="store_true")
    ap.add_argument("--reduction", type=float, default=0.5)
    ap.add_argument("--min-tris", type=int, default=64)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    tot_in = tot_out = 0; n_p3d = n_copy = n_err = 0
    for dirpath, _, files in os.walk(a.src):
        rel = os.path.relpath(dirpath, a.src)
        outdir = os.path.join(a.dst, rel) if rel != "." else a.dst
        os.makedirs(outdir, exist_ok=True)
        for fn in files:
            s = os.path.join(dirpath, fn); d = os.path.join(outdir, fn)
            if fn.lower().endswith(".p3d"):
                try:
                    tex, gs, osz, nsz = process(s, d, a)
                    tot_in += osz; tot_out += nsz; n_p3d += 1
                    if not a.quiet:
                        parts = [f"{osz/1e6:6.2f}->{nsz/1e6:6.2f}MB"]
                        if tex: parts.append(f"tex chg={tex['changed']} pal={tex['paletteized']}")
                        if gs: parts.append(f"geo dec={gs['decimated']} "
                                            f"tris={gs['tris_in']}->{gs['tris_out']}")
                        print(f"  {' '.join(parts)}  {os.path.join(rel, fn)}")
                except Exception as e:
                    n_err += 1
                    print(f"  ERR {os.path.join(rel, fn)}: {e}", file=sys.stderr)
                    shutil.copy2(s, d)   # fall back to verbatim copy on failure
            else:
                shutil.copy2(s, d); n_copy += 1
    print(f"\n{n_p3d} p3d processed, {n_copy} files copied, {n_err} errors")
    if tot_in:
        print(f"total .p3d on disk: {tot_in/1e6:.1f} -> {tot_out/1e6:.1f} MB "
              f"({100*(1-tot_out/tot_in):.0f}% smaller)")


if __name__ == "__main__":
    main()
