//=============================================================================
// Native PSP sceGU pddi backend — render context + prim buffer.
//=============================================================================
#ifndef _GUCON_HPP_
#define _GUCON_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/basecontext.hpp>

class pguDisplay;
class pguDevice;

//--------------------------------------------------------------
class pguContext : public pddiBaseContext
{
public:
    pguContext(pguDevice* dev, pguDisplay* disp);
    ~pguContext();

    void BeginFrame();
    void EndFrame();

    void Clear(unsigned bufferMask);

    void SetScissor(pddiRect* rect);

    pddiPrimStream* BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount = 0, unsigned pass = 0);
    void EndPrims(pddiPrimStream* stream);

    void DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer);

    int GetMaxLights();
    void SetAmbientLight(pddiColour col);

    void SetCullMode(pddiCullMode mode);
    void SetColourWrite(bool red, bool green, bool blue, bool alpha);

    void EnableZBuffer(bool enable);
    void SetZCompare(pddiCompareMode compareMode);
    void SetZWrite(bool);
    void SetZBias(float bias);
    void SetZRange(float n, float f);

    void EnableStencilBuffer(bool enable);
    void SetStencilCompare(pddiCompareMode compare);
    void SetStencilRef(int ref);
    void SetStencilMask(unsigned mask);
    void SetStencilWriteMask(unsigned mask);
    void SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp);

    void SetFillMode(pddiFillMode mode);

    void EnableFog(bool enable);
    void SetFog(pddiColour colour, float start, float end);

    int GetMaxTextureDimension(void);

    pddiExtension* GetExtension(unsigned extID);
    bool VerifyExtension(unsigned extID);

    pguDisplay* GetDisplay(void) { return display; }

    unsigned contextID;

    // apply the current shader's texture+blend state to the GE
    void ApplyShaderState(pddiShader* material);

protected:
    void LoadHardwareMatrix(pddiMatrixType id);
    void SetupHardwareProjection(void);
    void SetupHardwareLight(int);
    void  BeginTiming(void);
    float EndTiming(void);

    void SetVertexArray(unsigned descr, void* data, int count);

    pguDevice* device;
    pguDisplay* display;

    pddiShader* defaultShader;
    int maxTexSize;

    // sceGU commands are only valid inside a sceGuStart/sceGuFinish block. The
    // engine calls state setters outside a frame (DefaultState / tContext::Setup),
    // so those must NOT touch the GE — they just update the base state, which is
    // re-applied to the GE at BeginFrame.
    bool m_inFrame;
    void ApplyRenderState(void);
};

//--------------------------------------------------------------
// Retained-mode geometry: vertices are converted to a single GU-interleaved
// buffer at build time and drawn with one sceGuDrawArray call.
class pguPrimBuffer : public pddiPrimBuffer
{
public:
    pguPrimBuffer(pguContext* context, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex);
    ~pguPrimBuffer();

    pddiPrimBufferStream* Lock();
    void Unlock(pddiPrimBufferStream* stream);

    unsigned char* LockIndexBuffer();
    void UnlockIndexBuffer(int count);

    void SetIndices(unsigned short* indices, int count);

    bool CheckMemImageVersion(int) { return false; }
    void* LockMemImage(unsigned) { return NULL; }
    void UnlockMemImage() {}
    unsigned GetMemImageLength() { return 0; }
    void SetMemImageParam(unsigned, unsigned) {}

    void Display(void);

    pddiPrimType GetPrimType() const { return primType; }
    unsigned GetVertexType() const   { return vertexType; }

protected:
    pddiPrimBufferStream* stream;
    pguContext* context;

    pddiPrimType primType;
    unsigned vertexType;

    // interleaved GU vertex data (see gucon.cpp for the packed struct + guVType)
    void* verts;          // aligned GU-format vertex array
    unsigned guVType;     // GU vertex-descriptor bits
    unsigned stride;      // bytes per GU vertex
    unsigned allocated;   // vertex capacity
    unsigned total;       // vertices written via the stream

    unsigned short* indices;
    unsigned indexCount;
};

#endif
