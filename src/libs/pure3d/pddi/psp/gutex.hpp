//=============================================================================
// Native PSP sceGU pddi backend — texture. Mirrors pddi/gl/gltex.
// Paletted textures stay 8-bit and use the GE's native CLUT (no RGBA expand).
//=============================================================================
#ifndef _GUTEX_HPP_
#define _GUTEX_HPP_

#include <pddi/pddi.hpp>
#include <pddi/pdditype.hpp>
class pguContext;

class pguTexture : public pddiTexture
{
public:
    pguTexture(pguContext*);
    ~pguTexture();

    bool Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip,
                pddiTextureType type = PDDI_TEXTYPE_RGB,
                pddiTextureUsageHint usageHint = PDDI_USAGE_STATIC);

    pddiPixelFormat GetPixelFormat();
    int GetWidth();
    int GetHeight();
    int GetDepth();
    int GetNumMipMaps();
    int GetAlphaDepth();

    int GetNumPaletteEntries(void);
    void SetPalette(int nEntries, pddiColour* palette);
    int GetPalette(pddiColour* palette);

    pddiLockInfo* Lock(int mipLevel, pddiRect* rect = 0);
    void Unlock(int mipLevel);

    void Prefetch(void);
    void Discard(void);
    void SetPriority(int priority);
    int GetPriority();

    // sceGU-specific: bind this texture (+ CLUT) into the current GE state.
    void SetGUState(void);

protected:
    pguContext* context;

    int log2X, log2Y;
    int xSize, ySize;
    int depth;              // bits per texel in system memory (8 = paletted, 32 = RGBA)
    pddiTextureType type;
    int nMipMap;
    int priority;

    pddiLockInfo lock;
    char** bits;            // one buffer per mip (16-byte aligned for the GE)

    pddiColour m_palette[256];
    int        m_nPaletteEntries;
    unsigned   m_clut[256];  // ABGR CLUT built for the GE (lazy)
    bool       m_clutBuilt;
};

#endif
