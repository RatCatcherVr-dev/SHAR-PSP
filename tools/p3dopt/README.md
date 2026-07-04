# p3dopt — offline .p3d texture optimizer for the PSP port

The PSP has a single ~22 MB malloc arena (24 MB user partition − 1 MB reserved
by `PSP_HEAP_SIZE_KB(-1024)` − 1 MB GE display list), shared by *everything*.
The native sceGU backend samples texture texels straight from that heap
(`gutex.cpp`: `sceGuTexImage(..., bits[0])`, no separate VRAM copy), so a
texture's **system-RAM cost == its texel cost**:

- paletted (`GU_PSM_T8`): **1 byte/texel**
- everything else (RGBA, or DXT decoded to RGBA — sceGU has no compressed
  sampler here): **4 bytes/texel**

The retail PC frontend assets are ~1105/1219 **true-colour** images → **24.3 MB
resident just for frontend + camset textures**, which alone exceeds the budget.
This tool rewrites the embedded PNGs to shrink that.

## Why this works / what it touches

- `.p3d` is a little-endian chunk tree (`P3D\xff` magic). `p3d.py` parses it into
  a `Chunk` tree and re-serializes with sizes recomputed bottom-up, so unknown
  chunks round-trip **byte-exact** (verified on frontend/camset/level/building
  files via `python3 p3d.py <files...>`).
- Images live in **IMAGE chunks (0x19001)** under many containers (TEXTURE,
  SPRITE, RESOURCEIMAGE, MULTISPRITE, …). The tool optimizes *every* IMAGE chunk.
- Texture creation is driven by the **PNG's own IHDR** (`texture.cpp:255` passes
  only name/size/format to `ParseAsTexture`), so we re-encode the PNG and update
  the surrounding chunk `w/h/bpp/palettized/alpha` fields to stay consistent.
- Blobs are all PNG (`format==1`); PIL handles decode/re-encode. Non-PNG blobs
  are skipped. Dimensions are kept **power-of-two** (GE requirement).

## Levers (each optional)

- `--paletteize` — quantize true-colour images to a 256-entry palette
  (4× smaller). Alpha-preserving (FASTOCTREE) for images with alpha, MEDIANCUT
  for opaque. **This is the big win, ~lossless.**
- `--maxdim N` — downscale each dimension to the nearest pow2 ≤ N (default 128).
  Lanczos resample. Extra squeeze; softens backgrounds, can hurt UI text.
- `--drop-mips` — keep only mip 0 of TEXTURE chunks. The sceGU path uses no
  mipmaps but every mip still allocates resident `bits[]`.

## Results (frontend.p3d + camset.p3d, 1219 images, 24.3 MB baseline)

| settings | resident | cut |
|---|---|---|
| `--paletteize --drop-mips` (full res) | **7.4 MB** | **69%** (~no quality loss) |
| `--paletteize --drop-mips --maxdim 128` | **5.5 MB** | **77%** (softer bg) |

Recommended default: **palette-only** (`--paletteize --drop-mips`), add
`--maxdim 128` only if you need the extra 2 MB.

## Geometry decimation (`decimate.py`)

For in-game **levels** geometry — not textures — is the RAM wall (~2–2.6 MB
resident per .p3d at 2×), and ~98% of level triangles live in triangle STRIPS.
The pass, per static (non-skinned) PRIMGROUP: reads positions + normals/uv(s)/
colours + indices, builds a triangle-list face array (converting strips→lists),
decimates with `fast_simplification` (quadric edge-collapse), replays the
collapses to get an old→new vertex mapping, carries attributes across it
(averaged; normals renormalized; colours in BGRA), rewrites the lists + PRIMGROUP
header as an indexed **triangle list** (`primType=0` — the GU backend renders it
via indexed `sceGuDrawArray`, gucon.cpp:65/678), and recomputes the MESH
BOX/SPHERE bounds. Skinned groups, non-triangle prims, and tiny meshes
(`--min-tris`) are left untouched; every output is re-parsed and structurally
validated (header/list counts, index ranges).

`--reduction R` = fraction of triangles to remove (0.5 = halve). Results:
`l1r3.p3d` 18,473→9,221 tris, resident geom **2.24→1.40 MB**; `l1z1.p3d`
13,098→6,538, **1.57→0.90 MB**. Shape verified by before/after wireframe render.

Complements the in-tree **`tVertexList`-free** code change (`primgroup.cpp`),
which halves the *other* geometry copy losslessly — do both.

## Setup & usage

Uses an isolated venv (Pillow + numpy + fast-simplification):

```bash
cd tools/p3dopt
python3 -m venv .venv && ./.venv/bin/pip install Pillow numpy fast-simplification
PY=./.venv/bin/python

# inspect where texture RAM goes
$PY analyze.py content/art/frontend/scrooby/frontend.p3d

# frontend tree: textures only, palette-only (safe, ~lossless)
$PY batch.py content/art/frontend/scrooby psp/dist/art/frontend/scrooby \
     --paletteize --drop-mips --maxdim 100000

# levels: textures + geometry decimation in one pass
$PY batch.py content/art psp/dist/art \
     --paletteize --drop-mips --decimate --reduction 0.5

# single-file passes
$PY optimize.py in.p3d out.p3d --paletteize --drop-mips [--maxdim 128]
$PY decimate.py in.p3d out.p3d --reduction 0.5

# round-trip codec self-test (must print OK for every file)
$PY p3d.py content/art/*.p3d
```

`batch.py` is directory-based (mirrors src→dst, copies non-.p3d verbatim; falls
back to a verbatim copy on any per-file error).

## Files

- `p3d.py` — chunk-tree codec (`load`/`dump`/`save`, `Chunk`, `Reader`, `pack_pstr`).
- `analyze.py` — texture inventory + resident-RAM estimate + projection.
- `optimize.py` — texture rewriter (`optimize_root`/`optimize_file`).
- `geo.py` — geometry chunk reader/writer (MESH/PRIMGROUP/lists, strip→list).
- `decimate.py` — geometry decimation pass (`decimate_root`/`decimate_file`).
- `batch.py` — directory-tree driver composing both passes in one load/dump.

## Not handled (yet)

- **Compressed `P3DZ` files** — need the engine's custom block-LZ
  (`tFileFTT::Decompress`) reimplemented first. Most shipped assets are
  uncompressed `P3D\xff`.
- **In-engine visual verification** — structural + visual (wireframe) checks pass;
  final fidelity must be confirmed by loading the `psp/dist` tree in PPSSPP.
- **Skinned-mesh decimation** — characters are skipped for safety (weights/matrix
  palette would need remapping).
