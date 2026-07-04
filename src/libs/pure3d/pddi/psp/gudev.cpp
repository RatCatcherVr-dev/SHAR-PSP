//=============================================================================
// Native PSP sceGU pddi backend — device + pddiCreate entry point.
//=============================================================================
#include <pddi/psp/gudev.hpp>
#include <pddi/psp/gudisplay.hpp>
#include <pddi/psp/gucon.hpp>
#include <pddi/psp/gutex.hpp>
#include <pddi/psp/gumat.hpp>

#include <stdio.h>
#include <string.h>
#include <pddi/base/debug.hpp>

#define PDDI_GU_BUILD 1

static pguDevice gblDevice;
static char libName[] = "PSP sceGU";

int pddiCreate(int versionMajor, int versionMinor, pddiDevice** device)
{
    if((versionMajor != PDDI_VERSION_MAJOR) || (versionMinor != PDDI_VERSION_MINOR))
    {
        *device = NULL;
        return PDDI_VERSION_ERROR;
    }
    *device = &gblDevice;
    return PDDI_OK;
}

pguDevice::pguDevice() : initialized(false), context(NULL), nDisplays(0), displayInfo(NULL) {}

pguDevice::~pguDevice()
{
    if(displayInfo)
    {
        for(int i = 0; i < nDisplays; i++)
            delete[] displayInfo[i].modeInfo;
        delete[] displayInfo;
    }
}

void pguDevice::GetLibraryInfo(pddiLibInfo* info)
{
    info->versionMajor = PDDI_VERSION_MAJOR;
    info->versionMinor = PDDI_VERSION_MINOR;
    info->versionBuild = PDDI_GU_BUILD;
    info->libID = PDDI_LIBID_PS2;   // no PSP id; PS2 is the closest console tag
    strcpy(info->description, libName);
}

unsigned pguDevice::GetCaps() { return 0; }

int pguDevice::GetDisplayInfo(pddiDisplayInfo** info)
{
    if(!displayInfo)
    {
        nDisplays = 1;
        displayInfo = new pddiDisplayInfo[1];
        displayInfo[0].id = 0;
        strcpy(displayInfo[0].description, "PSP");
        displayInfo[0].pci = 0;
        displayInfo[0].vendor = 0;
        displayInfo[0].fullscreenOnly = true;
        displayInfo[0].caps = 0;
        displayInfo[0].modeInfo = new pddiModeInfo[1];
        displayInfo[0].modeInfo[0].width = 480;
        displayInfo[0].modeInfo[0].height = 272;
        displayInfo[0].modeInfo[0].bpp = 32;
        displayInfo[0].nDisplayModes = 1;
    }
    *info = displayInfo;
    return nDisplays;
}

const char* pguDevice::GetDeviceDescription() { return libName; }

void pguDevice::SetCurrentContext(pddiRenderContext* c) { context = c; }
pddiRenderContext* pguDevice::GetCurrentContext() { return context; }

pddiDisplay* pguDevice::NewDisplay(int)
{
    return (pddiDisplay*)new pguDisplay();
}

pddiRenderContext* pguDevice::NewRenderContext(pddiDisplay* display)
{
    pguContext* c = new pguContext(this, (pguDisplay*)display);
    if(c->GetLastError() != PDDI_OK)
    {
        delete c;
        return NULL;
    }
    return c;
}

pddiTexture* pguDevice::NewTexture(pddiTextureDesc* desc)
{
    pguTexture* tex = new pguTexture((pguContext*)context);
    if(!tex->Create(desc->GetSizeX(), desc->GetSizeY(), desc->GetBitDepth(),
                    desc->GetAlphaDepth(), desc->GetMipMapCount(), desc->GetType(), desc->GetUsage()))
    {
        delete tex;
        return NULL;
    }
    return tex;
}

pddiShader* pguDevice::NewShader(const char*, const char*)
{
    return new pguMat((pguContext*)context);
}

pddiPrimBuffer* pguDevice::NewPrimBuffer(pddiPrimBufferDesc* desc)
{
    return new pguPrimBuffer((pguContext*)context, desc->GetPrimType(),
                             desc->GetVertexFormat(), desc->GetVertexCount(), desc->GetIndexCount());
}

void pguDevice::AddCustomShader(const char*, const char*) {}
void pguDevice::Release(void) {}
