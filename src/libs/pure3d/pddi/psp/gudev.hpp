//=============================================================================
// Native PSP sceGU pddi backend — device.
//
// Mirrors pddi/gl/gldev but drives the PSP Graphics Engine directly via sceGu*
// instead of going through pspGL (whose per-draw shim overhead makes this game
// unplayably slow). Selected by linking pddigu_psp instead of pddigl_psp; the
// single pddiCreate() entry point returns this device.
//=============================================================================
#ifndef _GUDEV_HPP_
#define _GUDEV_HPP_

#include <pddi/pddi.hpp>

class pguDevice : public pddiDevice
{
public:
    pguDevice();
    ~pguDevice();

    void        GetLibraryInfo(pddiLibInfo* info);
    const char* GetDeviceDescription();
    int         GetDisplayInfo(pddiDisplayInfo** info);
    unsigned    GetCaps();

    void SetCurrentContext(pddiRenderContext* context);
    pddiRenderContext* GetCurrentContext();

    pddiDisplay* NewDisplay(int id);
    pddiRenderContext* NewRenderContext(pddiDisplay* display);
    pddiTexture* NewTexture(pddiTextureDesc* desc);
    pddiPrimBuffer* NewPrimBuffer(pddiPrimBufferDesc* desc);
    pddiShader* NewShader(const char* name, const char* aux = NULL);

    void AddCustomShader(const char* name, const char* aux = NULL);

    void Release(void);

protected:
    bool initialized;
    pddiRenderContext* context;

    int nDisplays;
    pddiDisplayInfo* displayInfo;
};

#endif
