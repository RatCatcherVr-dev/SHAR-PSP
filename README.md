# The Simpsons: Hit & Run

This repository contains the source code for _The Simpsons: Hit & Run_ with quite a few enhancements done to it, Such as C++ 20 & x64 Builds, Vcpkg for easier third-party 
library management, UWP Builds & more. 

## Commit History

All previous commit history has been archived in [this branch](https://github.com/3UR/Simpsons-Hit-Run/tree/commit-history-archive). This was done because of how messy and inconsistent the commit messages were.

## Disclaimer

This repository contains modified source code and references to original game assets for preservation, and enhancement purposes only.

- This project is **not affiliated with, endorsed by, or sponsored by** the original publishers or intellectual property owners.
- No ownership of the original intellectual property is claimed.
- This project is **not monetized** in any way (no sales, ads, sponsorships, or donations).
- Commercial redistribution of this project or its builds is not permitted.

_The Simpsons: Hit & Run_ and all related characters, assets, audio, trademarks, and branding remain the property of their respective rights holders.

## Issues
If you encounter issues, please [create an issue](https://github.com/3UR/Simpsons-Hit-Run/issues/new).

### Known Bugs
- Memory Corruption
- Assets (Need a good way to include them)

## Contributing
If you would like to contribute to this project please [create a fork](https://github.com/3UR/Simpsons-Hit-Run/fork) and then [open a pull request](https://github.com/3UR/Simpsons-Hit-Run/pulls).

## Installation

### Quick Installation (Pre-built Binaries)
<!-- TODO: Clean up this section and improve instructions. -->

### Desktop

1. Download the latest build from the [Releases page](https://github.com/3UR/Simpsons-Hit-Run/releases/latest).
2. Extract the contents of the zip file to your desired location.
3. Navigate to the extracted folder and locate `SRR2.exe`.
4. You can now run the game.

### Xbox Series S|X
> [!WARNING]  
> Xbox One is currently not supported and will crash Xbox Series S|X is fine.

1. Navigate to the [Releases page](https://github.com/3UR/Simpsons-Hit-Run/releases/latest).
2. Download a file that looks like `SRR2_UWP_X.X.X.X_x64_XXX.appx`.
3. Now navigate to the Xbox dev mode portal (https://xbox:11443/ or the local IP and port the dev mode dashboard shows you).
4. Then press "Add" and drag and drop the AppX you downloaded.
5. Once done it should run.

### Developer Installation (Building from Source)
When working with the source you will need these installed:

- Visual Studio 2026
- C++ Language Support (In the Visual Studio installer, select the `Desktop development with C++` and `Universal Windows Platform development` options)
- Vcpkg support (When selecting `Desktop development with C++`, make sure `Vcpkg package manager` is selected on the right side)

#### Setup
> [!NOTE]  
> If you have already built Vcpkg projects before with Visual Studio then you can skip everything past step 3.

1. Open command prompt and navigate to a directory where you want to store the source and run `git clone --recurse-submodules https://github.com/3UR/Simpsons-Hit-Run`
2. When done you can open `SRR2.sln` with Visual Studio.
3. Once in Visual Studio press `Tools -> Command Line -> Developer Command Prompt`
4. A Developer Command Prompt will open enter the following `cd tools/vcpkg` and then `./bootstrap-vcpkg.bat`
5. Once that is done run `./vcpkg integrate install`

#### Building
If the setup was successful, you should now be able to build any project in the solution. When building for UWP don't forget to change the config! (example: if it's `ReleaseWindows` make it `ReleaseUwp`).

## PSP Port (Experimental)

An experimental **PSP** target (`RAD_PSP` platform macro) is in progress using the
`pspdev` toolchain. It reuses the engine's fixed-function renderer through a native
**sceGU** `pddi` backend (`src/libs/pure3d/pddi/psp`). The frontend main menu — 2D
UI, text, and the animated 3D living-room scene (Homer, the RC car, glows, TV) —
currently renders on **real PSP hardware** (tested on a PSP-2000).

### Building
Requires the `pspdev` SDK (`psp-gcc`, `psp-cmake`). From the repo root:
```
psp-cmake -S psp -B psp/build && make -C psp/build -j
```
The output EBOOT is `psp/build/EBOOT/EBOOT.PBP`.

### Asset Optimizer (`tools/p3dopt`)
The PSP has only ~22 MB of usable RAM, shared by everything, and the native sceGU
backend samples texture data straight from that heap — so a texture's resident cost
is its full decompressed size. The retail PC assets don't fit (the frontend's
textures alone are ~24 MB resident, mostly needless 32-bit true-colour), so
[`tools/p3dopt`](tools/p3dopt/README.md) rewrites `.p3d` files **offline** to shrink
them before they're deployed:

- **Textures** — paletteize true-colour images to an 8-bit CLUT (4× smaller),
  optionally downscale to a max power-of-two size, and drop unused mipmaps.
- **Geometry** — decimate meshes (quadric edge-collapse), converting triangle
  strips to lists and carrying UVs / normals / colours across the reduction.

It's a small Python tool (Pillow + numpy + `fast-simplification`); every rewrite is
re-parsed to verify it isn't corrupt. Palette-only is near-lossless and drops the
frontend's resident texture use from **~24 MB to ~7 MB**.

```
cd tools/p3dopt
python3 -m venv .venv && ./.venv/bin/pip install Pillow numpy fast-simplification

# textures only (safe default), whole frontend tree:
./.venv/bin/python batch.py content/art/frontend psp/dist/art/frontend --paletteize --drop-mips

# textures + geometry decimation (levels):
./.venv/bin/python batch.py content/art psp/dist/art --paletteize --drop-mips --decimate --reduction 0.5

# inspect where texture RAM goes before deciding settings:
./.venv/bin/python analyze.py content/art/frontend/scrooby/frontend.p3d
```

Then deploy the EBOOT plus the optimized assets from `psp/dist/` into
`PSP/GAME/SHAR/` on the memory stick. See [`tools/p3dopt/README.md`](tools/p3dopt/README.md)
for the full option reference.

## Media

### Windows
![Screenshot 2025-02-10 092453](https://github.com/user-attachments/assets/7b5c9c6a-259d-4e5d-bd07-e429bd2f54bb)

### Xbox
[_Watch HD on YouTube_](https://www.youtube.com/watch?v=qxqnziUVz9c)

https://github.com/user-attachments/assets/9793dccf-5dd6-4bbf-beb6-a6db33521a0b

[_Watch HD on YouTube_](https://www.youtube.com/watch?v=l_Ii-4Wygn8)

https://github.com/user-attachments/assets/ccfdb377-10ed-418b-a81b-932aad9938e1
