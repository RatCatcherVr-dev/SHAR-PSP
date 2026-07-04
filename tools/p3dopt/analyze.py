"""Inventory textures across .p3d files: format, dimensions, blob size, and the
estimated *resident* RAM cost on the PSP sceGU backend (paletted=1B/texel,
everything else decoded to 32-bit RGBA=4B/texel; sceGU has no compressed sampler
so DXT expands to RGBA in RAM)."""
import sys, glob, os
from collections import Counter
from p3d import load, Reader

TEXTURE, IMAGE, IMAGE_DATA, IMAGE_FILENAME = 0x19000, 0x19001, 0x19002, 0x19003
FMT = {0: "RAW", 1: "PNG", 2: "TGA", 3: "BMP", 4: "IPU", 5: "DXT", 6: "DXT1",
       7: "DXT2", 8: "DXT3", 9: "DXT4", 10: "DXT5", 11: "PS2_4", 12: "PS2_8",
       13: "PS2_16", 14: "PS2_32"}


def read_image(ch):
    r = Reader(ch.payload)
    name = r.pstr(); ver = r.u32()
    w = r.u32(); h = r.u32(); bpp = r.u32(); pal = r.u32(); alpha = r.u32(); fmt = r.u32()
    blob = None
    for c in ch.children:
        if c.id == IMAGE_DATA:
            rr = Reader(c.payload); sz = rr.u32(); blob = rr.rest()[:sz]
    return dict(name=name.decode('latin-1', 'replace'), w=w, h=h, bpp=bpp,
                pal=pal, alpha=alpha, fmt=fmt, blobsz=(len(blob) if blob else 0),
                external=any(c.id == IMAGE_FILENAME for c in ch.children))


def textures(root):
    # every IMAGE chunk is a resident texture on the GE, regardless of whether
    # its container is a TEXTURE, SPRITE, RESOURCEIMAGE, MULTISPRITE, ...
    for ch in root.walk():
        if ch.id == IMAGE:
            yield read_image(ch)


def resident_bytes(t):
    # sceGU: paletted stays 8-bit (1B/texel), else decoded to 32-bit RGBA.
    texels = t['w'] * t['h']
    return texels * (1 if t['pal'] else 4)


def opt_resident(t, maxdim=128):
    # projection: clamp each dim to nearest pow2 <= maxdim, paletteize to 8-bit.
    def clamp(v):
        p = 1
        while p * 2 <= min(v, maxdim):
            p *= 2
        return p
    return clamp(t['w']) * clamp(t['h']) * 1


def main(paths, maxdim=128):
    fmt_ct = Counter(); dim_ct = Counter(); pal_ct = Counter()
    total_blob = 0; total_resident = 0; total_opt = 0; ntex = 0; ext = 0
    biggest = []
    for path in paths:
        try:
            root = load(path)
        except Exception as e:
            print(f"skip {path}: {e}"); continue
        for t in textures(root):
            ntex += 1
            fmt_ct[FMT.get(t['fmt'], t['fmt'])] += 1
            dim_ct[f"{t['w']}x{t['h']}"] += 1
            pal_ct['paletted' if t['pal'] else 'truecolor'] += 1
            total_blob += t['blobsz']
            if t['external']:
                ext += 1; continue
            rb = resident_bytes(t); total_resident += rb
            total_opt += opt_resident(t, maxdim)
            biggest.append((rb, os.path.basename(path), t))
    print(f"\n=== {len(paths)} files, {ntex} textures ({ext} external-ref) ===")
    print("formats:", dict(fmt_ct.most_common()))
    print("paletted:", dict(pal_ct))
    print("top dims:", dict(dim_ct.most_common(12)))
    print(f"embedded blob total: {total_blob/1e6:.2f} MB")
    print(f"est. resident (sceGU) now:            {total_resident/1e6:.2f} MB")
    print(f"est. resident if <= {maxdim}px + 8-bit pal: {total_opt/1e6:.2f} MB  "
          f"({100*(1-total_opt/max(total_resident,1)):.0f}% saved)")
    biggest.sort(key=lambda x: x[0], reverse=True)
    print("\ntop 15 by resident RAM:")
    for rb, base, t in biggest[:15]:
        print(f"  {rb/1024:8.1f} KB  {t['w']}x{t['h']} {FMT.get(t['fmt'],t['fmt']):5} "
              f"pal={t['pal']} a={t['alpha']}  {base}:{t['name']}")


if __name__ == "__main__":
    args = sys.argv[1:]
    paths = []
    for a in args:
        paths += glob.glob(a) if any(c in a for c in "*?[") else [a]
    main(paths)
