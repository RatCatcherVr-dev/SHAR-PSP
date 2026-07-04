# PSP Port — Plan to Render Actual Game Content

Status as of 2026-06-29. Builds on the completed **render foundation** (pddi
`Create → Device → Display(EGL) → RenderContext → BeginFrame/Clear/EndFrame/SwapBuffers`).
The EBOOTs in `psp/dist/` (`gl_triangle`, `pddi_render`, `probe`) run, so Gate 0 / Phase 1 are validated.

## PROGRESS — Phases 2 & 3 COMPLETE (compile side)

All four MIPS32 static libs build clean (`make -C psp/build`):
`libradmath_psp.a`, `libradcore_psp.a` (now incl. radtime + radfile), `libpddigl_psp.a`,
and the new **`libpure3d_p3d_psp.a`** (33 objs — the asset/scene/geometry/texture layer).

- **Phase 2a (radtime): DONE.** `time.cpp` is `std::chrono`-based and portable; only needed the
  `radTime64` typedef guard (`radtime.hpp:88`) extended to `RAD_PSP`. Added `time.cpp`+`stopwatch.cpp` to CMake.
- **Phase 2b (radfile): DONE.** Wrote `radfile/psp/pspdrives.{hpp,cpp}` — `radPspDrive : radDrive` over
  newlib stdio (`fopen/fread/fseek`), which pspdev routes to the Memory Stick. The async worker-thread +
  request-queue framework (`radfile/common/`, 13 files) compiles unmodified. `radFileHandle = FILE*`.
  Default drive = `"ms0:"`. Added PSP branches to `platformdrives.{hpp,cpp}`.
- **Phase 3 (p3d layer): DONE (compiles).** New `pure3d_p3d_psp` CMake target. Created the PSP platform
  header trio `p3d/platform/psp/{platform,plat_types,plat_filemap}.hpp` (modeled on win32; **plain-int
  P3D_U* types**, NOT `<cstdint>` — newlib's `uint32_t`=`unsigned long` on MIPS clashes with pddi's
  `PDDI_U32`=`unsigned int`). Routed the three p3d platform selectors + `p3d/buildconfig.hpp` to `RAD_PSP`.

### radcore/pure3d guard edits made this session (all `+ RAD_PSP`)
`radtime.hpp` (radTime64), `radfile.hpp` (radMemcardInfo stub), `raddebug.hpp` (3: rDebugPrintf no-op,
rTunePrintf no-op, `rDebugAssertFail_Implementation` prototype — impl in `debug.cpp` is unguarded),
`radfile/common/platformdrives.{hpp,cpp}`, `p3d/buildconfig.hpp` (2 guards),
`p3d/{platform,plat_types,plat_filemap}.hpp` (selectors), `p3d/png.cpp` (`#include <string.h>` for memset).
New files: `radfile/psp/pspdrives.{hpp,cpp}`, `p3d/platform/psp/{platform,plat_types,plat_filemap}.hpp`.

### Phase 4 is now unblocked — key facts for the EBOOT harness
- **No PSP `tPlatform`/`platform.cpp` needed.** `tContext` has a public ctor
  `tContext(pddiDevice*, pddiDisplay*, pddiRenderContext*)` (symbol-verified in the archive). The harness
  replicates `tPlatform::CreateContext`'s recipe inline: `pddiCreate → NewDisplay → InitDisplay(EGL) →
  NewRenderContext → new tContext(dev,disp,ctx) → ctx->Setup()`. It already does the pddi part.
- **Do NOT call `p3d::InstallDefaultLoaders()`** (loaders.cpp) — it `new`s loaders from EXCLUDED files
  (lights, sprites, billboards, anim, scenegraph) and would force-link them. Register the minimal set
  manually: `GetLoadManager()->GetP3DHandler()->AddHandler(new tGeometryLoader/tShaderLoader/tTextureLoader)`
  + image handlers (`tPNGHandler`/`tBMPHandler`/`tTargaHandler`). Static-lib linking only pulls `loaders.cpp.obj`
  if `InstallDefaultLoaders` is referenced, so avoiding it keeps the excluded subsystems out of the link.
- Link line adds `-lpure3d_p3d_psp` ahead of `-lpddigl_psp -lradcore_psp -lradmath_psp -lGL ... -lpng -lz`.

The text below is the original plan; Phases 2–3 above supersede their "exit criteria".

---

## (original) Status

## The gap, precisely

What works today: the **pddi device layer** (`pddi/base` + `pddi/gl` + PSP EGL display) compiles and links.
It can clear the screen and swap buffers.

What's missing to draw a real model: the **Pure3D asset + scene layer** (`src/libs/pure3d/p3d/`, 57 `.cpp`
plus `anim/ effects/ platform/ scenegraph/ shadow/`) is **not in the PSP build** (`psp/CMakeLists.txt`
only builds `pddigl_psp`). Its loader path depends on two radcore backends that **do not exist for PSP yet**:

- `radfile` — async file I/O. `p3d/loadmanager.cpp` → `p3d/fileftt.hpp` (radfile-backed `tFile`). Excluded from PSP build.
- `radtime` — `loadmanager.cpp` includes `radtime.hpp`; used widely. Excluded from PSP build.

So the critical path is: **prove pixels render → build the primitive path → port radtime + radfile →
compile the p3d layer → load a real `.p3d` and call `Display()`.**

---

## Gate 0 — Visually confirm the render foundation (BLOCKING, do first)

Everything downstream assumes pspGL+EGL actually puts pixels on screen. This is currently unproven
(built + symbol-verified only). Cheapest possible validation before investing in the asset stack.

- Run `psp/dist/EBOOT_pddi_render.pbp` (animated clear-colour loop) in **PPSSPP** on the Windows host.
- Pass: screen cycles clear colours. Fail: debug EGL config / `eglSwapBuffers` / `sceGu` init in
  `src/libs/pure3d/pddi/gl/display_psp/gldisplay.cpp` before anything else.
- Note: `DrawString` is `#ifndef RAD_RELEASE`, so text won't show in the RAD_RELEASE build — rely on clear colour.

**Exit criteria:** confirmed colour change on PPSSPP (and ideally real hardware).

---

## Phase 1 — Hardcoded geometry through the engine's own pddi (no asset I/O)

Goal: prove the **primitive + matrix path** on PSP independent of radfile/radtime/p3d-layer. This is the
smallest step from "clear screen" to "a 3D triangle the engine drew."

Tasks:
1. In the EBOOT harness, after context init: `pddiShader* s = device->NewShader("..."/* base unlit */)`.
   The fixed-function GL backend (`pddi/gl/glmat.cpp`, `glshader`) is already in `pddigl_psp`.
2. Set up projection + view: push an identity/ortho or simple perspective matrix via the pddi context
   matrix API (`pddiRenderContext::SetWorldMatrix/SetViewMatrix/SetProjectionMatrix` — see `pddi/base/basecontext.cpp`).
3. `pddiPrimStream* st = ctx->BeginPrims(s, PDDI_PRIM_TRIANGLES, PDDI_V_C, 3)` → emit 3 `Vertex()` → `EndPrims(st)`.
4. Render inside the existing `BeginFrame/EndFrame/SwapBuffers` loop.

Risks: fixed-function matrix state on pspGL (glMatrixMode/glLoadMatrix path); vertex format flags
(`PDDI_V_C`, `PDDI_V_CT`) must map to a pspGL-supported interleaved format. All fixed-function, low risk.

**Exit criteria:** a coloured triangle (then a textured quad) rendered by `pddiRenderContext`, confirmed on PPSSPP.
This validates everything `tGeometry::Display()` ultimately calls.

---

## Phase 2 — Port `radtime` and `radfile` backends for PSP

These are the two radcore backends the asset loader needs. Both are bounded and well-scoped.

### 2a. radtime (small — do first)
- Provide the `radTime64` typedef and a PSP sysclock source. PSP options: `sceKernelGetSystemTimeWide()`
  (microsecond wide timer) or SDL2 ticks (already linked).
- Files: extend guards in `src/libs/radcore/include/radtime.hpp`; add `radtime` source to `radcore_psp`
  in `psp/CMakeLists.txt`. Reimplement the platform timing source (mirror the win32/SDL path).
- Effort: small (~half a day). Mostly a typedef + one clock call.

### 2b. radfile (one `radDrive` subclass — the framework gives async/threading for free)
Reference: `src/libs/radcore/src/radfile/win32/win32drive.{hpp,cpp}` (~450 lines, std::fstream).
The async worker-thread + request-queue infrastructure in `radfile/common/` is platform-independent and
already builds; a backend only blocks inside the drive thread, so **no true async needed**.

Create:
- `src/libs/radcore/src/radfile/psp/pspdrives.hpp` — `class radPspDrive : public radDrive` + `radPspDriveFactory(...)`.
- `src/libs/radcore/src/radfile/psp/pspdrives.cpp` (~300–400 lines) — implement the 5 methods that matter:
  `Initialize`, `OpenFile`, `ReadFile`, `WriteFile` (write optional initially), `CloseFile`, returning
  `radDrive::CompletionStatus`. Back with `sceIoOpen/Read/Lseek/Close` (or newlib `fopen/fread`).

Modify:
- `radfile/common/platformdrives.hpp` — add `#elif defined(RAD_PSP)` handle typedef (`typedef int radFileHandle;`).
- `radfile/common/platformdrives.cpp` — add RAD_PSP branch in the 3 dispatch functions (default drive `"ms:"`,
  validate, factory).
- `radcore/include/radfile.hpp` — `RAD_PSP` already in the `#error` guard; update message only.
- `psp/CMakeLists.txt` — add `radfile/common/*.cpp` (13 files) + `radfile/psp/pspdrives.cpp` to `radcore_psp`.
  Exclude `radfile/{win32,ps2,xbox}`.

Dependencies (all already in `radcore_psp`): radmemory, radobject, radthread (SDL2-backed), radstring, raddebug.

**Shortcut if 2b slips:** load assets via `tFileMem` from a buffer embedded in the EBOOT (or read once with
raw `sceIo` outside radfile). Unblocks Phase 4 without the full radfile port. Revisit radfile for real streaming.

**Exit criteria:** `radFileOpen("ms:/test.bin")` + read returns correct bytes on PPSSPP, serviced via `radFileService()`.

---

## Phase 3 — Compile the Pure3D `p3d/` asset + scene layer for PSP

Add a fourth static lib (`pure3d_p3d_psp`) or fold p3d sources into the existing pure3d lib.

Tasks:
1. Add `p3d/*.cpp` to the CMake build incrementally. Start with the **minimal render set**:
   `context.cpp utility.cpp memory.cpp loadmanager.cpp inventory.cpp chunkfile.cpp file.cpp fileftt.cpp
   drawable.cpp geometry.cpp primgroup.cpp shader.cpp texture.cpp matrixstack.cpp view.cpp camera.cpp`.
2. Resolve dependency creep — the real unknown of this phase. Expect to hit:
   - `radmemorymonitor.hpp` (in `context.cpp`) — excluded from PSP. Guard it out / stub (it's dev-only, no-op in RAD_RELEASE).
   - Image decoding for textures (`texture.cpp` → `tImageFactory`/converters). PNG decode needs **libpng**
     (pspdev ships it). Identify which decoders the chosen test asset needs and compile only those.
   - `choreo`/`poser`/`sim` pull-ins via animation/composite drawables — **avoid** by sticking to static
     `tGeometry` (skip `anim/` for now).
3. Endianness: PSP is little-endian MIPS; PC-authored `.p3d` is little-endian → `tFile` endian-swap off. No work expected.
4. Wire it into the harness link line alongside `-lpddigl_psp -lradcore_psp -lradmath_psp`.

Risks (highest of the whole plan): transitive includes may drag in large subsystems. Mitigate by adding
files one at a time and stubbing/guarding dev-only and animation paths until the static-mesh set links clean.

**Exit criteria:** the minimal p3d source set compiles and links into the EBOOT for MIPS32.

---

## Phase 4 — Load a real `.p3d` and render `tGeometry`

The payoff. With Phases 1–3 done, wire the documented minimal loop:

```cpp
tContext* p3dCtx = new tContext(device, display, renderContext);   // from Phase 1 pddi objects
tP3DFileHandler* h = p3dCtx->GetLoadManager()->GetP3DHandler();
h->AddHandler(new tGeometryLoader());
h->AddHandler(new tShaderLoader());
h->AddHandler(new tTextureLoader());

p3d::load("ms:/test.p3d");                                          // sync; radfile-backed (Phase 2)
tGeometry* model = p3d::find<tGeometry>(p3dCtx->GetInventory(), "MeshName");

// per frame:
p3dCtx->BeginFrame();
//   set camera/view via p3d view + matrixstack
model->Display();                                                  // → tPrimGroup::Display → pddi->DrawPrimBuffer
p3dCtx->EndFrame(true);                                            // swap
```

Render path (all already compiled by Phase 3): `tGeometry::Display()` → `tPrimGroupOptimised::Display()` →
`p3d::pddi->DrawPrimBuffer(shader, buffer)` (`p3d/primgroup.cpp:125`), or streamed via `BeginPrims/EndPrims`
(the path Phase 1 already proved).

Test asset: the simplest is a single static textured mesh authored as a standalone `.p3d` (a `tGeometry` +
`tShader` + small `tTexture`). The shipped game stores assets inside `.rcf`/cement archives — extracting or
authoring one small `.p3d` is part of this phase (see Phase 5).

**Exit criteria:** a textured static mesh from a `.p3d` file on screen on PPSSPP.

---

## Phase 5 — Asset acquisition / packaging (parallelizable with 1–4)

- Obtain or author **one small `.p3d`** for Phase 4 testing. Options: extract a simple prop from the game's
  archives with existing community P3D tools, or author a cube/quad `.p3d` with a known chunk layout
  (chunk IDs in `src/libs/pure3d/constants/chunkids.hpp`).
- Memory budget: PSP has ~24–32 MB shared + 2 MB VRAM. SHAR was a 32 MB PS2-era title; track the working set
  from the first real asset load. This is the project's biggest long-term risk (per port memory).
- Cement/`.rcf` archive support (`radfile/common/cementer.cpp`) is already platform-independent and would
  ride along once radfile is up — defer until a single loose `.p3d` renders.

---

## Recommended order & rough sizing

| # | Phase | Size | Blocking? |
|---|-------|------|-----------|
| 0 | Visual confirm clear-screen on PPSSPP | hours | YES — gates everything |
| 1 | Hardcoded geometry via engine pddi | 1–2 days | proves primitive/matrix path |
| 2a | radtime PSP backend | ~0.5 day | needed by p3d loader |
| 2b | radfile PSP backend (or tFileMem shortcut) | 1–3 days | needed to load assets |
| 3 | Compile p3d asset/scene layer | 2–5 days | dep-creep is the wildcard |
| 4 | Load `.p3d`, render tGeometry | 1–2 days | the payoff |
| 5 | Test asset + memory budget | ongoing, parallel | — |

Do **0 → 1** first: they need no new backends and de-risk the renderer. Run **5** (get a test `.p3d`) in
parallel. Then **2 → 3 → 4** is the asset critical path. Keep the **tFileMem shortcut** in pocket so a slow
radfile port can't block first-pixels-of-content.

## Conventions reminder
- Repo is CRLF — use `perl -i -pe` for guard edits, not sed/Edit, on the radcore/radfile headers.
- The PSP-specific display backend dir (`display_psp/`) is LF, so Edit tool works there.
- Every TU needs one build-target macro (`RAD_RELEASE`) + one platform macro (`RAD_PSP`).
