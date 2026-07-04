//=============================================================================
// Native PSP sceGU pddi backend — render context + prim buffer.
//
// The whole point of this backend: geometry is submitted to the GE with a
// single sceGuDrawArray per prim group (no per-draw shim/dcache/sync overhead
// like pspGL), and paletted textures use the GE's native CLUT.
//=============================================================================
#include <pddi/psp/gucon.hpp>
#include <pddi/psp/gudev.hpp>
#include <pddi/psp/gudisplay.hpp>
#include <pddi/psp/gumat.hpp>
#include <pddi/psp/gutex.hpp>

#include <pspgu.h>
#include <pspgum.h>
#include <psputils.h>   // sceKernelDcacheWritebackRange
#include <radmemory.hpp>
#include <string.h>
#include <math.h>
#include <stdio.h>

// GE display list (16-byte aligned, big enough for a full menu/scene frame).
unsigned int __attribute__((aligned(16))) g_guDisplayList[262144];

// Per-frame draw counters, read by the harness PERF log (kept from the pspGL
// era so we can measure the sceGU backend's draw counts the same way).
int g_pspDrawBuffer = 0;
int g_pspDrawStream = 0;

// TEST: set true around CPU-skinned character draws so the material path can
// treat just those draws as opaque (isolate whether Homer is alpha-blended away).
bool g_pspSkinnedDraw = false;

// DIAG: set true (by FePure3dObject::Render, around the gaghomer/homer draw) to
// force that object to draw as an untextured WHITE silhouette with depth test
// OFF, i.e. on top of everything. This separates the failure modes:
//   * silhouette appears over the room  -> Homer was occluded / z-fought away
//   * still nothing                     -> Homer is off-screen or collapsed
//     (cross-check shar_skel.log for a wild/NULL bone in the matrix palette).
bool g_pspDebugChar = false;

// The pspGL room-backdrop cache is obsolete with sceGU (drawing the room live
// is cheap now). Provide no-op stubs so the harness / FePure3dObject still link;
// pglRoomBackdropReady()==false makes camset render normally.
extern "C" void pglDrawRoomBackdrop(void) {}
extern "C" void pglCaptureRoomBackdrop(void) {}
bool pglRoomBackdropReady(void) { return false; }

// Canonical interleaved GU vertex. GU requires components in this exact order:
// (weights) texture, colour, normal, position. We always emit all three of
// texture/colour/normal so one vertex layout serves every prim group.
struct GuVert
{
    float    u, v;
    unsigned colour;      // 0xAABBGGRR
    float    nx, ny, nz;
    float    x, y, z;
};

static const int GU_VTYPE_BASE =
    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D;

static int guPrimTable[5] =
{
    GU_TRIANGLES,       // PDDI_PRIM_TRIANGLES
    GU_TRIANGLE_STRIP,  // PDDI_PRIM_TRISTRIP
    GU_LINES,           // PDDI_PRIM_LINES
    GU_LINE_STRIP,      // PDDI_PRIM_LINESTRIP
    GU_POINTS           // PDDI_PRIM_POINTS
};

static inline unsigned GuCol(pddiColour c)
{
    return (unsigned(c.Alpha()) << 24) | (unsigned(c.Blue()) << 16) |
           (unsigned(c.Green()) << 8) | unsigned(c.Red());
}

static void gulog(const char* s)
{
    FILE* f = pspDiagFopen("ms0:/shar_gu.log", "a");
    if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
}

//=============================================================================
// Vertex stream shared by immediate (BeginPrims) and retained (prim buffer):
// writes into a GuVert array, filling sensible defaults for absent components.
//=============================================================================
class pguVertStream : public pddiPrimBufferStream
{
public:
    GuVert* v;      // current vertex
    GuVert* base;
    unsigned count;

    // Rewind only. The engine fills a retained buffer with a multi-pass COLUMN
    // fill (tPrimGroupLoader::Load): POSITIONLIST, then NORMALLIST, then UVLIST,
    // each a separate pass that re-Lock()s (Reset) and writes one attribute per
    // vertex, advancing with Next(). Reset must therefore NOT clear per-vertex
    // fields or a later pass would wipe an earlier one's data. Defaults for
    // channels absent from the source format are set once at buffer allocation.
    void Reset(GuVert* buf) { base = v = buf; count = 0; }

    void Position(float x, float y, float z) { v->x = x; v->y = y; v->z = z; Next(); }
    void Normal(float x, float y, float z)   { v->nx = x; v->ny = y; v->nz = z; }
    void Colour(pddiColour c, int = 0)       { v->colour = GuCol(c); }
    void TexCoord1(float s, int = 0)         { v->u = s; v->v = 0; }
    void TexCoord2(float s, float t, int=0)  { v->u = s; v->v = t; }
    void TexCoord3(float s, float t, float, int=0) { v->u = s; v->v = t; }
    void TexCoord4(float s, float t, float, float, int=0) { v->u = s; v->v = t; }
    void Specular(pddiColour) {}
    void SkinIndices(unsigned, unsigned, unsigned, unsigned) {}
    void SkinWeights(float, float, float) {}

    void Vertex(rmt::Vector* p, pddiColour c)                    { Colour(c); Position(p->x,p->y,p->z); }
    void Vertex(rmt::Vector* p, rmt::Vector* n)                  { Normal(n->x,n->y,n->z); Position(p->x,p->y,p->z); }
    void Vertex(rmt::Vector* p, rmt::Vector2* uv)                { TexCoord2(uv->x,uv->y); Position(p->x,p->y,p->z); }
    void Vertex(rmt::Vector* p, pddiColour c, rmt::Vector2* uv)  { Colour(c); TexCoord2(uv->x,uv->y); Position(p->x,p->y,p->z); }
    void Vertex(rmt::Vector* p, rmt::Vector* n, rmt::Vector2* uv){ Normal(n->x,n->y,n->z); TexCoord2(uv->x,uv->y); Position(p->x,p->y,p->z); }

    // Advance to the next vertex. Must NOT copy the current vertex forward — the
    // multi-pass column fill writes each attribute independently, so carrying a
    // whole vertex forward would clobber attributes written by other passes
    // (this was zeroing every UV, rendering the textured room black).
    void Next() { v++; count++; }
};

// Immediate-mode stream (pddiPrimStream): Coord/Vertex EMIT a vertex (like
// glVertex3f), Normal/UV/Colour set the pending component first.
class pguImmStream : public pddiPrimStream
{
public:
    GuVert* base;
    GuVert* v;
    unsigned count;

    void Reset(GuVert* buf) { base = v = buf; count = 0; v->u=v->v=0; v->colour=0xffffffff; v->nx=v->ny=0; v->nz=1.0f; }
    void Emit() { GuVert prev = *v; v++; count++; *v = prev; }

    void Coord(float x, float y, float z) { v->x=x; v->y=y; v->z=z; Emit(); }
    void Normal(float x, float y, float z){ v->nx=x; v->ny=y; v->nz=z; }
    void Colour(pddiColour c, int = 0)    { v->colour = GuCol(c); }
    void UV(float u, float vv, int = 0)   { v->u=u; v->v=vv; }
    void Specular(pddiColour) {}
    void Vertex(pddiVector* p, pddiColour c)                     { v->colour=GuCol(c); v->x=p->x; v->y=p->y; v->z=p->z; Emit(); }
    void Vertex(pddiVector* p, pddiVector* n)                    { v->nx=n->x; v->ny=n->y; v->nz=n->z; v->x=p->x; v->y=p->y; v->z=p->z; Emit(); }
    void Vertex(pddiVector* p, pddiVector2* uv)                  { v->u=uv->x; v->v=uv->y; v->x=p->x; v->y=p->y; v->z=p->z; Emit(); }
    void Vertex(pddiVector* p, pddiColour c, pddiVector2* uv)    { v->colour=GuCol(c); v->u=uv->x; v->v=uv->y; v->x=p->x; v->y=p->y; v->z=p->z; Emit(); }
    void Vertex(pddiVector* p, pddiVector* n, pddiVector2* uv)   { v->nx=n->x; v->ny=n->y; v->nz=n->z; v->u=uv->x; v->v=uv->y; v->x=p->x; v->y=p->y; v->z=p->z; Emit(); }
};

static pguImmStream s_immStream;
static pddiPrimType s_immPrimType = PDDI_PRIM_TRIANGLES;

//=============================================================================
// pguContext
//=============================================================================
pguContext::pguContext(pguDevice* dev, pguDisplay* disp)
    : pddiBaseContext((pddiDisplay*)disp, (pddiDevice*)dev)
{
    device = dev;
    display = disp;
    device->AddRef();
    display->AddRef();

    maxTexSize = 512;
    contextID = 0;
    m_inFrame = false;         // no active display list yet -> setters skip the GE
    DefaultState();

    defaultShader = new pguMat(this);
    defaultShader->AddRef();
    gulog("pguContext created");
}

pguContext::~pguContext()
{
    defaultShader->Release();
    display->Release();
    device->Release();
}

void pguContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();
    sceGuStart(GU_DIRECT, g_guDisplayList);
    m_inFrame = true;
    ApplyRenderState();     // re-establish GE state accumulated while out-of-frame
}

void pguContext::EndFrame()
{
    m_inFrame = false;
    pddiBaseContext::EndFrame();
    // Real hardware: the GE reads verts/indices/matrices from RAM by DMA,
    // bypassing the CPU cache. Per-buffer flushes cover the streamed path, but
    // re-enable the 3D scene surfaced a hardware-only hang PPSSPP never shows
    // (no cache). Write back everything the CPU touched this frame before the GE
    // consumes the display list, so it reads live data. Broad net; narrow later.
    sceKernelDcacheWritebackAll();
    sceGuFinish();
    sceGuSync(0, 0);
}

// Re-issue the current render state to the GE at the start of each frame's
// display list (state set outside a frame is only stored, not applied).
void pguContext::ApplyRenderState(void)
{
    sceGuEnable(GU_TEXTURE_2D);

    // Re-apply the engine's tracked depth state. State set outside a frame (in
    // DefaultState/tContext setup) is only stored, so the real z-buffer settings
    // must be re-issued to the GE here at frame start; the engine then refines
    // them per layer in-frame (depth on for the 3D room, off for the 2D UI).
    // Without real depth testing the room was pure painter's-order — walls, the
    // TV and Homer were overwritten by whatever drew after them.
    EnableZBuffer(state.renderState->zEnabled);
    SetZCompare(state.renderState->zCompare);
    SetZWrite(state.renderState->zWrite);

    // Cull left off for now: geometry winding on the GE isn't verified yet, and
    // two-sided rendering avoids dropping walls while we confirm depth is right.
    sceGuDisable(GU_CULL_FACE);

    // Lighting rig for lit materials (characters like Homer). The frontend's own
    // scene lights arrive disabled and out-of-frame on PSP (see shar_light.log),
    // so a lit character would otherwise get only flat ambient — dark and unshaded.
    // Provide a fixed eye-space key directional light plus an ambient fill so
    // characters are shaded and bright, matching the console look. This only
    // affects materials that enable GU_LIGHTING (characters); the unlit room and
    // 2D UI ignore GU lights entirely. Light dir is in eye space because the
    // backend collapses view into the model matrix (GU_VIEW = identity), so lights
    // set here are effectively camera-relative — a stable "key light" on the menu
    // character regardless of the scene's own (absent) lights.
    sceGuAmbient(0xff808080);                          // ~50% ambient fill
    ScePspFVector3 keyDir = { 0.30f, 0.45f, 0.84f };   // eye-space, upper-front
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &keyDir);
    sceGuLightColor(0, GU_DIFFUSE, 0xff909090);        // ~56% white key
    sceGuEnable(GU_LIGHT0);
    sceGuLightMode(GU_SINGLE_COLOR);
}

void pguContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);
    if(!m_inFrame) return;

    int mask = 0;
    if(bufferMask & PDDI_BUFFER_COLOUR)  mask |= GU_COLOR_BUFFER_BIT;
    if(bufferMask & PDDI_BUFFER_DEPTH)   mask |= GU_DEPTH_BUFFER_BIT;
    if(bufferMask & PDDI_BUFFER_STENCIL) mask |= GU_STENCIL_BUFFER_BIT;

    sceGuClearColor(GuCol(state.viewState->clearColour));
    sceGuClearDepth(0);   // reversed depth: far = 0
    sceGuClear(mask | GU_FAST_CLEAR_BIT);
}

void pguContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
    if(!m_inFrame) return;
    if(!rect)
        sceGuScissor(0, 0, display->GetWidth(), display->GetHeight());
    else
        sceGuScissor(rect->left, rect->top, rect->right, rect->bottom);
}

//-----------------------------------------------------------------------------
// Matrices. pddiMatrix / rmt::Matrix is column-major (the GL backend loads it
// straight into glLoadMatrixf), matching ScePspFMatrix4, so we can memcpy.
//-----------------------------------------------------------------------------
void pguContext::LoadHardwareMatrix(pddiMatrixType id)
{
    if(!m_inFrame) return;
    if(id != PDDI_MATRIX_MODELVIEW) return;
    ScePspFMatrix4 m;
    memcpy(&m, state.matrixStack[id]->Top(), sizeof(ScePspFMatrix4));
    // The engine is left-handed; our projection (glFrustum-style) is right-
    // handed, so negate the modelview's output-Z row — same fix the GL backend
    // applies — otherwise geometry lands behind the camera and is clipped away.
    m.x.z = -m.x.z; m.y.z = -m.y.z; m.z.z = -m.z.z; m.w.z = -m.w.z;
    ScePspFMatrix4 ident; gumLoadIdentity(&ident);
    sceGuSetMatrix(GU_VIEW,  &ident);
    sceGuSetMatrix(GU_MODEL, &m);
}

void pguContext::SetupHardwareProjection(void)
{
    if(!m_inFrame) return;
    ScePspFMatrix4 proj;
    float n = state.viewState->camera.nearPlane;
    float f = state.viewState->camera.farPlane;

    if(state.viewState->projectionMode == PDDI_PROJECTION_PERSPECTIVE)
    {
        float halfX = (float)tan(double(state.viewState->camera.fov * 0.5)) * n;
        float halfY = halfX / state.viewState->camera.aspect;
        // glFrustum-equivalent, column-major
        memset(&proj, 0, sizeof(proj));
        proj.x.x = n / halfX;
        proj.y.y = n / halfY;
        proj.z.z = -(f + n) / (f - n);
        proj.z.w = -1.0f;
        proj.w.z = -(2.0f * f * n) / (f - n);
    }
    else if(state.viewState->projectionMode == PDDI_PROJECTION_DEVICE)
    {
        // Screen-pixel 2D (fonts/sprites): glOrtho(0, W, H, 0) — origin top-left.
        float l = 0.0f, r = (float)display->GetWidth();
        float b = (float)display->GetHeight(), t = 0.0f;
        gumLoadIdentity(&proj);
        proj.x.x = 2.0f / (r - l);
        proj.y.y = 2.0f / (t - b);
        proj.z.z = -2.0f / (f - n);
        proj.w.x = -(r + l) / (r - l);
        proj.w.y = -(t + b) / (t - b);
        proj.w.z = -(f + n) / (f - n);
    }
    else
    {
        // orthographic
        float l = -0.5f, r = 0.5f;
        float b = -((1.0f / state.viewState->camera.aspect) / 2.0f);
        float t =  ((1.0f / state.viewState->camera.aspect) / 2.0f);
        gumLoadIdentity(&proj);
        proj.x.x = 2.0f / (r - l);
        proj.y.y = 2.0f / (t - b);
        proj.z.z = -2.0f / (f - n);
        proj.w.x = -(r + l) / (r - l);
        proj.w.y = -(t + b) / (t - b);
        proj.w.z = -(f + n) / (f - n);
    }
    sceGuSetMatrix(GU_PROJECTION, &proj);

    // viewport: map the pddi view window (0..1) to the screen.
    float vw = state.viewState->viewWindow.right - state.viewState->viewWindow.left;
    float vh = state.viewState->viewWindow.bottom - state.viewState->viewWindow.top;
    int W = display->GetWidth(), H = display->GetHeight();
    int px = (int)(state.viewState->viewWindow.left * W);
    int py = (int)(state.viewState->viewWindow.top * H);
    int pw = (int)(vw * W);
    int ph = (int)(vh * H);
    if(pw < 1) pw = W;
    if(ph < 1) ph = H;
    sceGuOffset(2048 - (W / 2), 2048 - (H / 2));
    sceGuViewport(2048 - (W/2) + px + pw/2, 2048 - (H/2) + py + ph/2, pw, ph);
    sceGuScissor(px, py, px + pw, py + ph);
}

//-----------------------------------------------------------------------------
// Retained-mode geometry
//-----------------------------------------------------------------------------
void pguContext::DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer)
{
    if(!m_inFrame) return;
    extern int g_pspDrawBuffer;
    g_pspDrawBuffer++;
    if(!material) material = defaultShader;
    ((pddiBaseShader*)material)->SetMaterial();

    // DIAG: dump the MODELVIEW matrix the GE will apply (GU_TRANSFORM_3D does
    // model*view*proj). Log it for the skinned char (Homer) AND the first few
    // room draws. If Homer's skinned verts are world-space but this modelview
    // carries an extra model transform (non-view rotation/scale/translation the
    // room draws don't have), the GE double-transforms Homer off-screen.
    extern bool g_pspSkinnedDraw;
    {
        static int s_mvSkin = 0, s_mvRoom = 0;
        bool doLog = g_pspSkinnedDraw ? (s_mvSkin < 3) : (s_mvRoom < 3);
        if(doLog)
        {
            const float* mv = (const float*)state.matrixStack[PDDI_MATRIX_MODELVIEW]->Top();
            FILE* f = pspDiagFopen("ms0:/shar_gu.log", "a");
            if(f && mv)
            {
                fprintf(f, "MODELVIEW (%s):\n", g_pspSkinnedDraw ? "SKIN/Homer" : "room");
                for(int r = 0; r < 4; r++)
                    fprintf(f, "   [% .3f % .3f % .3f % .3f]\n",
                            mv[r*4+0], mv[r*4+1], mv[r*4+2], mv[r*4+3]);
                fclose(f);
            }
            if(g_pspSkinnedDraw) s_mvSkin++; else s_mvRoom++;
        }
    }

    // (Debug override removed: disabling GU_DEPTH_TEST here also disabled depth
    // WRITES on the PSP, so the opaque room — drawn after Homer — painted over
    // him. Homer now draws with normal depth state.)
    ((pguPrimBuffer*)buffer)->Display();
}

//-----------------------------------------------------------------------------
// Immediate-mode geometry (fonts / sprites / 2D). Verts go into the display
// list via sceGuGetMemory, then a single sceGuDrawArray on EndPrims.
//-----------------------------------------------------------------------------
pddiPrimStream* pguContext::BeginPrims(pddiShader* material, pddiPrimType primType, unsigned, int vertexCount, unsigned)
{
    static GuVert s_scratch[1024];
    s_immPrimType = primType;
    if(!m_inFrame)
    {
        s_immStream.Reset(s_scratch);   // out-of-frame: swallow writes, no draw
        return &s_immStream;
    }
    extern int g_pspDrawStream;
    g_pspDrawStream++;
    if(!material) material = defaultShader;
    ((pddiBaseShader*)material)->SetMaterial();

    int n = vertexCount > 0 ? vertexCount : 256;
    GuVert* buf = (GuVert*)sceGuGetMemory((n + 1) * sizeof(GuVert));
    s_immStream.Reset(buf);
    return &s_immStream;
}

void pguContext::EndPrims(pddiPrimStream*)
{
    if(m_inFrame && s_immStream.count > 0)
        sceGuDrawArray(guPrimTable[s_immPrimType], GU_VTYPE_BASE, s_immStream.count, 0, s_immStream.base);
}

//-----------------------------------------------------------------------------
// State
//-----------------------------------------------------------------------------
static int guCmpTable[8] =
{
    GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_GREATER, GU_GEQUAL, GU_EQUAL, GU_NOTEQUAL
};

void pguContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);
    if(!m_inFrame) return;
    if(mode == PDDI_CULL_NONE) sceGuDisable(GU_CULL_FACE);
    else { sceGuEnable(GU_CULL_FACE); sceGuFrontFace(mode == PDDI_CULL_INVERTED ? GU_CCW : GU_CW); }
}

void pguContext::SetColourWrite(bool r, bool g, bool b, bool a)
{
    pddiBaseContext::SetColourWrite(r, g, b, a);
    if(!m_inFrame) return;
    unsigned mask = 0;
    if(!r) mask |= 0x000000ff;
    if(!g) mask |= 0x0000ff00;
    if(!b) mask |= 0x00ff0000;
    if(!a) mask |= 0xff000000;
    sceGuPixelMask(mask);
}

void pguContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    if(!m_inFrame) return;
    if(enable) sceGuEnable(GU_DEPTH_TEST);
    else       sceGuDisable(GU_DEPTH_TEST);
}

void pguContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    if(!m_inFrame) return;
    // depth is reversed on PSP, so flip the sense of the comparison
    static const int rev[8] = { GU_NEVER, GU_ALWAYS, GU_GREATER, GU_GEQUAL, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_NOTEQUAL };
    sceGuDepthFunc(rev[compareMode]);
}

void pguContext::SetZWrite(bool w)
{
    pddiBaseContext::SetZWrite(w);
    if(!m_inFrame) return;
    sceGuDepthMask(w ? 0 : 0xffff);
}

void pguContext::SetZBias(float) {}
void pguContext::SetZRange(float, float) {}

void pguContext::EnableStencilBuffer(bool) {}
void pguContext::SetStencilCompare(pddiCompareMode) {}
void pguContext::SetStencilRef(int) {}
void pguContext::SetStencilMask(unsigned) {}
void pguContext::SetStencilWriteMask(unsigned) {}
void pguContext::SetStencilOp(pddiStencilOp, pddiStencilOp, pddiStencilOp) {}

void pguContext::SetFillMode(pddiFillMode) {}

void pguContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
    if(!m_inFrame) return;
    if(enable) sceGuEnable(GU_FOG); else sceGuDisable(GU_FOG);
}

void pguContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour, start, end);
    if(!m_inFrame) return;
    sceGuFog(start, end, GuCol(colour));
}

int pguContext::GetMaxTextureDimension(void) { return maxTexSize; }

pddiExtension* pguContext::GetExtension(unsigned extID)
{
    // The base context provides shared extensions (e.g. mem registration, which
    // tContext's ctor calls Register() on unconditionally — returning NULL here
    // crashed context creation).
    return pddiBaseContext::GetExtension(extID);
}
bool pguContext::VerifyExtension(unsigned extID)
{
    return pddiBaseContext::VerifyExtension(extID);
}

int pguContext::GetMaxLights() { return 4; }   // PSP GE supports 4 hardware lights

void pguContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
    { static int n=0; if(n<8){ FILE* f=pspDiagFopen("ms0:/shar_light.log","a");
      if(f){ fprintf(f,"SetAmbient inFrame=%d col=%08x\n", (int)m_inFrame, GuCol(col)); fclose(f);} n++; } }
    if(!m_inFrame) return;
    sceGuAmbient(GuCol(col));
}

void pguContext::SetupHardwareLight(int i)
{
    { static int n=0; if(n<24){ pddiLight& LL = state.lightingState->light[i];
      FILE* f=pspDiagFopen("ms0:/shar_light.log","a");
      if(f){ fprintf(f,"Light %d inFrame=%d enabled=%d type=%d col=%08x dir=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f)\n",
              i, (int)m_inFrame, (int)LL.enabled, (int)LL.type, GuCol(LL.colour),
              LL.worldDirection.x, LL.worldDirection.y, LL.worldDirection.z,
              LL.worldPosition.x, LL.worldPosition.y, LL.worldPosition.z); fclose(f);} n++; } }
    if(!m_inFrame) return;
    pddiLight& L = state.lightingState->light[i];
    if(!L.enabled) { sceGuDisable(GU_LIGHT0 + i); return; }

    ScePspFVector3 pos = { L.worldPosition.x, L.worldPosition.y, L.worldPosition.z };
    ScePspFVector3 dir = { L.worldDirection.x, L.worldDirection.y, L.worldDirection.z };
    unsigned col = GuCol(L.colour);

    if(L.type == PDDI_LIGHT_DIRECTIONAL)
        sceGuLight(i, GU_DIRECTIONAL, GU_DIFFUSE, &dir);
    else
        sceGuLight(i, GU_POINTLIGHT, GU_DIFFUSE, &pos);
    sceGuLightColor(i, GU_DIFFUSE, col);
    sceGuEnable(GU_LIGHT0 + i);
}

void pguContext::SetVertexArray(unsigned, void*, int) {}
void  pguContext::BeginTiming(void) {}
float pguContext::EndTiming(void) { return 0.0f; }

//=============================================================================
// pguPrimBuffer
//=============================================================================
pguPrimBuffer::pguPrimBuffer(pguContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex)
    : context(c), primType(type), vertexType(vertexFormat),
      verts(NULL), guVType(GU_VTYPE_BASE), stride(sizeof(GuVert)),
      allocated(nVertex), total(0), indices(NULL), indexCount(nIndex)
{
    // +1 guard slot (a stream Next() past the last written vertex is harmless).
    verts = radMemoryAllocAligned(radMemoryGetCurrentAllocator(), (size_t)(nVertex + 1) * sizeof(GuVert), 16);

    // Default-init every vertex once. The multi-pass column fill only writes the
    // channels the source geometry actually has, so give the others sane values
    // (white opaque colour, +Z normal, zero UV) — the GE always reads all of
    // texture/colour/normal/position from GU_VTYPE_BASE regardless of source.
    if(verts)
    {
        GuVert* gv = (GuVert*)verts;
        for(int i = 0; i <= nVertex; i++)
        {
            gv[i].u = gv[i].v = 0.0f;
            gv[i].colour = 0xffffffff;
            gv[i].nx = gv[i].ny = 0.0f; gv[i].nz = 1.0f;
            gv[i].x = gv[i].y = gv[i].z = 0.0f;
        }
    }
    if(nIndex > 0)
        indices = (unsigned short*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), (size_t)nIndex * sizeof(unsigned short), 16);
    stream = new pguVertStream();
}

pguPrimBuffer::~pguPrimBuffer()
{
    if(verts) radMemoryFreeAligned(verts);
    if(indices) radMemoryFreeAligned(indices);
    delete (pguVertStream*)stream;
}

pddiPrimBufferStream* pguPrimBuffer::Lock()
{
    ((pguVertStream*)stream)->Reset((GuVert*)verts);
    return (pddiPrimBufferStream*)stream;
}

void pguPrimBuffer::Unlock(pddiPrimBufferStream*)
{
    total = ((pguVertStream*)stream)->count;

    // Flush the just-written vertices from the CPU data cache to RAM. The GE
    // reads vertex data by DMA straight from physical RAM, bypassing the cache,
    // so without this it sees stale bytes. Static buffers (the room VBO) are
    // filled once and happened to work only because their cache lines evicted
    // naturally over frames; CPU-skinned characters (Homer) refill this buffer
    // EVERY frame, so their fresh verts sat unflushed in cache and the GE read
    // the constructor's zero-initialised positions -> whole mesh collapsed to
    // the origin -> invisible. Flush here (once per fill) fixes both.
    if(verts && total > 0)
        sceKernelDcacheWritebackRange(verts, total * sizeof(GuVert));
}

unsigned char* pguPrimBuffer::LockIndexBuffer() { return (unsigned char*)indices; }
void pguPrimBuffer::UnlockIndexBuffer(int count) { indexCount = count; }

void pguPrimBuffer::SetIndices(unsigned short* i, int count)
{
    indexCount = count;
    if(!indices)
        indices = (unsigned short*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), (size_t)count * sizeof(unsigned short), 16);
    memcpy(indices, i, count * sizeof(unsigned short));
    // Same reason as Unlock(): the GE reads the index list by DMA from RAM.
    if(indices && count > 0)
        sceKernelDcacheWritebackRange(indices, count * sizeof(unsigned short));
}

void pguPrimBuffer::Display(void)
{
    // DIAG (first 12 retained draws = room meshes): dump vertex colour/uv so we
    // can tell whether the dark room is texture*dark-vertex-colour vs black texture.
    static int s_dbg = 0;
    if(s_dbg < 12)
    {
        GuVert* v = (GuVert*)verts;
        FILE* f = pspDiagFopen("ms0:/shar_gu.log", "a");
        if(f && v && total > 0)
        {
            unsigned mid = total / 2, last = total - 1;
            fprintf(f, "prim %d: total=%u vfmt=%08x uv[0]=(%.3f,%.3f) uv[1]=(%.3f,%.3f) uv[mid]=(%.3f,%.3f) uv[last]=(%.3f,%.3f)\n",
                    s_dbg, total, vertexType, v[0].u, v[0].v,
                    total > 1 ? v[1].u : 0.f, total > 1 ? v[1].v : 0.f,
                    v[mid].u, v[mid].v, v[last].u, v[last].v);
            fclose(f);
        }
        s_dbg++;
    }

    // DIAG: dump the ACTUAL draw params for the CPU-skinned character draws
    // (Homer). Positions were proven on-screen upstream, so if these submit a
    // sane primType / index count / triangle, the geometry reaches the GE and the
    // fault is render state; if primType/index/positions are wrong here, the skin
    // fill or buffer setup is the culprit.
    extern bool g_pspSkinnedDraw;
    if(g_pspSkinnedDraw)
    {
        static int s_skdraw = 0;
        if(s_skdraw < 12)
        {
            GuVert* v = (GuVert*)verts;
            FILE* f = pspDiagFopen("ms0:/shar_gu.log", "a");
            if(f && v)
            {
                unsigned i0 = (indexCount > 0 && indices) ? indices[0] : 0;
                unsigned i1 = (indexCount > 1 && indices) ? indices[1] : 1;
                unsigned i2 = (indexCount > 2 && indices) ? indices[2] : 2;
                fprintf(f, "SKINDRAW %d: primType=%d guVType=%08x indexed=%d indexCount=%d total=%u | "
                           "tri v[%u]=(%.2f,%.2f,%.2f) v[%u]=(%.2f,%.2f,%.2f) v[%u]=(%.2f,%.2f,%.2f) col0=%08x\n",
                        s_skdraw, primType, guVType, (indexCount > 0 && indices) ? 1 : 0, indexCount, total,
                        i0, v[i0].x, v[i0].y, v[i0].z,
                        i1, v[i1].x, v[i1].y, v[i1].z,
                        i2, v[i2].x, v[i2].y, v[i2].z, v[i0].colour);
                fclose(f);
            }
            s_skdraw++;
        }
    }

    // DIAG: for the skinned character draws only, force the simplest possible
    // render state (opaque, untextured -> white vertex colour, no alpha kill, no
    // lighting, depth off) so a rasterised Homer CANNOT be hidden by material or
    // blend state. The room is skipped this build, so depth-off is safe. If Homer
    // shows white now, the fault was material/blend; if still nothing, the GE is
    // not rasterising the skinned geometry at all.
    if(indexCount > 0 && indices)
        sceGuDrawArray(guPrimTable[primType], guVType | GU_INDEX_16BIT, indexCount, indices, verts);
    else
        sceGuDrawArray(guPrimTable[primType], guVType, total, 0, verts);
}
