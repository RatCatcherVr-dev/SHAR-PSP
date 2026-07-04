<div align="center">

# 🎮 The Simpsons: Hit &amp; Run — PSP Port

**An experimental PlayStation Portable port of Radical Entertainment's 2003 classic** — running the original **SRR2 / Pure3D** engine on the handheld through a hand-written **sceGU** renderer.

[![Platform](https://img.shields.io/badge/platform-PSP-003791?logo=playstation&logoColor=white)](#)
[![Renderer](https://img.shields.io/badge/renderer-sceGU-2ea44f)](#)
[![Toolchain](https://img.shields.io/badge/toolchain-pspdev-orange)](https://pspdev.github.io/)
[![Language](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](#)
[![Frontend](https://img.shields.io/badge/frontend-running%20on%20hardware-success)](#)

</div>

---

> The engine's fixed-function pipeline runs on a native **sceGU** backend, and the retail PC assets are squeezed offline to fit the PSP's ~22 MB of RAM. The animated frontend menu already renders on a **real PSP‑2000**.

## ✨ Status

**What works** (verified on real PSP‑2000 hardware, and on PPSSPP):

- ✅ Native **sceGU** `pddi` renderer (`src/libs/pure3d/pddi/psp`) — replaces the fixed-function GL path
- ✅ **radcore** — custom heaps, async **radfile** with a Memory‑Stick drive, threads & timing
- ✅ **Pure3D** asset & scene loading — chunk files, geometry, textures (CLUT/paletted), skeletons, CPU-skinned composites, scene graph & keyframe animation
- ✅ **Scrooby** frontend — 2D UI, fonts & text
- ✅ The **animated 3D menu** — Homer's living room, the RC car, item glows and the TV, framed over the 2D menu
- ✅ Offline **asset optimizer** to make the retail assets fit in RAM

**On the roadmap:**

- 🚧 Gameplay (`GameFlow` contexts), player input (`pspctrl`) & audio (`sceAudio`)
- 🚧 Game-asset packaging
- 🚧 Native DXT/T4 texture sampling + narrower cache flushes for extra speed

## 🔨 Building

Requires the [`pspdev`](https://pspdev.github.io/) SDK on your `PATH` (`psp-gcc`, `psp-cmake`). From the repo root:

```sh
psp-cmake -S psp -B psp/build && make -C psp/build -j
```

The bootable EBOOT is written to `psp/build/EBOOT/EBOOT.PBP`.

## 📦 Asset Optimizer (`tools/p3dopt`)

The PSP has only **~22 MB** of usable RAM, shared by everything, and the sceGU backend samples texture data straight from that heap — so a texture's resident cost is its full decompressed size. The retail PC assets don't fit (the frontend's textures alone are **~24 MB** resident, mostly needless 32‑bit true‑colour), so [`tools/p3dopt`](tools/p3dopt/README.md) rewrites `.p3d` files **offline** before they're deployed:

| Lever | What it does |
| --- | --- |
| 🎨 **Textures** | Paletteize true‑colour images to an 8‑bit CLUT (4× smaller), optionally downscale to a max power‑of‑two size, and drop unused mipmaps |
| 📐 **Geometry** | Decimate meshes (quadric edge‑collapse), converting triangle strips → lists and carrying UVs / normals / colours across the reduction |

It's a small, dependency‑light Python tool (Pillow · numpy · `fast-simplification`), and every rewrite is re‑parsed to verify it isn't corrupt. Palette‑only is **near‑lossless** and drops the frontend's resident texture use from **~24 MB → ~7 MB**.

```sh
cd tools/p3dopt
python3 -m venv .venv && ./.venv/bin/pip install Pillow numpy fast-simplification

# textures only (safe default) — the whole frontend tree
./.venv/bin/python batch.py content/art/frontend psp/dist/art/frontend --paletteize --drop-mips

# textures + geometry decimation — for levels
./.venv/bin/python batch.py content/art psp/dist/art --paletteize --drop-mips --decimate --reduction 0.5

# inspect where texture RAM goes before choosing settings
./.venv/bin/python analyze.py content/art/frontend/scrooby/frontend.p3d
```

See [`tools/p3dopt/README.md`](tools/p3dopt/README.md) for the full option reference.

## 🚀 Deploying

Copy the build output and the optimized assets onto the memory stick:

```
PSP/GAME/SHAR/
├── EBOOT.PBP                    # from psp/build/EBOOT/
└── art/ …                       # optimized assets from psp/dist/
```

Then launch **SHAR** from the PSP's Game menu (or point PPSSPP at the memory‑stick folder).

## ⚖️ Disclaimer

This repository contains modified source code and references to original game assets for **preservation and enhancement purposes only**.

- **Not affiliated with, endorsed by, or sponsored by** the original publishers or IP owners.
- No ownership of the original intellectual property is claimed.
- **Not monetized** in any way (no sales, ads, sponsorships, or donations).
- Commercial redistribution of this project or its builds is not permitted.

_The Simpsons: Hit & Run_ and all related characters, assets, audio, trademarks, and branding remain the property of their respective rights holders.

## 🙏 Built On

This port builds on the [Simpsons: Hit &amp; Run source project](https://github.com/3UR/Simpsons-Hit-Run) (the C++20 / x64 / UWP‑enhanced SRR2 tree). Issues and contributions for the PSP work are welcome via fork &amp; pull request.
