//=============================================================================
// PSP test EBOOT for the SHAR (SRR2) engine port.
//
// Brings up the full new stack end-to-end on hardware/emulator:
//   radcore (thread/memory/time/file + PSP Memory-Stick drive)
//     -> pddi (GL/EGL device, display, render context)
//       -> Pure3D tContext (via tPlatform) + minimal chunk loaders
//         -> loads a real game .p3d ("test.p3d") off the Memory Stick.
//
// Visual result (so it can be judged on PPSSPP / hardware without a debugger):
//   * pulsing GREEN  = engine came up AND a .p3d loaded through the chunk loader
//   * pulsing RED    = engine came up but the load failed (bad/missing chunks)
//   * pulsing BLUE   = no test.p3d found / load returned a hard fail
// A steady pulse at all proves radcore + pddi + tContext + the frame loop run.
//
// Drawing the loaded tGeometry (camera/shader/vertex-format work) is the next
// iteration; the render loop below is structured so that slots straight in.
//=============================================================================

#include <pspkernel.h>
#include <pspsysmem.h>   // sceKernelMaxFreeMemSize — real heap ceiling at boot
#include <pspdebug.h>
#include <pspctrl.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>   // mallinfo() — real heap high-water for the OOM probe

static void mlog(const char* s)
{
    FILE* f = fopen("ms0:/shar_main.log", "a");
    if (f) { fputs(s, f); fputc('\n', f); fclose(f); }
}

// Frontend main-menu selection index (0..5), read by FePure3dObject::Render to
// show only the selected item's glow object (mirrors the original menu). Driven
// by the D-pad below. Default 4 = New Game (Homer glow), the retail default.
int g_pspSelectedGlow = 4;

#include <radmemory.hpp>
#include <radthread.hpp>
#include <radtime.hpp>
#include <radfile.hpp>

#include <radload/radload.hpp>

#include <pddi/pddi.hpp>

#include <p3d/platform/psp/platform.hpp>
#include <p3d/context.hpp>
#include <p3d/utility.hpp>
#include <p3d/loadmanager.hpp>
#include <p3d/inventory.hpp>
#include <p3d/geometry.hpp>
#include <p3d/shader.hpp>
#include <p3d/texture.hpp>
#include <p3d/drawable.hpp>
#include <p3d/view.hpp>
#include <p3d/pointcamera.hpp>
#include <p3d/matrixstack.hpp>
#include <p3d/scenegraph/scenegraph.hpp>
#include <p3d/anim/skeleton.hpp>
#include <p3d/anim/polyskin.hpp>
#include <p3d/anim/compositedrawable.hpp>
#include <p3d/anim/animate.hpp>          // tAnimationLoader, tFrameControllerLoader
#include <p3d/anim/multicontroller.hpp>  // tMultiControllerLoader
#include <p3d/camera.hpp>                // tCameraLoader (camset framing)
#include <p3d/light.hpp>                 // tLightLoader, tLightGroupLoader
#include <p3d/billboardobject.hpp>       // tBillboardQuadGroupLoader (glows)
#include <p3d/locator.hpp>               // tLocatorLoader
#include <p3d/texturefont.hpp>
#include <p3d/imagefont.hpp>
#include <p3d/png.hpp>
#include <p3d/bmp.hpp>
#include <p3d/targa.hpp>
#include <p3d/sprite.hpp>
#include <p3d/image.hpp>

// Scrooby frontend/UI engine (menus)
#include <App.h>
#include <Project.h>
#include <Screen.h>
#include <Page.h>
#include <Text.h>
#include <Group.h>
#include <Layer.h>
#include <Pure3dObject.h>
#include "FeProject.h"   // to force-clear the frontend's "loading" flag (see PH_MENU)
#include <p3d/anim/multicontroller.hpp>
#include <math.h>

// Main-menu label text (one multi-string Scrooby::Text; SetIndex picks which
// menu-item string is shown). Fetched once the MainMenu screen resolves; driven
// by the D-pad below. The game's CGuiMenu does this in the real frontend, but the
// harness doesn't run the GameFlow, so we replicate the small piece we need.
static Scrooby::Text* g_menuText = NULL;

// Camera intro (mirrors CGuiScreenIntroTransition). CamAndSet ("camset") is the
// room set + the shared menu camera + one long baked animation; the intro camera
// fly-in is the frame window [721..770] of that animation. We play it once then
// hold the idle pose (the pose frame 770 ends on) by not advancing further.
static Scrooby::Pure3dObject* g_camset  = NULL;
static Scrooby::Layer*        g_tvFrame = NULL;   // TV bezel overlay; fades in on intro
static int   g_introState = 0;   // 0=await controller, 1=playing intro, 2=idle/done
static const float FE_INTRO_START = 721.0f;
static const float FE_INTRO_END   = 770.0f;

// Iris wipe ("round black fade"): a full-screen 3D mask object "3dIris" on page
// "IrisCover" driven by the "IrisController" animation. Frame 0 and NumFrames =
// fully open (screen visible); the midpoint = fully closed (black). On menu open
// we start closed (mid) and play to the end so the iris opens to reveal the menu.
static Scrooby::Pure3dObject* g_iris      = NULL;
static Scrooby::Layer*        g_irisLayer = NULL;
static tMultiController*       g_irisMC    = NULL;
static float g_irisFrames = 0.0f;
static int   g_irisState  = 0;   // 0=inactive/absent, 1=revealing, 2=done(hidden)

// Set true to render the frontend menu (Scrooby) instead of a model .p3d.
static const bool kMenuMode = true;

// Cached-room backdrop (implemented in the GL backend, pddi/gl/glcon.cpp).
extern "C" void pglDrawRoomBackdrop( void );

// Frame-time breakdown (ms) for PSP profiling.
float g_dbgBackdropMs = 0.0f, g_dbgDrawFrameMs = 0.0f, g_dbgEndFrameMs = 0.0f;
int g_pspDiagFrame = -1;  // >=0 => log each drawable child (first menu frames)

// Fires when a Scrooby project finishes loading (records the project pointer).
struct HarnessLoadCB : public Scrooby::LoadProjectCallback
{
    volatile bool       done;
    Scrooby::Project*   proj;
    HarnessLoadCB() : done(false), proj(NULL) {}
    void OnProjectLoadComplete( Scrooby::Project* p ) { proj = p; done = true; }
};

// Frontend load phases: show bootup.p3d's loading screen while the large
// frontend.p3d streams in the background, then switch to the main menu.
enum FePhase { PH_BOOTUP, PH_FRONTEND, PH_MENU };
static FePhase        s_fePhase = PH_BOOTUP;
static HarnessLoadCB  s_bootCB;
static HarnessLoadCB  s_feCB;

#include <radmath/radmath.hpp>
#include <math.h>

PSP_MODULE_INFO("SHAR_P3D_TEST", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);   // give all but 1MB of the user heap to malloc/radMemory

//-----------------------------------------------------------------------------
// Exit callback so the PSP HOME button quits cleanly.
//-----------------------------------------------------------------------------
static volatile int s_exit = 0;

static int ExitCallback(int, int, void*) { s_exit = 1; return 0; }
static int CallbackThread(SceSize, void*)
{
    int cbid = sceKernelCreateCallback("ExitCB", ExitCallback, 0);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void SetupCallbacks()
{
    int th = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    SetupCallbacks();

    // truncate logs from any previous run
    { FILE* f = fopen("ms0:/shar_main.log",  "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_gl.log",    "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_load.log",  "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_thread.log","w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_drive.log", "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_ftt.log",   "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_tex.log",   "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_chunks.log","w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_sprite.log","w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_fe.log",    "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_rm.log",    "w"); if (f) fclose(f); }
    { FILE* f = fopen("ms0:/shar_gltex.log", "w"); if (f) fclose(f); }
    mlog("boot");

    // Real available user memory at boot — the true heap ceiling. On a CFW PSP
    // with plugins loaded (see seplugins/), the kernel/VSH/plugins consume much
    // of the 32 MB, so a 1000 may hand the app far less than the ~24 MB the bare
    // partition suggests. PSP_HEAP_SIZE_KB(-1024) then reserves all-but-1MB of
    // THIS. If the frontend load dies at ~this-many MB used, it's OOM at the
    // real ceiling (the post-hoc shar_oom.log can't survive a hard power-off).
    {
        char buf[128];
        sprintf(buf, "mem: MaxFree=%uKB TotalFree=%uKB",
                sceKernelMaxFreeMemSize() / 1024, sceKernelTotalFreeMemSize() / 1024);
        mlog(buf);
    }

    // Log what PPSSPP/PSP gives us — essential for diagnosing path issues.
    {
        char buf[256];
        if (argc > 0 && argv && argv[0])
        {
            sprintf(buf, "argv[0]=%s", argv[0]);
            mlog(buf);
        }
        else
        {
            mlog("argc=0 (no argv)");
        }
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd)))
        {
            sprintf(buf, "cwd=%s", cwd);
            mlog(buf);
        }
        else
        {
            mlog("getcwd failed");
        }
    }

    // --- radcore foundation -------------------------------------------------
    radThreadInitialize();
    radMemoryInitialize();
    radTimeInitialize();
    radFileInitialize(50, 32, RADMEMORY_ALLOC_DEFAULT);
    radLoadInitialize();                             // creates the radLoad singleton (tLoadManager::AddHandler uses it)
    radDriveMount(NULL, RADMEMORY_ALLOC_DEFAULT);    // mount default drive (ms0:)

    // --- Pure3D platform + context (pddi device/display/context inside) -----
    tPlatform* platform = tPlatform::Create(NULL);

    tContextInitData init;
    init.xsize = 480;
    init.ysize = 272;
    init.bpp   = 32;

    tContext* ctx = platform->CreateContext(&init);
    mlog(ctx ? "context created" : "context NULL");

    // Files loaded in order. Shared character assets (global.p3d: the
    // char_swatches / eyeball texture atlas) MUST load before any model that
    // references them — shader->texture binding is resolved at load time, so the
    // atlas has to already be in the inventory. If global.p3d is absent the load
    // just cancels and we move on (harmless for non-character models).
    static const char* kFiles[]  = { "global.p3d", "test.p3d" };
    static const int   kNumFiles = (int)(sizeof(kFiles) / sizeof(kFiles[0]));
    int   fileIdx    = 0;
    int   flushCount = 0;
    bool  allLoaded  = false;
    bool  curFileDone = false;
    tLoadRequest* req = NULL;   // current file's async request

    // Start an async load for one file and wake the worker once. We build the
    // request by hand (p3d::loadAsync's SetCallback(NULL) null-derefs, and a
    // sync open deadlocks on WaitForCompletion), then pump it in the loop.
    auto startLoad = [&](const char* fn) -> tLoadRequest*
    {
        mlog(fn);
        radMemoryAllocator old = ::radMemorySetCurrentAllocator(RADMEMORY_ALLOC_TEMP);
        tFile* lf = p3d::openFile(fn, false);
        tLoadRequest* r = new tLoadRequest(lf);
        ::radMemorySetCurrentAllocator(old);
        r->SetAsync(true);
        p3d::loadManager->Load(r);
        p3d::loadManager->SwitchTask();   // wake the worker once
        return r;
    };

    if (ctx)
    {
        // --- minimal chunk loaders (NOT InstallDefaultLoaders, which would
        //     force-link the excluded subsystems) --------------------------
        tP3DFileHandler* p3dh = new tP3DFileHandler;
        p3d::loadManager->AddHandler(p3dh, "p3d");
        p3dh->AddHandler(new tGeometryLoader);
        p3dh->AddHandler(new tShaderLoader);
        p3dh->AddHandler(new tTextureLoader);
        p3dh->AddHandler(new Scenegraph::Loader);        // multi-mesh scene-graph models
        p3dh->AddHandler(new tSkeletonLoader);           // skeleton (joint bind pose)
        p3dh->AddHandler(new tPolySkinLoader);           // skinned meshes
        p3dh->AddHandler(new tCompositeDrawableLoader);  // characters + vehicles
        p3dh->AddHandler(new tTextureFontLoader);        // fonts (menu text)
        p3dh->AddHandler(new tImageFontLoader);
        p3dh->AddHandler(new tImageLoader);              // Texture::IMAGE
        p3dh->AddHandler(new tSpriteLoader);             // Texture::SPRITE (menu 2D images!)
        // Scene loaders — required by the frontend menu's 3D room scene.
        p3dh->AddHandler(new tCameraLoader);             // P3D_CAMERA (camset framing)
        p3dh->AddHandler(new tLightLoader);              // LIGHT (scene lighting)
        p3dh->AddHandler(new tLightGroupLoader);         // P3D_LIGHT_GROUP
        p3dh->AddHandler(new tBillboardQuadGroupLoader); // QUAD_GROUP (glow sprites)
        p3dh->AddHandler(new tLocatorLoader);            // LOCATOR (reference points)
        p3dh->AddHandler(new tAnimationLoader);          // ANIMATION (keyframe data)
        p3dh->AddHandler(new tFrameControllerLoader);    // FRAME_CONTROLLER
        p3dh->AddHandler(new tMultiControllerLoader);    // P3D_MULTI_CONTROLLER (drives FCs)

        // Image FILE handlers — the frontend loads sprites from standalone
        // .png/.bmp/.tga files (e.g. resource/images/gamelogo.png). Without
        // these, every menu image sprite is blank.
        p3d::loadManager->AddHandler(new tPNGHandler,   "png");
        p3d::loadManager->AddHandler(new tBMPHandler,   "bmp");
        p3d::loadManager->AddHandler(new tTargaHandler, "tga");

        ctx->SetClearMask(PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH);
        ctx->SetClearDepth(1.0f);
        ctx->SetClearColour(tColour(24, 24, 40));

        if (kMenuMode)
        {
            // Scrooby::App::GetInstance() creates the frontend app and registers
            // the PROJECT + text-bible chunk handlers on our p3d file handler.
            mlog("scrooby: GetInstance");
            Scrooby::App* app = Scrooby::App::GetInstance();
            mlog(app ? "scrooby: app ok" : "scrooby: app NULL");
            // Two-project load: bootup first (its PROJECT chunk must parse
            // before the frontend's — loading frontend as the very first project
            // crashes in the Scrooby PROJECT-chunk handler), then frontend.
            app->LoadProject("art\\frontend\\scrooby\\bootup.p3d", &s_bootCB);
            mlog("scrooby: LoadProject(bootup) queued");
        }
        else
        {
            req = startLoad(kFiles[fileIdx]);   // begin with global.p3d
        }
    }
    // Controller: digital mode is enough for menu navigation (D-pad + buttons).
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    mlog("entering render loop");

    // --- scene state (built once the load finishes) -------------------------
    static const int MAX_GEOS = 512;
    tGeometry*   geos[ MAX_GEOS ];
    int          nGeos = 0;
    tView*       view  = NULL;
    tPointCamera* cam  = NULL;
    Scenegraph::Scenegraph* scene = NULL;   // set if the file has a scene graph
    tCompositeDrawable*     composite = NULL; // set if the file is a character/vehicle
    rmt::Vector  sceneCentre( 0, 0, 0 );
    float        sceneRadius = 10.0f;
    bool         sceneReady  = false;
    int          buildTries  = 0;

    // --- render loop --------------------------------------------------------
    unsigned frame = 0;
    float    orbitAng = 0.0f;   // model-viewer camera spin, time-integrated
    bool loggedDone = false;
    // Real per-frame delta (ms), so animation speed is decoupled from the
    // render rate: the harness must feed *elapsed* time to the Pure3D/Scrooby
    // controllers (they advance by deltaTime*fps), exactly like the desktop
    // game loop does with radTimeGetMilliseconds() (game.cpp:482). Feeding a
    // fixed 16 ms made animation track the frame rate (fast on PPSSPP @60,
    // slow-mo on real PSP @20). sceKernelGetSystemTimeWide() is microseconds.
    unsigned long long prevTimeUs = sceKernelGetSystemTimeWide();
    while (!s_exit)
    {
        unsigned long long nowTimeUs = sceKernelGetSystemTimeWide();
        float deltaMs = (float)(nowTimeUs - prevTimeUs) / 1000.0f;
        prevTimeUs = nowTimeUs;
        // Clamp hitches / debugger stalls (mirrors guisystem.cpp:508); also
        // guards the very first frame's bogus delta.
        if (deltaMs > 100.0f || deltaMs < 0.0f) deltaMs = 20.0f;

        int pulse = (frame & 0x3F);
        if (frame & 0x40) pulse = 0x3F - pulse;   // triangle wave 0..63

        // --- Scrooby frontend menu path ------------------------------------
        if (kMenuMode)
        {
            // Drive the resource-manager loads (Scrooby queues tLoadRequests):
            radFileService();                 // file IO
            p3d::loadManager->SwitchTask();   // process loads + fire callbacks/dump
            sceKernelDelayThread(2000);

            Scrooby::App* app = Scrooby::App::GetInstance();

            // Menu navigation: D-pad up/down moves the selection (0..5, wrapping),
            // which FePure3dObject::Render uses to show only that item's glow —
            // mirroring the original main menu. Edge-detected so one press = one
            // step. (Full Scrooby text-item highlight + Accept action come with the
            // GameFlow frontend port; this drives the glow selection for now.)
            if (s_fePhase == PH_MENU)
            {
                SceCtrlData pad;
                sceCtrlPeekBufferPositive(&pad, 1);
                static unsigned s_prevBtns = 0;
                unsigned pressed = pad.Buttons & ~s_prevBtns;   // rising edges
                // Navigate over the number of menu strings (falls back to 6 glows).
                int itemCount = (g_menuText && g_menuText->GetNumOfStrings() > 0)
                                ? g_menuText->GetNumOfStrings() : 6;
                if (pressed & PSP_CTRL_LEFT)
                    g_pspSelectedGlow = (g_pspSelectedGlow + itemCount - 1) % itemCount;
                if (pressed & PSP_CTRL_RIGHT)
                    g_pspSelectedGlow = (g_pspSelectedGlow + 1) % itemCount;
                s_prevBtns = pad.Buttons;

                // Drive the label + highlight/animation, mirroring CGuiMenu:
                //  - SetIndex   : show the selected item's string (text changes)
                //  - SetColour  : yellow highlight (the shown item is the selection)
                //  - Reset+Scale: shrink a bit (smaller) with a gentle size pulse
                if (g_menuText)
                {
                    g_menuText->SetIndex(g_pspSelectedGlow);
                    const float kBaseScale = 0.7f;              // smaller than asset
                    long long ms = (long long)(sceKernelGetSystemTimeWide() / 1000);
                    float ph = (float)(ms % 600) / 600.0f;      // 600 ms pulse period
                    float pulse = 1.0f + 0.06f * sinf(ph * 2.0f * 3.14159265f);
                    g_menuText->ResetTransformation();
                    g_menuText->ScaleAboutCenter(kBaseScale * pulse);
                    g_menuText->SetColour(tColour(255, 255, 0));
                }

                // Camera intro state machine (mirrors CGuiScreenIntroTransition +
                // CGuiScreen::StartTransitionAnimation): play CamAndSet's baked
                // animation window [721..770] once — the menu fly-in — fading the
                // TV frame in over the last 20 frames, then hold the idle pose by
                // not advancing further. CamAndSet's controller is attached lazily
                // on its first Render, so g_camset->GetMultiController() is NULL for
                // the first frame; we start once it's available.
                if (g_camset)
                {
                    tMultiController* mc = g_camset->GetMultiController();
                    if (mc)
                    {
                        if (g_introState == 0)
                        {
                            mc->SetFrameRange(FE_INTRO_START, FE_INTRO_END);
                            mc->Reset();                 // frame 0 (== 721 absolute)
                            g_introState = 1;
                            mlog("scrooby: camera intro start [721..770]");
                        }
                        else if (g_introState == 1)
                        {
                            mc->Advance(deltaMs);
                            float total = mc->GetNumFrames();     // 770-721
                            float cur   = mc->GetFrame();
                            float remaining = total - cur;
                            if (g_tvFrame)
                            {
                                const float FADE = 20.0f;
                                float a = (remaining < FADE) ? (1.0f - remaining / FADE) : 0.0f;
                                if (a < 0.0f) a = 0.0f;
                                if (a > 1.0f) a = 1.0f;
                                a *= a;                  // ease-in, matches original
                                g_tvFrame->SetAlpha(a);
                            }
                            if (cur >= total - 0.5f)
                            {
                                if (g_tvFrame) g_tvFrame->SetAlpha(1.0f);
                                g_introState = 2;        // hold idle pose (stop advancing)
                                mlog("scrooby: camera intro done -> idle");
                            }
                        }
                    }
                }

                // Iris reveal: the attached IrisController advances via the normal
                // Update path; once it opens fully (last frame), hide the iris
                // layer so the black mask is gone for the rest of the menu.
                if (g_irisState == 1 && g_irisMC)
                {
                    if (g_irisMC->LastFrameReached() ||
                        g_irisMC->GetFrame() >= g_irisFrames - 0.5f)
                    {
                        if (g_irisLayer) g_irisLayer->SetVisible(false);
                        g_irisState = 2;
                        mlog("scrooby: iris reveal done");
                    }
                }
            }

            // Phase machine:
            //  PH_BOOTUP   - bootup.p3d loading; DrawFrame shows it once ready.
            //  PH_FRONTEND - bootup screen stays up while frontend.p3d streams
            //                (DrawFrame drives ContinueLoading every frame now).
            //  PH_MENU     - frontend loaded; switch to it and go to MainMenu.
            if (s_fePhase == PH_BOOTUP && s_bootCB.done)
            {
                app->LoadProject("art\\frontend\\scrooby\\frontend.p3d", &s_feCB);
                s_fePhase = PH_FRONTEND;
                mlog("scrooby: bootup up -> streaming frontend");
            }
            else if (s_fePhase == PH_FRONTEND && s_feCB.done && s_feCB.proj)
            {
                app->SetProject(s_feCB.proj);         // current project -> frontend
                s_fePhase = PH_MENU;
                mlog("scrooby: frontend loaded -> PH_MENU");
            }

            // In PH_MENU, keep trying to switch to the MainMenu screen until it
            // resolves — the screen resource may not be ready the exact frame the
            // project's load-complete callback fires.
            if (s_fePhase == PH_MENU)
            {
                // The load-complete callback can report the wrong project (the
                // two-project handoff races at high frame rates), so don't trust
                // it — scan every loaded project for the one that actually has a
                // "MainMenu" screen and switch to it.
                static bool gotoMenuDone = false;
                if (!gotoMenuDone)
                {
                    for (int pi = 0; ; pi++)
                    {
                        Scrooby::Project* pr = app->GetProject(pi);
                        if (!pr) break;
                        Scrooby::Screen* mm = pr->GetScreen("MainMenu");
                        if (mm)
                        {
                            app->SetProject(pr);
                            // The frontend's load-complete callback fired for the
                            // wrong project (two-project race), so this project is
                            // still flagged "loading" and DrawFrame won't draw it.
                            // Force its loaded state now that its screens exist.
                            FeProject* fp = dynamic_cast<FeProject*>(pr);
                            if (fp) fp->OnResourceLoadComplete();
                            pr->GotoScreen(mm, NULL);
                            gotoMenuDone = true;
                            // Grab the multi-string menu label so we can change
                            // the selection text + highlight it (see input/pulse
                            // code). Same lookup the game uses:
                            // GetPage("MainMenu")->GetText("MainMenu").
                            Scrooby::Page* pg = mm->GetPage("MainMenu");
                            if (pg) g_menuText = pg->GetText("MainMenu");
                            // The asset carries both a console ("MainMenu") and a
                            // PC ("MainMenu_PC") label; the game hides the unused
                            // one. We use the console label, so hide the PC variant
                            // (otherwise it draws as a static white string behind).
                            if (pg)
                            {
                                Scrooby::Text* pcLabel = pg->GetText("MainMenu_PC");
                                if (pcLabel) pcLabel->SetVisible(false);
                            }
                            // The level-select sub-menu ("L1 Suburbs" etc.) is
                            // hidden by default in the retail game (it's gated
                            // behind level-selection/debug builds): the screen
                            // does levelPage->GetGroup("Menu")->SetVisible(false).
                            // The harness doesn't run that controller, so hide it
                            // here or it shows as static white text on the menu.
                            {
                                Scrooby::Page* lvlPg = mm->GetPage("Level");
                                if (lvlPg)
                                {
                                    Scrooby::Group* lvlMenu = lvlPg->GetGroup("Menu");
                                    if (lvlMenu) lvlMenu->SetVisible(false);
                                }
                            }
                            // Camera intro / static-set setup: grab CamAndSet
                            // (the room+camera+baked-anim object) and the TV frame
                            // overlay. The TV frame starts hidden and fades in over
                            // the tail of the intro camera move.
                            {
                                Scrooby::Page* pg3d = mm->GetPage("3dFE");
                                if (pg3d) g_camset = pg3d->GetPure3dObject("CamAndSet");
                                Scrooby::Page* pgTv = mm->GetPage("TVFrame");
                                if (pgTv) g_tvFrame = pgTv->GetLayer("TVFrame");
                                // Only hide the TV frame if we have CamAndSet to run
                                // the intro that fades it back in — otherwise it'd
                                // stay invisible forever.
                                if (g_tvFrame && g_camset) g_tvFrame->SetAlpha(0.0f);
                                else g_tvFrame = NULL;
                                g_introState = 0;
                            }
                            // Iris wipe setup (probe + drive). Fetch the IrisCover
                            // page/layer/object and the IrisController animation;
                            // if present, start the iris closed and reveal outward.
                            {
                                Scrooby::Page* pgIris = mm->GetPage("IrisCover");
                                FILE* lf = fopen("ms0:/shar_iris.log", "a");
                                if (pgIris)
                                {
                                    g_irisLayer = pgIris->GetLayer("IrisCover");
                                    if (g_irisLayer) g_irisLayer->SetVisible(true);
                                    g_iris = pgIris->GetPure3dObject("3dIris");
                                    if (g_iris) g_iris->SetZBufferEnabled(false);
                                    g_irisMC = p3d::find<tMultiController>("IrisController");
                                    if (g_irisMC) g_irisFrames = g_irisMC->GetNumFrames();
                                    if (lf) fprintf(lf, "iris: page=%p layer=%p obj=%p mc=%p frames=%.1f\n",
                                        (void*)pgIris, (void*)g_irisLayer, (void*)g_iris,
                                        (void*)g_irisMC, g_irisFrames);
                                }
                                else if (lf) fprintf(lf, "iris: no IrisCover page\n");
                                if (lf) fclose(lf);

                                if (g_iris && g_irisMC && g_irisFrames > 1.0f)
                                {
                                    // Start fully closed (midpoint = black), then
                                    // play to the end so the iris opens to reveal.
                                    g_irisMC->SetCycleMode(FORCE_NON_CYCLIC);
                                    g_irisMC->SetFrameRange(g_irisFrames * 0.5f, g_irisFrames);
                                    g_irisMC->SetFrame(g_irisFrames * 0.5f);
                                    g_irisMC->SetRelativeSpeed(0.5f);
                                    g_iris->SetMultiController(g_irisMC);   // attach -> Update advances it
                                    g_irisState = 1;
                                }
                            }
                            if (g_menuText)
                            {
                                int n = g_menuText->GetNumOfStrings();
                                char mb[96];
                                sprintf(mb, "scrooby: menu label found, %d strings", n);
                                mlog(mb);
                                if (n > 0) g_pspSelectedGlow %= n;
                                g_menuText->SetIndex(g_pspSelectedGlow);
                            }
                            char db[96];
                            sprintf(db, "scrooby: GotoScreen(MainMenu) on project %d (%p)", pi, (void*)pr);
                            mlog(db);
                            break;
                        }
                    }
                }
            }

            if (ctx)
            {
                if (s_fePhase == PH_BOOTUP && !s_bootCB.done)
                {
                    // The bootup screen itself hasn't parsed yet: pulse a colour
                    // so the very first fraction of a second isn't a dead screen.
                    int pv = (frame & 0x3F);
                    if (frame & 0x40) pv = 0x3F - pv;   // triangle 0..63
                    ctx->SetClearColour(tColour(8, 12, 24 + pv));
                }
                else
                {
                    // A Scrooby screen (bootup loading screen or the menu) is
                    // being drawn on top; clear to black behind it.
                    ctx->SetClearColour(tColour(0, 0, 0));
                }

                // The menu's 3D scene objects (FePure3dObject: Homer's living
                // room) render into the context's active tView, taking their own
                // camera from the 'camset' resource. Provide a base view+camera
                // so FePure3dObject::Render has one to work with.
                static tView* feView = NULL;
                if (feView == NULL)
                {
                    tPointCamera* c = new tPointCamera;
                    c->AddRef();
                    c->SetFOV(rmt::DegToRadian(60.0f), 480.0f / 272.0f);
                    c->SetNearPlane(0.1f);
                    c->SetFarPlane(2000.0f);
                    c->SetPosition(rmt::Vector(0, 0, -5));
                    c->SetTarget(rmt::Vector(0, 0, 0));
                    feView = new tView;
                    feView->AddRef();
                    feView->SetCamera(c);
                }
                p3d::context->SetView(feView);

                ctx->BeginFrame();            // clears (ctx clear mask/colour set)
                // Draw the cached 3D room as the frame backdrop (behind
                // everything) once it has been captured. The camset object then
                // skips its ~280-mesh live render; only animated objects (Homer)
                // draw live over this. Big FPS win for the menu.
                pglDrawRoomBackdrop();
                // DrawFrame pumps ContinueLoading() until the project is loaded,
                // then draws the current screen (FeScreen sets its own 2D camera).
                Scrooby::App::GetInstance()->DrawFrame(deltaMs);
                ctx->EndFrame(true);
            }


            frame++;
            continue;
        }

        // --- drive the load sequence (global.p3d, then test.p3d) ------------
        // Read the current file's state only until it is done; the completion
        // callback (fired by SwitchTask below) frees 'req', so polling it after
        // that is a use-after-free.
        if (req && !curFileDone)
        {
            radLoadState ls = req->GetState();
            if (ls == COMPLETE || ls == CANCELED) curFileDone = true;
        }

        // Pump file IO so the worker's reads complete (during loading).
        radFileService();

        // When the current file is done, pump SwitchTask a few frames so its
        // completion callback (tLoadRequest::InternalCallback::Done) Dumps the
        // loaded objects into p3d::inventory. Only THEN start the next file, so
        // the model's shaders can resolve textures the shared file just added.
        if (curFileDone && !allLoaded)
        {
            p3d::loadManager->SwitchTask();
            if (++flushCount >= 4)
            {
                fileIdx++;
                flushCount  = 0;
                curFileDone = false;
                if (fileIdx < kNumFiles)
                {
                    req = startLoad(kFiles[fileIdx]);   // next file (test.p3d)
                }
                else
                {
                    allLoaded = true;
                    req = NULL;
                    mlog("all files loaded");
                }
            }
        }
        // Yield CPU to the load worker (PSP is strict-priority).
        sceKernelDelayThread(2000);   // 2 ms

        // state: 1 = everything loaded, 2 = still loading
        int state = allLoaded ? 1 : 2;

        // --- one-time scene build once the inventory is populated -----------
        if (state == 1 && !sceneReady && ctx)
        {
            // Collect every tGeometry the chunk loaders put in the inventory.
            // The Dump callback may not have run yet on the first COMPLETE
            // frame, so retry until geometry appears (or we give up).
            nGeos = 0;
            tInventory::Iterator<tGeometry> it;
            for (tGeometry* g = it.First(); g && nGeos < MAX_GEOS; g = it.Next())
            {
                geos[ nGeos++ ] = g;
            }

            buildTries++;
            if (nGeos == 0 && buildTries < 120)
            {
                // inventory not populated yet — try again next frame
                goto after_scene_build;
            }
            { char b[48]; sprintf(b, "geometries found: %d (tries=%d)", nGeos, buildTries); mlog(b); }

            // If the file carries a scene graph, it positions the meshes at
            // their correct transforms (car body / doors / wheels). Prefer it
            // over the flat "draw every mesh at the origin" fallback.
            {
                tInventory::Iterator<Scenegraph::Scenegraph> sit;
                scene = sit.First();
            }
            { char b[48]; sprintf(b, "scenegraph: %s", scene ? "FOUND" : "none"); mlog(b); }

            // Characters + vehicles are CompositeDrawables (skeleton + props/skins).
            {
                tInventory::Iterator<tCompositeDrawable> cit;
                composite = cit.First();
            }
            { char b[48]; sprintf(b, "composite: %s", composite ? "FOUND" : "none"); mlog(b); }

            // Frame the camera to the union of the geometries' bounds. Prefer
            // the bounding box; fall back to the bounding sphere if the box is
            // degenerate (no BOX chunk in the mesh).
            rmt::Vector lo(  1e18f,  1e18f,  1e18f );
            rmt::Vector hi( -1e18f, -1e18f, -1e18f );
            bool haveBounds = false;
            for (int i = 0; i < nGeos; i++)
            {
                rmt::Box3D  bx; geos[i]->GetBoundingBox( &bx );
                rmt::Vector clo = bx.low, chi = bx.high;
                if ( chi.x - clo.x < 1e-4f && chi.y - clo.y < 1e-4f && chi.z - clo.z < 1e-4f )
                {
                    rmt::Sphere s; geos[i]->GetBoundingSphere( &s );
                    clo.Set( s.centre.x - s.radius, s.centre.y - s.radius, s.centre.z - s.radius );
                    chi.Set( s.centre.x + s.radius, s.centre.y + s.radius, s.centre.z + s.radius );
                }
                if ( clo.x < lo.x ) lo.x = clo.x;
                if ( clo.y < lo.y ) lo.y = clo.y;
                if ( clo.z < lo.z ) lo.z = clo.z;
                if ( chi.x > hi.x ) hi.x = chi.x;
                if ( chi.y > hi.y ) hi.y = chi.y;
                if ( chi.z > hi.z ) hi.z = chi.z;
                haveBounds = true;
            }
            if (haveBounds)
            {
                sceneCentre.Set( (lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f );
                rmt::Vector diag; diag.Sub( hi, sceneCentre );
                sceneRadius = diag.Magnitude();
            }
            if ( !(sceneRadius > 0.01f && sceneRadius < 1e7f) )   // guard degenerate bounds
            {
                sceneCentre.Set( 0, 0, 0 );
                sceneRadius = 10.0f;
            }
            {
                char b[96];
                sprintf(b, "scene centre=(%.2f,%.2f,%.2f) r=%.2f",
                        sceneCentre.x, sceneCentre.y, sceneCentre.z, sceneRadius);
                mlog(b);
            }

            cam = new tPointCamera;
            cam->AddRef();
            cam->SetFOV( rmt::DegToRadian(60.0f), 480.0f / 272.0f );
            cam->SetNearPlane( sceneRadius * 0.02f + 0.05f );
            cam->SetFarPlane ( sceneRadius * 8.0f  + 100.0f );

            view = new tView;
            view->AddRef();
            view->SetCamera( cam );
            view->SetClearColour( tColour(24, 24, 40) );          // dark slate => "rendering" state
            view->SetClearMask( PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH );
            view->SetAmbientLight( tColour(255, 255, 255) );      // full ambient (no lights loaded)

            ctx->SetClearMask( 0 );   // the view owns the clear now; avoid a double clear
            sceneReady = true;
            mlog("scene ready");
        }
    after_scene_build:

        // --- draw -----------------------------------------------------------
        if (ctx && sceneReady)
        {
            // Orbit the camera so the result is unmistakably 3D (proves depth,
            // projection and the view matrix are all live). Pull back a little
            // extra (1.8) since scene-graph assembly spreads parts wider than
            // their individual local bounding boxes suggest.
            // Was float(frame)*0.02f (≈1.2 rad/s at 60fps); integrate real time
            // so the orbit rate is the same on PSP (20fps) and PPSSPP (60fps).
            orbitAng += deltaMs * 0.001f * 1.2f;
            float ang  = orbitAng;
            float dist = sceneRadius / rmt::Sin( rmt::DegToRadian(30.0f) ) * 1.8f;   // fit half-FOV
            rmt::Vector pos;
            pos.Set( sceneCentre.x + rmt::Sin(ang) * dist,
                     sceneCentre.y + sceneRadius * 0.5f,
                     sceneCentre.z + rmt::Cos(ang) * dist );
            cam->SetPosition( pos );
            cam->SetTarget( sceneCentre );

            ctx->BeginFrame();
            view->BeginRender();
            if (composite)
            {
                // Character/vehicle: evaluates the skeleton bind pose, then draws
                // each rigid prop at its joint and each skinned mesh (CPU-skinned).
                composite->Display();
            }
            else if (scene)
            {
                // Traverses the transform hierarchy, drawing each mesh at its
                // correct place (body / doors / wheels assembled).
                scene->Display();
            }
            else
            {
                // No container: draw every mesh at the origin (static prop).
                for (int i = 0; i < nGeos; i++)
                    geos[i]->Display();
            }
            view->EndRender();
            ctx->EndFrame(true);
        }
        else if (ctx)
        {
            // Pre-load feedback: pulse by load state (blue=loading, red=failed).
            tColour c;
            if (state == 0) c.Set(64 + pulse * 3, 0, 0);
            else            c.Set(0, 0, 64 + pulse * 3);
            ctx->SetClearColour(c);
            ctx->BeginFrame();
            ctx->EndFrame(true);
        }

        if (frame == 1)  mlog("first frame rendered");
        if (state == 1 && !loggedDone) { mlog("load COMPLETE"); loggedDone = true; }
        if ((frame % 180) == 0)
        {
            char buf[64];
            sprintf(buf, "frame %u: state=%d nGeos=%d ready=%d", frame, state, nGeos, (int)sceneReady);
            mlog(buf);
        }
        frame++;
    }

    sceKernelExitGame();
    return 0;
}
