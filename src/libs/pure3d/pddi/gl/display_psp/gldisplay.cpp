//=============================================================================
// PSP display backend for the pddi OpenGL renderer.
//
// Implements the pglDisplay interface (pddi/gl/gldisplay.hpp) on top of pspGL's
// EGL entry points instead of SDL. pspGL is a GL1.x -> sceGU bridge, so the rest
// of the pddi/gl backend (device/context/matrix/texture) is reused unchanged.
//=============================================================================

#include <pddi/gl/gl.hpp>
#include <pddi/gl/glcon.hpp>
#include <pddi/gl/gldisplay.hpp>
#include <pddi/base/debug.hpp>

#include <GLES/egl.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// The PSP has a single framebuffer / GL context, so the EGL handles live here
// rather than as members (which would force SDL types into the shared header).
static EGLDisplay s_eglDisplay = 0;
static EGLContext s_eglContext = 0;
static EGLSurface s_eglSurface = 0;

// Lightweight diagnostic log to the Memory Stick (no debugger needed).
static void psplog(const char* fmt, ...)
{
    FILE* f = fopen("ms0:/shar_gl.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

pglDisplay::pglDisplay(pddiDisplayInfo* info)
{
    displayInfo = info;
    mode        = PDDI_DISPLAY_FULLSCREEN;
    winWidth    = 480;
    winHeight   = 272;
    winBitDepth = 32;

    context = NULL;
    win     = NULL;
    hRC     = NULL;
    prevRC  = NULL;

    extBGRA = false;
    gammaR = gammaG = gammaB = 1.0f;
    reset = true;
    beginTime = 0.0f;
}

pglDisplay::~pglDisplay()
{
    if (s_eglDisplay)
    {
        eglMakeCurrent(s_eglDisplay, 0, 0, 0);
        if (s_eglContext) eglDestroyContext(s_eglDisplay, s_eglContext);
        if (s_eglSurface) eglDestroySurface(s_eglDisplay, s_eglSurface);
        eglTerminate(s_eglDisplay);
        s_eglDisplay = 0;
        s_eglContext = 0;
        s_eglSurface = 0;
    }
}

bool pglDisplay::InitDisplay(const pddiDisplayInit* init)
{
    return InitDisplay(init->xsize, init->ysize, init->bpp);
}

bool pglDisplay::InitDisplay(int x, int y, int bpp)
{
    reset       = true;
    winWidth    = x;
    winHeight   = y;
    winBitDepth = bpp;

    psplog("InitDisplay %dx%d bpp=%d\n", x, y, bpp);

    s_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    psplog("  eglGetDisplay = %p (err %x)\n", (void*)s_eglDisplay, (unsigned)eglGetError());
    if (!s_eglDisplay)
        return false;
    EGLBoolean initOk = eglInitialize(s_eglDisplay, NULL, NULL);
    psplog("  eglInitialize = %d (err %x)\n", (int)initOk, (unsigned)eglGetError());

    EGLint attr[] = {
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint    nCfg = 0;
    EGLBoolean chose = eglChooseConfig(s_eglDisplay, attr, &cfg, 1, &nCfg);
    psplog("  eglChooseConfig = %d nCfg=%d (err %x)\n", (int)chose, (int)nCfg, (unsigned)eglGetError());
    if (!chose || nCfg < 1)
        return false;

    s_eglContext = eglCreateContext(s_eglDisplay, cfg, EGL_NO_CONTEXT, NULL);
    psplog("  eglCreateContext = %p (err %x)\n", (void*)s_eglContext, (unsigned)eglGetError());
    s_eglSurface = eglCreateWindowSurface(s_eglDisplay, cfg, 0, NULL);
    psplog("  eglCreateWindowSurface = %p (err %x)\n", (void*)s_eglSurface, (unsigned)eglGetError());
    if (!s_eglContext || !s_eglSurface)
        return false;

    EGLBoolean cur = eglMakeCurrent(s_eglDisplay, s_eglSurface, s_eglSurface, s_eglContext);
    psplog("  eglMakeCurrent = %d (err %x)\n", (int)cur, (unsigned)eglGetError());
    psplog("  GL_VENDOR=%s\n  GL_RENDERER=%s\n  GL_VERSION=%s\n",
           (const char*)glGetString(GL_VENDOR),
           (const char*)glGetString(GL_RENDERER),
           (const char*)glGetString(GL_VERSION));

    extBGRA = CheckExtension("GL_EXT_bgra");
    return true;
}

pddiDisplayInfo* pglDisplay::GetDisplayInfo(void)
{
    return displayInfo;
}

bool pglDisplay::CheckExtension(const char* extName)
{
    const char* p = (const char*)glGetString(GL_EXTENSIONS);
    if (!p || !extName)
        return false;

    size_t len = strlen(extName);
    const char* end = p + strlen(p);
    while (p < end)
    {
        size_t n = strcspn(p, " ");
        if (len == n && strncmp(extName, p, n) == 0)
            return true;
        p += (n + 1);
    }
    return false;
}

unsigned pglDisplay::GetFreeTextureMem()      { return unsigned(-1); }
unsigned pglDisplay::GetBufferMask()          { return ~0U; }
int      pglDisplay::GetHeight()              { return winHeight; }
int      pglDisplay::GetWidth()               { return winWidth; }
int      pglDisplay::GetDepth()               { return winBitDepth; }
pddiDisplayMode pglDisplay::GetDisplayMode()  { return mode; }
int      pglDisplay::GetNumColourBuffer()     { return 2; }

void pglDisplay::GetGamma(float* r, float* g, float* b) { *r = gammaR; *g = gammaG; *b = gammaB; }
void pglDisplay::SetGamma(float r, float g, float b)    { gammaR = r; gammaG = g; gammaB = b; }

void pglDisplay::SwapBuffers(void)
{
    static int s_swaps = 0;
    if (s_eglDisplay && s_eglSurface)
    {
        eglSwapBuffers(s_eglDisplay, s_eglSurface);
        if (s_swaps < 3)
            psplog("  SwapBuffers #%d (err %x)\n", s_swaps, (unsigned)eglGetError());
        s_swaps++;
    }
    else if (s_swaps < 3)
    {
        psplog("  SwapBuffers: no display/surface!\n");
        s_swaps++;
    }
    reset = false;
}

unsigned pglDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    // Not implemented on PSP.
    return 0;
}

// Window messaging is a desktop (SDL) concept; no-ops on PSP.
long pglDisplay::ProcessWindowMessage(SDL_Window* wnd, const SDL_WindowEvent* event) { return 0; }
void pglDisplay::SetWindow(SDL_Window* wnd) {}

void  pglDisplay::BeginTiming() {}
float pglDisplay::EndTiming()   { return 0.0f; }

unsigned pglDisplay::FillDisplayModes(int, pddiModeInfo* displayModes)
{
    if (displayModes)
    {
        displayModes[0].width  = 480;
        displayModes[0].height = 272;
        displayModes[0].bpp    = 32;
    }
    return 1;
}

void pglDisplay::BeginContext(void)
{
    if (s_eglDisplay)
        eglMakeCurrent(s_eglDisplay, s_eglSurface, s_eglSurface, s_eglContext);
}

void pglDisplay::EndContext(void)
{
}
