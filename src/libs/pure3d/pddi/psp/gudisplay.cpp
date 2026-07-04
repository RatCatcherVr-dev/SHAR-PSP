//=============================================================================
// Native PSP sceGU pddi backend — display implementation.
//=============================================================================
#include <pddi/psp/gudisplay.hpp>
#include <pddi/pdditype.hpp>

#include <pspge.h>
#include <pspdisplay.h>
#include <pspgu.h>

#include <stdio.h>
#include <string.h>

// PSP screen geometry. Buffer stride must be a multiple of 64 pixels; 512 for
// a 480-wide screen. 32-bit colour (GU_PSM_8888) framebuffers.
#define PSP_BUF_WIDTH  512
#define PSP_SCR_WIDTH  480
#define PSP_SCR_HEIGHT 272

// Display-list buffer lives in gucon.cpp (the context drives sceGuStart/Finish).
extern unsigned int __attribute__((aligned(16))) g_guDisplayList[];

// VRAM (edram) buffer offsets, in bytes from the GE edram base. 32-bit buffers:
//   draw  @ 0            (512*272*4 = 0x88000)
//   disp  @ 0x88000
//   depth @ 0x110000     (16-bit depth: 512*272*2 = 0x44000)
static void* s_drawBuf = (void*)0;
static void* s_dispBuf = (void*)0x88000;
static void* s_depthBuf = (void*)0x110000;

static void gulog(const char* s)
{
    FILE* f = fopen("ms0:/shar_gu.log", "a");
    if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
}

pguDisplay::pguDisplay()
    : winWidth(PSP_SCR_WIDTH), winHeight(PSP_SCR_HEIGHT), winBitDepth(32),
      reset(true), displayInfo(NULL), gammaR(1.0f), gammaG(1.0f), gammaB(1.0f)
{
    mode = PDDI_DISPLAY_WINDOW;
}

pguDisplay::~pguDisplay()
{
    sceGuTerm();
}

bool pguDisplay::InitDisplay(const pddiDisplayInit* init)
{
    return InitDisplay(init->xsize, init->ysize, init->bpp);
}

bool pguDisplay::InitDisplay(int x, int y, int bpp)
{
    reset       = true;
    winWidth    = PSP_SCR_WIDTH;
    winHeight   = PSP_SCR_HEIGHT;
    winBitDepth = 32;

    gulog("pguDisplay::InitDisplay");

    sceGuInit();

    sceGuStart(GU_DIRECT, g_guDisplayList);
    sceGuDrawBuffer(GU_PSM_8888, s_drawBuf, PSP_BUF_WIDTH);
    sceGuDispBuffer(PSP_SCR_WIDTH, PSP_SCR_HEIGHT, s_dispBuf, PSP_BUF_WIDTH);
    sceGuDepthBuffer(s_depthBuf, PSP_BUF_WIDTH);

    // The GE renders in a 4096x4096 virtual space; centre the 480x272 window.
    sceGuOffset(2048 - (PSP_SCR_WIDTH / 2), 2048 - (PSP_SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, PSP_SCR_WIDTH, PSP_SCR_HEIGHT);
    // PSP depth is reversed (near=65535, far=0) and compared with GEQUAL.
    sceGuDepthRange(65535, 0);

    sceGuScissor(0, 0, PSP_SCR_WIDTH, PSP_SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    gulog("pguDisplay::InitDisplay done");
    return true;
}

long pguDisplay::ProcessWindowMessage(SDL_Window*, const SDL_WindowEvent*) { return 0; }
void pguDisplay::SetWindow(SDL_Window*) {}
pddiDisplayInfo* pguDisplay::GetDisplayInfo(void) { return displayInfo; }

int pguDisplay::GetHeight()               { return winHeight; }
int pguDisplay::GetWidth()                { return winWidth; }
int pguDisplay::GetDepth()                { return winBitDepth; }
pddiDisplayMode pguDisplay::GetDisplayMode() { return mode; }
int pguDisplay::GetNumColourBuffer()      { return 2; }
unsigned pguDisplay::GetBufferMask()      { return ~0U; }
unsigned pguDisplay::GetFreeTextureMem()  { return unsigned(-1); }

void pguDisplay::SwapBuffers(void)
{
    sceGuSwapBuffers();
    reset = false;
}

unsigned pguDisplay::Screenshot(pddiColour*, int) { return 0; }

void pguDisplay::GetGamma(float* r, float* g, float* b) { *r = gammaR; *g = gammaG; *b = gammaB; }
void pguDisplay::SetGamma(float r, float g, float b)    { gammaR = r; gammaG = g; gammaB = b; }
void  pguDisplay::BeginTiming() {}
float pguDisplay::EndTiming()   { return 0.0f; }
