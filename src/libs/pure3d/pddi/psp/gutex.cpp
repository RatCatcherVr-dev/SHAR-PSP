//=============================================================================
// Native PSP sceGU pddi backend — texture implementation.
//=============================================================================
#include <pddi/psp/gutex.hpp>
#include <pddi/psp/gucon.hpp>

#include <pspgu.h>
#include <psputils.h>   // sceKernelDcacheWritebackRange
#include <radmemory.hpp>
#include <string.h>
#include <stdio.h>

static int fastlog2(int x)
{
    int r = 0;
    while(x > 1) { x >>= 1; r++; }
    return r;
}

pguTexture::pguTexture(pguContext* c)
    : context(c), log2X(0), log2Y(0), xSize(0), ySize(0), depth(32),
      type(PDDI_TEXTYPE_RGB), nMipMap(0), priority(0), bits(NULL),
      m_nPaletteEntries(0), m_clutBuilt(false)
{
    memset(m_palette, 0, sizeof(m_palette));
}

pguTexture::~pguTexture()
{
    if(bits)
    {
        for(int i = 0; i <= nMipMap; i++)
            if(bits[i]) radMemoryFreeAligned(bits[i]);
        delete[] bits;
    }
}

bool pguTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip,
                        pddiTextureType t, pddiTextureUsageHint)
{
    xSize = x;
    ySize = y;
    log2X = fastlog2(x);
    log2Y = fastlog2(y);
    type = t;
    nMipMap = nMip;

    // Paletted content stays 8-bit (native CLUT); everything else is RGBA.
    if(type == PDDI_TEXTYPE_PALETTIZED)
        depth = 8;
    else
        depth = 32;

    bits = new char*[nMipMap + 1];
    for(int i = 0; i <= nMipMap; i++)
    {
        int w = xSize >> i; if(w < 1) w = 1;
        int h = ySize >> i; if(h < 1) h = 1;
        size_t sz = (size_t)(w * h * depth) / 8;
        bits[i] = (char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), sz, 16);
    }
    return true;
}

pddiPixelFormat pguTexture::GetPixelFormat()
{
    if(type == PDDI_TEXTYPE_PALETTIZED) return PDDI_PIXEL_PAL8;
    return PDDI_PIXEL_ARGB8888;
}

int pguTexture::GetWidth()      { return xSize; }
int pguTexture::GetHeight()     { return ySize; }
int pguTexture::GetDepth()      { return depth; }
int pguTexture::GetNumMipMaps() { return nMipMap; }
int pguTexture::GetAlphaDepth() { return (type == PDDI_TEXTYPE_RGB) ? 0 : 8; }

int pguTexture::GetNumPaletteEntries(void) { return m_nPaletteEntries; }

void pguTexture::SetPalette(int nEntries, pddiColour* palette)
{
    m_nPaletteEntries = nEntries;
    for(int i = 0; i < nEntries && i < 256; i++)
        m_palette[i] = palette[i];
    m_clutBuilt = false;
}

int pguTexture::GetPalette(pddiColour* palette)
{
    for(int i = 0; i < m_nPaletteEntries; i++)
        palette[i] = m_palette[i];
    return m_nPaletteEntries;
}

pddiLockInfo* pguTexture::Lock(int mipMap, pddiRect*)
{
    lock.width  = xSize >> mipMap; if(lock.width < 1) lock.width = 1;
    lock.height = ySize >> mipMap; if(lock.height < 1) lock.height = 1;
    lock.depth  = depth;
    if(type == PDDI_TEXTYPE_PALETTIZED)
    {
        lock.format = PDDI_PIXEL_PAL8;
        lock.native = false;
        int rowBytes = (lock.width * lock.depth) / 8;
        lock.pitch = rowBytes;
        lock.bits = bits[mipMap];
    }
    else
    {
        lock.format = PDDI_PIXEL_ARGB8888;
        lock.native = false;   // engine writes RGBA byte order (via MakeColour)
        lock.pitch = lock.width * 4;
        lock.bits = bits[mipMap];
        // Channel layout for tImageConverter::MakeColour(): pddiColour is
        // 0xAARRGGBB; the GE's GU_PSM_8888 wants byte order R,G,B,A (word
        // 0xAABBGGRR). Without these, the shifts default to 0 and MakeColour
        // returns 0xAARRGGBB unchanged -> the GE reads it as RGBA -> red/blue
        // swapped (white/grey UI hides it; the colored TV frame + arrows don't).
        // Mirrors the GL backend's non-native lock (gltex.cpp).
        lock.rgbaRShift[0] = 16; lock.rgbaLShift[0] = 0;   // R -> byte 0
        lock.rgbaRShift[1] = 0;  lock.rgbaLShift[1] = 0;   // G -> byte 1
        lock.rgbaRShift[2] = 0;  lock.rgbaLShift[2] = 16;  // B -> byte 2
        lock.rgbaRShift[3] = 0;  lock.rgbaLShift[3] = 0;   // A -> byte 3
        lock.rgbaMask[0] = 0x00ff0000; lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x000000ff; lock.rgbaMask[3] = 0xff000000;
    }
    return &lock;
}

void pguTexture::Unlock(int mipMap)
{
    // Texture V-origin fix. The engine authors UVs for GL's *bottom-left* texel
    // origin (tSprite::BuildPoly emits v = 1.0f - row/h, image data uploaded
    // row-0-first) and the pspGL path worked because it emulated GL. The native
    // GE uses a *top-left* texel origin (V=0 == row 0 == top), the opposite, so
    // straight upload sampled every texture upside-down. Reverse the row order
    // once here so the GE's V matches GL and all engine UVs render upright.
    // Frontend textures are static (locked/unlocked once); if a texture is ever
    // re-locked this would double-flip, so revisit for dynamic textures.
    if(!bits || mipMap < 0 || mipMap > nMipMap || !bits[mipMap]) return;

    int w = xSize >> mipMap; if(w < 1) w = 1;
    int h = ySize >> mipMap; if(h < 1) h = 1;
    int rowBytes = (w * depth) / 8;
    char* base = bits[mipMap];
    if(rowBytes <= 0) return;

    // Reverse the row order (flip) so the GE's top-left V origin matches the
    // engine's GL-authored bottom-left UVs. Skipped for 1-row mips and for rows
    // too wide for the scratch buffer — but the dcache flush below still runs.
    static char tmp[512 * 4];   // one row scratch (max 512px * 32bpp)
    if(h >= 2 && rowBytes <= (int)sizeof(tmp))
    {
        for(int y = 0; y < h / 2; y++)
        {
            char* top = base + (size_t)y * rowBytes;
            char* bot = base + (size_t)(h - 1 - y) * rowBytes;
            memcpy(tmp, top, rowBytes);
            memcpy(top, bot, rowBytes);
            memcpy(bot, tmp, rowBytes);
        }
    }

    // Flush the just-written texel data from the CPU data cache to RAM. The GE
    // samples texture data by DMA straight from physical RAM, bypassing the
    // cache, so without this it reads stale bytes on real hardware (PPSSPP has
    // no cache, so it "worked" there). Same reason the prim buffer flushes its
    // verts in gucon.cpp. Must run for EVERY locked mip (incl. 1-row / wide
    // surfaces that skip the flip above), or those sample as garbage on metal.
    sceKernelDcacheWritebackRange(base, (size_t)h * rowBytes);
}

void pguTexture::Prefetch(void) {}
void pguTexture::Discard(void) {}
void pguTexture::SetPriority(int p) { priority = p; }
int  pguTexture::GetPriority() { return priority; }

void pguTexture::SetGUState(void)
{
    int bufw = xSize;   // pow2 width == buffer stride for the GE

    if(type == PDDI_TEXTYPE_PALETTIZED)
    {
        if(!m_clutBuilt)
        {
            // pddiColour (0xAARRGGBB) -> GE CLUT entry (0xAABBGGRR)
            for(int i = 0; i < 256; i++)
            {
                pddiColour c = m_palette[i];
                m_clut[i] = (unsigned(c.Alpha()) << 24) | (unsigned(c.Blue()) << 16) |
                            (unsigned(c.Green()) << 8) | unsigned(c.Red());
            }
            // The GE loads the CLUT by DMA from RAM — flush it from the cache
            // (see the texel flush in Unlock; same hardware coherency rule).
            sceKernelDcacheWritebackRange(m_clut, sizeof(m_clut));
            m_clutBuilt = true;
        }
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(256 / 8, m_clut);        // 256 entries, 8 per block
        sceGuTexMode(GU_PSM_T8, 0, 0, 0);      // 8-bit indexed, no mips, unswizzled
        sceGuTexImage(0, xSize, ySize, bufw, bits[0]);
    }
    else
    {
        sceGuTexMode(GU_PSM_8888, 0, 0, 0);    // 32-bit RGBA, no mips, unswizzled
        sceGuTexImage(0, xSize, ySize, bufw, bits[0]);
    }
}
