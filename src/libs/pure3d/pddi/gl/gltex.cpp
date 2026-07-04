//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gl/gl.hpp>
#include <pddi/gl/gldisplay.hpp>
#include <pddi/gl/gltex.hpp>
#include <pddi/gl/glcon.hpp>

#include <math.h>
#include <stdio.h>
#include <pddi/base/debug.hpp>
#include <radmemory.hpp>

#include <microprofile.h>

// bruh
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0 // TODO(3UR): we need these still cant use the new stuff such as GL_COMPRESSED_RGBA_BPTC_UNORM in SetGLState because idk and I know nothing about graphics but having the new ones in PickPixelFormat makes tge lighting sooo much better so... if a graphics pro wants to actually clean this feel free to
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3
#define GL_BGRA_EXT                       0x80E1 // TODO(3UR): why cant we use GL_BGRA

#if defined(RAD_PSP)
// pspGL lacks sized / sRGB / BPTC internal formats. Map them to the base and
// S3TC formats the PSP hardware actually supports (sRGB collapses to linear;
// DXT is native on PSP). Matches the desktop path's DXT1==DXT3 simplification.
#define GL_SRGB8                            GL_RGB
#define GL_SRGB8_ALPHA8                     GL_RGBA
#define GL_RGB8                             GL_RGB
#define GL_R8                               GL_LUMINANCE
#define GL_RG8                              GL_LUMINANCE_ALPHA
#define GL_COMPRESSED_RGBA_BPTC_UNORM       GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#endif

static inline GLenum PickPixelFormat(pddiPixelFormat format)
{
    switch (format)
    {
    case PDDI_PIXEL_RGB555:
    case PDDI_PIXEL_RGB565: return GL_RGB5;
    case PDDI_PIXEL_ARGB1555: return GL_RGB5_A1;
    case PDDI_PIXEL_ARGB4444: return GL_RGBA4;
    case PDDI_PIXEL_RGB888: return GL_SRGB8;
    case PDDI_PIXEL_ARGB8888: return GL_SRGB8_ALPHA8;
    case PDDI_PIXEL_PAL8: return GL_RGB8;
    case PDDI_PIXEL_PAL4: return GL_RGB8;
    case PDDI_PIXEL_LUM8: return GL_R8;
    case PDDI_PIXEL_DUDV88: return GL_RG8;
    case PDDI_PIXEL_DXT1: return GL_COMPRESSED_RGBA_BPTC_UNORM;
    case PDDI_PIXEL_DXT3: return GL_COMPRESSED_RGBA_BPTC_UNORM;
    case PDDI_PIXEL_DXT5: return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;
    }
    PDDIASSERT(false);
    return GL_INVALID_ENUM;
};

static inline pddiPixelFormat PickPixelFormat(pddiTextureType type, int bitDepth, int alphaDepth)
{
    switch (type)
    {
    case PDDI_TEXTYPE_RGB:
        switch (alphaDepth)
        {
        case 0:
            return (bitDepth <= 16) ? PDDI_PIXEL_RGB565 : PDDI_PIXEL_RGB888;
        case 1:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB1555 : PDDI_PIXEL_ARGB8888;
        default:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB4444 : PDDI_PIXEL_ARGB8888;
        }
        break;

    case PDDI_TEXTYPE_PALETTIZED:
        return PDDI_PIXEL_PAL8;

    case PDDI_TEXTYPE_LUMINANCE:
        return PDDI_PIXEL_LUM8;

    case PDDI_TEXTYPE_BUMPMAP:
        return PDDI_PIXEL_DUDV88;

    case PDDI_TEXTYPE_DXT1:
        return PDDI_PIXEL_DXT1;

    case PDDI_TEXTYPE_DXT2:
        return PDDI_PIXEL_DXT2;

    case PDDI_TEXTYPE_DXT3:
        return PDDI_PIXEL_DXT3;

    case PDDI_TEXTYPE_DXT4:
        return PDDI_PIXEL_DXT4;

    case PDDI_TEXTYPE_DXT5:
        return PDDI_PIXEL_DXT5;

    case PDDI_TEXTYPE_YUV:
        return PDDI_PIXEL_YUV;
    }
    PDDIASSERT(false);
    return PDDI_PIXEL_UNKNOWN;
};

#if defined(RAD_PSP)
// Largest texture dimension uploaded to GL on PSP. The PC scene assets ship
// 256x256+ 32-bit textures; the PSP has only ~2MB of VRAM, so sampling many
// full-size textures thrashes bandwidth and tanks the frame rate. We box-
// downsample anything larger to this cap at upload time (once — SetGLState
// caches the GL texture). UVs are normalised, so this only lowers texel
// resolution, it doesn't change how the texture maps. Tune for quality/FPS.
#ifndef PSP_MAX_TEX_DIM
#define PSP_MAX_TEX_DIM 64
#endif

// Box-downsample a 32-bit (4 byte/texel) image from srcW x srcH to dstW x dstH
// (both must divide evenly — always true here, dimensions are powers of two).
// Returns a freshly allocated buffer the caller must radMemoryFreeAligned().
static unsigned char* pspDownsampleRGBA( const unsigned char* src,
                                         int srcW, int srcH, int dstW, int dstH )
{
    unsigned char* dst = (unsigned char*)radMemoryAllocAligned(
        radMemoryGetCurrentAllocator(), (size_t)dstW * dstH * 4, 16 );
    int sx = srcW / dstW;
    int sy = srcH / dstH;
    int blockCount = sx * sy;
    for( int y = 0; y < dstH; y++ )
    {
        for( int x = 0; x < dstW; x++ )
        {
            unsigned r = 0, g = 0, b = 0, a = 0;
            for( int j = 0; j < sy; j++ )
            {
                const unsigned char* row = src + (size_t)((y*sy + j) * srcW + x*sx) * 4;
                for( int i = 0; i < sx; i++ )
                {
                    r += row[i*4+0]; g += row[i*4+1];
                    b += row[i*4+2]; a += row[i*4+3];
                }
            }
            unsigned char* o = dst + (size_t)(y*dstW + x) * 4;
            o[0] = (unsigned char)(r / blockCount);
            o[1] = (unsigned char)(g / blockCount);
            o[2] = (unsigned char)(b / blockCount);
            o[3] = (unsigned char)(a / blockCount);
        }
    }
    return dst;
}
#endif

void pglTexture::SetGLState(void)
{
    if(context->contextID != contextID)
    {
        contextID = context->contextID;
        gltexture = 0;
    }

    MICROPROFILE_SCOPEI("PDDI", "pglTexture::SetGLState", MP_RED);

    glEnable(GL_TEXTURE_2D);
    if(!valid)
    {
        glDeleteTextures(1, &gltexture);
        glGenTextures(1,&gltexture);
        glBindTexture(GL_TEXTURE_2D, gltexture);

        if (type == PDDI_TEXTYPE_PALETTIZED)
        {
            // Native paletted: pixel data is 8-bit indices in system memory.
            // Expand through the CLUT into a transient RGBA buffer only now, at
            // upload (visible textures only), so system RAM stays 8-bit. Build
            // explicit R,G,B,A bytes (pddiColour packs BGRA) and upload GL_RGBA.
            int n = xSize * ySize;
            unsigned char* rgba = (unsigned char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), (size_t)n * 4, 16);
            const unsigned char* idx = (const unsigned char*)bits[0];
            for (int i = 0; i < n; i++)
            {
                int e;
                if (lock.depth == 4)
                {
                    unsigned char b = idx[i >> 1];       // 2 pixels per byte
                    e = (i & 1) ? (b >> 4) : (b & 0x0f);
                }
                else
                {
                    e = idx[i];
                }
                pddiColour col = m_palette[ e ];
                rgba[i*4+0] = (unsigned char)col.Red();
                rgba[i*4+1] = (unsigned char)col.Green();
                rgba[i*4+2] = (unsigned char)col.Blue();
                rgba[i*4+3] = (unsigned char)col.Alpha();
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, xSize, ySize, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)rgba);
            radMemoryFreeAligned(rgba);
#if defined(RAD_PSP)
            { FILE* tf = pspDiagFopen("ms0:/shar_tex.log","a");
              if (tf) { fprintf(tf, "upload PAL8 %dx%d pal=%d err=%x\n",
                        xSize, ySize, m_nPaletteEntries, (unsigned)glGetError()); fclose(tf);} }
#endif
        }
        else if (type == PDDI_TEXTYPE_DXT1 || type == PDDI_TEXTYPE_DXT3 || type == PDDI_TEXTYPE_DXT5)
        {
            unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
            GLenum internalFormat = lock.format == PDDI_PIXEL_DXT5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT :
                lock.format == PDDI_PIXEL_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT : GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, xSize,
                ySize, 0, (int)(ceil(xSize/4.0)*ceil(ySize/4.0)*blocksize), (GLvoid*)bits[0]);
#if defined(RAD_PSP)
            { FILE* tf = pspDiagFopen("ms0:/shar_tex.log","a");
              if (tf) { fprintf(tf, "upload DXT %dx%d fmt=%d bits=%p err=%x\n",
                        xSize, ySize, (int)lock.format, (void*)bits[0], (unsigned)glGetError()); fclose(tf);} }
#endif
        }
        else
        {
#if defined(RAD_PSP)
            // pspGL is GL ES 1.1: glTexImage2D requires internalformat == format
            // and rejects sized/sRGB internal formats. The image decoders always
            // hand us 32-bit RGBA, so use the data format for both. Otherwise
            // PickPixelFormat's GL_SRGB8/GL_RGB5/... raise GL_INVALID_OPERATION
            // (0x502), the texture never uploads, and geometry renders black.
            GLenum dataFormat = lock.native ? GL_BGRA_EXT : GL_RGBA;
            // Cap large scene textures to fit PSP VRAM (see PSP_MAX_TEX_DIM).
            int dx = xSize, dy = ySize;
            while( dx > PSP_MAX_TEX_DIM || dy > PSP_MAX_TEX_DIM )
            {
                if( dx > PSP_MAX_TEX_DIM ) dx >>= 1;
                if( dy > PSP_MAX_TEX_DIM ) dy >>= 1;
            }
            if( dx != xSize || dy != ySize )
            {
                unsigned char* small = pspDownsampleRGBA(
                    (const unsigned char*)bits[0], xSize, ySize, dx, dy );
                glTexImage2D(GL_TEXTURE_2D, 0, dataFormat, dx, dy, 0,
                    dataFormat, GL_UNSIGNED_BYTE, (GLvoid *)small);
                radMemoryFreeAligned( small );
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, dataFormat, xSize,
                    ySize, 0, dataFormat, GL_UNSIGNED_BYTE, (GLvoid *)bits[0]);
            }
            { FILE* tf = pspDiagFopen("ms0:/shar_tex.log","a");
              if (tf) { fprintf(tf, "upload RAW %dx%d->%dx%d fmt=%d native=%d err=%x\n",
                        xSize, ySize, dx, dy, (int)lock.format, (int)lock.native, (unsigned)glGetError()); fclose(tf);} }
#else
            glTexImage2D(GL_TEXTURE_2D, 0, PickPixelFormat(lock.format), xSize,
                ySize, 0, lock.native ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE,
                (GLvoid *)bits[0]);
#endif
        }

        valid = true;
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, gltexture);
    }
}

int fastlog2(int x)
{
    int r = 0;
    int tmp = x;
    while(tmp > 1)
    {
        r++;
        tmp = tmp >> 1;

        if((tmp << r) != x)
            // not power of 2
            return -1;
    }
    return r;
}

bool pglTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip, pddiTextureType textureType, pddiTextureUsageHint usageHint)
{
#if defined(RAD_PSP)
    { FILE* tf = pspDiagFopen("ms0:/shar_gltex.log","a");
      if (tf) { fprintf(tf, "Create type=%d %dx%d bpp=%d nMip=%d\n", (int)textureType, x, y, bpp, nMip); fclose(tf);} }
#endif
    xSize = x;
    ySize = y;
    nMipMap = nMip;
    type = textureType;

    log2X = fastlog2(xSize);
    log2Y = fastlog2(ySize);

    if((log2X == -1) || (log2Y == -1))
    {
        lastError = PDDI_TEX_NOT_POW_2;
        return false;
    }

    if ((xSize > context->GetMaxTextureDimension()) ||
        (ySize > context->GetMaxTextureDimension()))
    {
        lastError = PDDI_TEX_TOO_BIG;
        return false;
    }

    // Paletted textures are kept 8-bit in system memory (see gltex.hpp) — do
    // NOT expand to RGB here. SetGLState() expands via the palette at upload.

    bits = new char* [nMipMap + 1];
    if (type == PDDI_TEXTYPE_DXT1 || type == PDDI_TEXTYPE_DXT3 || type == PDDI_TEXTYPE_DXT5)
    {
        unsigned int blocksize = type == PDDI_TEXTYPE_DXT1 ? 8 : 16;
        for(int i = 0; i < nMipMap+1; i++)
            bits[i] = (char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), (size_t)(ceil(double(xSize>>i)/4)*ceil(double(ySize>>i)/4)*blocksize), 16);
    }
    else
    {
        for(int i = 0; i < nMipMap+1; i++)
            bits[i] = (char*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), ((xSize>>i)*(ySize>>i)*bpp)/8, 16);
    }

    lock.depth = bpp;
    lock.format = PickPixelFormat(textureType, bpp, alphaDepth);

    if(context->GetDisplay()->ExtBGRA())
    {
        lock.native = true;
        lock.rgbaLShift[0] = lock.rgbaRShift[0] =
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaLShift[2] = lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x00ff0000;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x000000ff;
        lock.rgbaMask[3] = 0xff000000;
    }
    else
    {
        lock.native = false;
        lock.rgbaRShift[0] = 16;
        lock.rgbaLShift[2] = 16;

        lock.rgbaLShift[0] = 
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x000000ff;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x00ff0000;
        lock.rgbaMask[3] = 0xff000000;
    }

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, (float)((xSize * ySize * lock.depth) / 8192));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, 1);

#if defined(RAD_PSP)
    { FILE* tf = pspDiagFopen("ms0:/shar_gltex.log","a");
      if (tf) { fprintf(tf, "  Create OK (lock.fmt=%d depth=%d)\n", (int)lock.format, lock.depth); fclose(tf);} }
#endif
    return true;
}

pglTexture::pglTexture(pglContext* c)
{
    context = c;
    contextID = c->contextID;
    bits = NULL;
    gltexture = 0;
    priority = 15;
    valid = false;
    m_nPaletteEntries = 0;
}

pglTexture::~pglTexture()
{
    if(gltexture) glDeleteTextures(1, &gltexture);

    for(int i = 0; i < nMipMap+1; i++)
        radMemoryFreeAligned(bits[i]);

    if(bits) delete [] bits;

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -(float)((xSize * ySize * lock.depth) / 8192));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, -1);
}

pddiPixelFormat pglTexture::GetPixelFormat()
{
    // Report PAL8 for paletted textures so the image converter knows to hand us
    // the palette (SetPalette); everything else is treated as 32-bit RGBA.
    if (type == PDDI_TEXTYPE_PALETTIZED)
        return PDDI_PIXEL_PAL8;
    return PDDI_PIXEL_ARGB8888;
}

int   pglTexture::GetWidth()
{
    return xSize;
}

int   pglTexture::GetHeight()
{
    return ySize;
}

int   pglTexture::GetDepth()
{
    return 32;
}

int   pglTexture::GetNumMipMaps()
{
    return nMipMap;
}

int pglTexture::GetAlphaDepth()
{
    return 8;
}

pddiLockInfo* pglTexture::Lock(int mipMap, pddiRect* rect)
{
    PDDIASSERT(mipMap <= nMipMap);

    lock.width = 1 << (log2X-mipMap);
    lock.height = 1 << (log2Y-mipMap);
    if (lock.format == PDDI_PIXEL_DXT1 || lock.format == PDDI_PIXEL_DXT3 || lock.format == PDDI_PIXEL_DXT5)
    {
        unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
        lock.pitch = (int)ceil( double( xSize >> mipMap ) / 4 ) * blocksize;
        lock.bits = bits[mipMap];
    }
    else if (lock.format == PDDI_PIXEL_YUV)
    {
        lock.pitch = (lock.width * lock.depth) / 8;
        lock.bits = bits[mipMap];
    }
    else if (lock.format == PDDI_PIXEL_PAL8)
    {
        // Paletted: PickPixelFormat reports PAL8 for BOTH 4- and 8-bit, so use
        // lock.depth (bpp) for the byte math — a 4-bit texture is 0.5 byte/pixel
        // (fonts are 4-bit). Same bottom-up convention as the RGBA path so the
        // fillers (converter FillLockPAL8, imagefactory ProcessScanline8) and
        // SetGLState agree on orientation.
        int rowBytes = (lock.width * lock.depth) / 8;
        lock.pitch = -rowBytes;
        lock.bits = bits[mipMap] + rowBytes * (lock.height - 1);
    }
    else
    {
        lock.pitch = -(lock.width * 4);
        lock.bits = bits[mipMap] + (lock.width * (lock.height - 1) * 4);
    }

    return &lock;
}

void pglTexture::Unlock(int mipLevel)
{
    valid = false;
}

void pglTexture::SetPriority(int p)
{
    priority = p;
}

int pglTexture::GetPriority(void)
{
    return priority;
}

// paging control
void pglTexture::Prefetch(void)
{
}

void pglTexture::Discard(void)
{
}

// palette managment
int pglTexture::GetNumPaletteEntries(void)
{
    return m_nPaletteEntries;
}

void pglTexture::SetPalette(int nEntries, pddiColour* palette)
{
    if (nEntries > 256) nEntries = 256;
    m_nPaletteEntries = nEntries;
    for (int i = 0; i < nEntries; i++)
        m_palette[i] = palette[i];
    valid = false;   // force re-upload with the (new) palette
}

int pglTexture::GetPalette(pddiColour* palette)
{
    for (int i = 0; i < m_nPaletteEntries; i++)
        palette[i] = m_palette[i];
    return m_nPaletteEntries;
}


