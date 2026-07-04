//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        platform.cpp  (PSP)
//
// Description: PSP implementation of the Pure3D tPlatform. Mirrors the win32
//              platform but creates the pddi device/display/context over the
//              PSP EGL display backend instead of SDL. tContext's ctor/Setup
//              are private (friend tPlatform), so context creation must live
//              here.
//
//=============================================================================

#include <p3d/platform/psp/platform.hpp>
#include <p3d/context.hpp>
#include <p3d/utility.hpp>
#include <p3d/memory.hpp>
#include <pddi/pddi.hpp>

#include <radtime.hpp>

//-----------------------------------------------------------------------------
tContextInitData::tContextInitData()
{
    window    = NULL;
    adapterID = 0;
    PDDIlib[0] = '\0';

    // pddiDisplayInit defaults → PSP framebuffer
    xsize = 480;
    ysize = 272;
    bpp   = 32;
}

//-----------------------------------------------------------------------------
tPlatform* tPlatform::currentPlatform = NULL;

tPlatform::tPlatform(void* inst)
{
    instance       = inst;
    currentContext = NULL;
    nContexts      = 0;
}

tPlatform::~tPlatform()
{
}

//-----------------------------------------------------------------------------
tPlatform* tPlatform::Create(void* instance)
{
    if (currentPlatform == NULL)
    {
        currentPlatform = InternalCreate(instance);
    }
    return currentPlatform;
}

tPlatform* tPlatform::InternalCreate(void* instance)
{
    return new tPlatform(instance);
}

tPlatform* tPlatform::GetPlatform(void)
{
    return currentPlatform;
}

void tPlatform::Destroy(tPlatform* plat)
{
    if (plat != NULL)
    {
        delete plat;
        currentPlatform = NULL;
    }
}

//-----------------------------------------------------------------------------
tContext* tPlatform::CreateContext(tContextInitData* d)
{
    pddiDevice*        device  = NULL;
    pddiDisplay*       display = NULL;
    pddiRenderContext* context = NULL;

    p3d::UsePermanentMem(true);

    int success = pddiCreate(PDDI_VERSION_MAJOR, PDDI_VERSION_MINOR, &device);
    if (success != PDDI_OK || device == NULL)
    {
        p3d::UsePermanentMem(false);
        return NULL;
    }

    display = device->NewDisplay(d->adapterID);
    display->SetWindow(d->window);
    display->InitDisplay(d);

    context = device->NewRenderContext(display);
    if (context == NULL)
    {
        p3d::UsePermanentMem(false);
        return NULL;
    }

    for (int find = 0; find < P3D_MAX_CONTEXTS; find++)
    {
        if (!contexts[find].context)
        {
            contexts[find].context = new tContext(device, display, context);
            contexts[find].window  = d->window;

            if (!currentContext)
                SetActiveContext(contexts[find].context);

            contexts[find].context->Setup();

            nContexts++;
            p3d::UsePermanentMem(false);
            return contexts[find].context;
        }
    }

    p3d::UsePermanentMem(false);
    return NULL;
}

//-----------------------------------------------------------------------------
void tPlatform::DestroyContext(tContext* context)
{
    for (int find = 0; find < P3D_MAX_CONTEXTS; find++)
    {
        if (contexts[find].context == context)
        {
            delete contexts[find].context;
            contexts[find].context = NULL;
            contexts[find].window  = NULL;
            if (currentContext == context)
                currentContext = NULL;
            nContexts--;
            return;
        }
    }
}

//-----------------------------------------------------------------------------
void tPlatform::SetActiveContext(tContext* context)
{
    currentContext   = context;
    p3d::context     = context;
    p3d::inventory   = context->GetInventory();
    p3d::stack       = context->GetMatrixStack();
    p3d::loadManager = context->GetLoadManager();
    p3d::pddi        = context->GetContext();
    p3d::device      = context->GetDevice();
    p3d::display     = context->GetDisplay();
}

//-----------------------------------------------------------------------------
bool tPlatform::ProcessWindowsMessage(SDL_Window*, const SDL_WindowEvent*)
{
    // No window messaging on PSP.
    return false;
}

//-----------------------------------------------------------------------------
P3D_U64 tPlatform::GetTimeFreq(void)
{
    return 1000000;  // microsecond clock
}

P3D_U64 tPlatform::GetTime(void)
{
    return (P3D_U64)radTimeGetMicroseconds64();
}

//-----------------------------------------------------------------------------
tFile* tPlatform::OpenFile(const char* /*filename*/)
{
    // Pure3D loads go through tFileFTT (radfile); the platform file map is
    // unused on PSP.
    return NULL;
}
