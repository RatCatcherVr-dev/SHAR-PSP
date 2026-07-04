"""Offline .p3d texture optimizer for the PSP port.

Shrinks resident texture RAM (the sceGU backend samples texel data straight
from the malloc heap, so system-RAM cost == texture cost) by three levers,
each optional:

  --maxdim N     downscale every texture so each dimension is the nearest
                 power-of-two <= N (GE requires pow2). Default 128.
  --paletteize   quantize true-colour textures to an 8-bit palette (4x smaller:
                 1 B/texel vs 4). Already-paletted PNGs stay paletted.
  --drop-mips    keep only mip level 0 of TEXTURE chunks. The PSP sceGU path
                 uses no mipmaps (gutex.cpp), but every mip still allocates
                 resident bits[].

Images live in IMAGE chunks (0x19001) under many containers — TEXTURE, SPRITE,
RESOURCEIMAGE, MULTISPRITE — so we optimize *every* IMAGE chunk, not just those
under a TEXTURE. Only PNG-encoded blobs (format==1) are touched; every other
chunk passes through byte-exact. Texture creation is driven by the PNG's own
IHDR (texture.cpp:255 passes only name/size/format), so we re-encode the PNG and
keep the surrounding chunk fields consistent with it.
"""
import argparse, io, os, struct
from PIL import Image
from p3d import load, dump, Reader, pack_pstr

TEXTURE, IMAGE, IMAGE_DATA = 0x19000, 0x19001, 0x19002
FMT_PNG = 1


def _pow2_le(v, cap):
    p = 1
    while p * 2 <= min(v, cap):
        p *= 2
    return p


def _has_real_alpha(im):
    if im.mode == "RGBA":
        lo, _ = im.getchannel("A").getextrema()
        return lo < 255
    if im.mode == "LA":
        return True
    if im.mode == "P" and "transparency" in im.info:
        return True
    return False


def _to_paletted(rgba, alpha):
    # FASTOCTREE is the one PIL quantizer that keeps an alpha channel; MEDIANCUT
    # gives cleaner palettes for opaque images.
    if alpha:
        return rgba.quantize(colors=256, method=Image.FASTOCTREE)
    return rgba.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT)


def _parse_image_fields(payload):
    r = Reader(payload)
    name = r.pstr(); ver = r.u32()
    w = r.u32(); h = r.u32(); bpp = r.u32(); pal = r.u32(); alpha = r.u32(); fmt = r.u32()
    return dict(name=name, ver=ver, w=w, h=h, bpp=bpp, pal=pal, alpha=alpha, fmt=fmt)


def _build_image_payload(f):
    return pack_pstr(f["name"]) + struct.pack(
        "<IIIIIII", f["ver"], f["w"], f["h"], f["bpp"], f["pal"], f["alpha"], f["fmt"])


def _parse_texture_fields(payload):
    r = Reader(payload)
    name = r.pstr(); ver = r.u32()
    d = dict(name=name, ver=ver, w=r.u32(), h=r.u32(), bpp=r.u32(), ad=r.u32(),
             nmip=r.u32(), ttype=r.u32(), usage=r.u32(), prio=r.u32())
    return d


def _build_texture_payload(f):
    return pack_pstr(f["name"]) + struct.pack(
        "<IIIIIIIII", f["ver"], f["w"], f["h"], f["bpp"], f["ad"], f["nmip"],
        f["ttype"], f["usage"], f["prio"])


def _optimize_png_blob(blob, maxdim, paletteize, stats):
    """Return (new_blob, w, h, bpp, pal, alpha) or None if left unchanged."""
    im = Image.open(io.BytesIO(blob)); im.load()
    was_pal = im.mode == "P"
    ow, oh = im.size
    nw, nh = _pow2_le(ow, maxdim), _pow2_le(oh, maxdim)
    alpha0 = _has_real_alpha(im)

    need_resize = (nw, nh) != (ow, oh)
    need_pal = paletteize and not was_pal and im.mode in ("RGB", "RGBA")
    if not need_resize and not need_pal:
        return None  # already small & in the mode we want -> keep original bytes

    if need_resize:
        im = im.convert("RGBA").resize((nw, nh), Image.LANCZOS)
        stats["downscaled"] += 1

    alpha = _has_real_alpha(im)
    if was_pal or need_pal:
        im = _to_paletted(im.convert("RGBA"), alpha)
        if need_pal:
            stats["paletteized"] += 1

    out = io.BytesIO()
    im.save(out, format="PNG", optimize=True)
    blob2 = out.getvalue()
    pal = 1 if im.mode == "P" else 0
    bpp = 8 if pal else (32 if alpha else 24)
    return blob2, im.size[0], im.size[1], bpp, pal, (1 if alpha else 0)


def _optimize_image_chunk(img, maxdim, paletteize, stats):
    ifields = _parse_image_fields(img.payload)
    if ifields["fmt"] != FMT_PNG:
        stats["skipped_nonpng"] += 1
        return None
    data = [c for c in img.children if c.id == IMAGE_DATA]
    if not data:
        return None
    r = Reader(data[0].payload); sz = r.u32(); blob = r.rest()[:sz]
    stats["bytes_in"] += sz
    try:
        res = _optimize_png_blob(blob, maxdim, paletteize, stats)
    except Exception as e:
        stats["errors"] += 1
        print(f"    ! {ifields['name'].decode('latin-1','replace')}: {e}")
        stats["bytes_out"] += sz
        return None
    if res is None:
        stats["bytes_out"] += sz
        stats["unchanged"] += 1
        return None
    blob2, w, h, bpp, pal, alpha = res
    stats["bytes_out"] += len(blob2)
    stats["changed"] += 1
    data[0].payload = struct.pack("<I", len(blob2)) + blob2
    ifields.update(w=w, h=h, bpp=bpp, pal=pal, alpha=alpha)
    img.payload = _build_image_payload(ifields)
    return (w, h, bpp)


def optimize_root(root, maxdim, paletteize, drop_mips):
    """Mutate an already-loaded chunk tree in place; return texture stats."""
    stats = dict(changed=0, unchanged=0, downscaled=0, paletteized=0,
                 mips_dropped=0, skipped_nonpng=0, errors=0, bytes_in=0, bytes_out=0)

    # 1) TEXTURE chunks: optionally drop mips, then optimize surviving IMAGE(s)
    #    and keep the TEXTURE header (w/h/bpp/nmip) consistent with mip 0.
    for tex in [c for c in root.walk() if c.id == TEXTURE]:
        imgs = [c for c in tex.children if c.id == IMAGE]
        if drop_mips and len(imgs) > 1:
            stats["mips_dropped"] += len(imgs) - 1
            keep = imgs[0]
            tex.children = [c for c in tex.children if c.id != IMAGE] + [keep]
            imgs = [keep]
        top = None
        for i, img in enumerate(imgs):
            r = _optimize_image_chunk(img, maxdim, paletteize, stats)
            if i == 0 and r:
                top = r
        tf = _parse_texture_fields(tex.payload)
        if top:
            tf["w"], tf["h"], tf["bpp"] = top
        tf["nmip"] = len(imgs)
        tex.payload = _build_texture_payload(tf)

    # 2) Every other IMAGE chunk (sprites, resource images) — standalone.
    for parent, img in root.walk_parent():
        if img.id == IMAGE and (parent is None or parent.id != TEXTURE):
            _optimize_image_chunk(img, maxdim, paletteize, stats)
    return stats


def optimize_file(src, dst, maxdim, paletteize, drop_mips):
    root = load(src)
    stats = optimize_root(root, maxdim, paletteize, drop_mips)
    data = dump(root)
    os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
    with open(dst, "wb") as f:
        f.write(data)
    load(dst)  # re-parse; raises if the rewrite corrupted the tree
    return stats, os.path.getsize(src), len(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src"); ap.add_argument("dst")
    ap.add_argument("--maxdim", type=int, default=128)
    ap.add_argument("--paletteize", action="store_true")
    ap.add_argument("--drop-mips", action="store_true")
    a = ap.parse_args()
    s, osz, nsz = optimize_file(a.src, a.dst, a.maxdim, a.paletteize, a.drop_mips)
    print(f"{a.src} -> {a.dst}")
    print(f"  file: {osz/1e6:.2f} -> {nsz/1e6:.2f} MB ({100*(1-nsz/osz):.0f}% smaller)")
    print(f"  images changed={s['changed']} unchanged={s['unchanged']} "
          f"downscaled={s['downscaled']} paletteized={s['paletteized']} "
          f"mips_dropped={s['mips_dropped']} nonpng={s['skipped_nonpng']} err={s['errors']}")
    print(f"  embedded PNG bytes: {s['bytes_in']/1e6:.2f} -> {s['bytes_out']/1e6:.2f} MB")


if __name__ == "__main__":
    main()
