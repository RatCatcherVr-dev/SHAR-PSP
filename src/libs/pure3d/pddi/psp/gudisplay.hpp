//=============================================================================
// Native PSP sceGU pddi backend — display.
// Owns sceGuInit + the frame/depth buffers and the vblank swap.
//=============================================================================
#ifndef _GUDISPLAY_HPP_
#define _GUDISPLAY_HPP_

#include <pddi/pddi.hpp>

class pguDisplay : public pddiDisplay
{
public:
    pguDisplay();
    ~pguDisplay();

    // pddiDisplay (PC-style extras are stubbed on PSP)
    long ProcessWindowMessage(SDL_Window* win, const SDL_WindowEvent* event);
    void SetWindow(SDL_Window* win);
    pddiDisplayInfo* GetDisplayInfo(void);

    bool InitDisplay(int x, int y, int bpp);
    bool InitDisplay(const pddiDisplayInit* initData);

    int GetHeight();
    int GetWidth();
    int GetDepth();
    pddiDisplayMode GetDisplayMode();
    int GetNumColourBuffer();
    unsigned GetBufferMask();
    unsigned GetFreeTextureMem();

    void SwapBuffers(void);
    unsigned Screenshot(pddiColour* buffer, int nBytes);

    // used by the context to reset per-frame render state after a display reset
    bool HasReset(void) { return reset; }

    void  GetGamma(float* r, float* g, float* b);
    void  SetGamma(float r, float g, float b);
    void  BeginTiming();
    float EndTiming();

    int winWidth;
    int winHeight;
    int winBitDepth;
    bool reset;

protected:
    pddiDisplayMode mode;
    pddiDisplayInfo* displayInfo;
    float gammaR, gammaG, gammaB;
};

#endif
