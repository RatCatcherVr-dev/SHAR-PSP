# PSP test EBOOT — `shartest`

Brings the SHAR (SRR2) engine up end-to-end on PSP and loads a real game `.p3d`
off the Memory Stick through the ported stack:

```
radcore (thread/memory/time/file + PSP Memory-Stick drive)
  -> pddi (GL/EGL device, display, render context)
    -> Pure3D tContext (via tPlatform) + minimal chunk loaders
      -> p3d::load("test.p3d")
```

## Build

```
psp-cmake -S psp -B psp/build && make -C psp/build shartest
```

Output: `psp/build/EBOOT/EBOOT.PBP` (also staged as `psp/dist/EBOOT_p3dtest.pbp`).

## Run (PPSSPP or hardware)

1. Copy the EBOOT to a game folder on the Memory Stick:
   `ms0:/PSP/GAME/shartest/EBOOT.PBP`
2. Put a test asset next to it, named `test.p3d`. Any file from the retail
   `content/art/` tree works — e.g. a frontend card
   (`content/art/frontend/scrooby2/resource/_frontend/card07.p3d`) or a small
   building (`content/art/b02.p3d`). Copy it to
   `ms0:/PSP/GAME/shartest/test.p3d`.
3. Launch from the PSP Game menu (or `PPSSPPSDL ms0:/PSP/GAME/shartest/EBOOT.PBP`).

## What you should see (no debugger needed)

The screen pulses a solid colour each frame — the colour reports the result:

| Colour | Meaning |
|--------|---------|
| pulsing **green** | engine came up **and** `test.p3d` loaded through the chunk loader |
| pulsing **red**   | engine came up but the load returned a hard failure |
| pulsing **blue**  | no context / no `test.p3d` found |

A steady pulse of *any* colour already proves radcore init, the PSP Memory-Stick
file backend, pddi device/display/context creation, and the Pure3D `tContext`
frame loop all run on the target. Green additionally proves `radfile` read the
file and the Pure3D chunk parser walked it.

Press **HOME** to exit cleanly.

## Next step — drawing the mesh

The loaded geometry is not yet rendered. To draw it, after a successful load:
find the first `tGeometry` in `p3d::inventory`, set up a `tView`/`tCamera`
(projection + view matrix), and call `geometry->Display()` inside the
`BeginFrame`/`EndFrame` block (the marked spot in `main.cpp`). This needs the
asset's shaders/textures to decode and the vertex format to be one pspGL
supports — best iterated against a known-simple asset on the emulator.
