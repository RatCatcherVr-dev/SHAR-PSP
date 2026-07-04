cmake_minimum_required(VERSION 3.16)
project(shar_psp CXX)

# Step-2 bring-up: portable core of SHAR (radmath + radcore core) for PSP/MIPS.
# Generated from a compile-sweep of which sources build cleanly with psp-g++.
# Platform backends (file IO, controller, sockets, profiler) are intentionally
# excluded here; they need PSP implementations (sceIo/pspctrl/sceAudio) later.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O2 -G0 -Wall)
add_compile_definitions(RAD_RELEASE RAD_PSP NDEBUG)

# PSP diagnostic logging (ms0:/shar_*.log) is OFF by default: each fopen/append/
# close to the Memory Stick costs several ms and, emitted per-asset during a
# load, dominates load time (see psp/psplog.h). Force-include the shim into
# every TU so the "ms0:/shar_*.log" opens (rewritten to pspDiagFopen) become
# NULL no-ops. Set PSP_DIAG_LOG=1 to restore instrumentation.
add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/psplog.h)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(
    ${ROOT}/src/libs/radcore/include
    ${ROOT}/src/libs/radcore/src
    ${ROOT}/src/game
    ${ROOT}/src/libs/radmath
    ${ROOT}/src/libs/pure3d
    ${ROOT}/src/libs/radcontent
    ${ROOT}/src/libs/radcontent/include
    ${ROOT}/src/libs/scrooby/include
    ${ROOT}/src/libs/scrooby/src
    ${ROOT}/deps/include
)

add_library(radmath_psp STATIC
    ${ROOT}/src/libs/radmath/radmath/matrix.cpp
    ${ROOT}/src/libs/radmath/radmath/numerical.cpp
    ${ROOT}/src/libs/radmath/radmath/plane.cpp
    ${ROOT}/src/libs/radmath/radmath/quaternion.cpp
    ${ROOT}/src/libs/radmath/radmath/radmath.cpp
    ${ROOT}/src/libs/radmath/radmath/random.cpp
    ${ROOT}/src/libs/radmath/radmath/spline.cpp
    ${ROOT}/src/libs/radmath/radmath/triangle.cpp
    ${ROOT}/src/libs/radmath/radmath/trig.cpp
    ${ROOT}/src/libs/radmath/radmath/vector.cpp
)

add_library(radcore_psp STATIC
    ${ROOT}/src/libs/radcore/src/pch/pch.cpp
    ${ROOT}/src/libs/radcore/src/radcontroller/controllerbuffer.cpp
    ${ROOT}/src/libs/radcore/src/radcontroller/directinputcontroller.cpp
    ${ROOT}/src/libs/radcore/src/radcontroller/sdlcontroller.cpp
    ${ROOT}/src/libs/radcore/src/raddebug/debug.cpp
    ${ROOT}/src/libs/radcore/src/raddebugcommunication/host.cpp
    ${ROOT}/src/libs/radcore/src/raddebugcommunication/hostchannel.cpp
    ${ROOT}/src/libs/radcore/src/raddebugfileserver/fileserver.cpp
    ${ROOT}/src/libs/radcore/src/raddebugwatch/watchclient.cpp
    ${ROOT}/src/libs/radcore/src/raddispatch/dispatcher.cpp
    ${ROOT}/src/libs/radcore/src/radkey/radkey.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/align.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/platalloc.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/externalmemoryobject.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/memorymanager.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/memoryobject.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/memorypool.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/memoryspacewin32.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/staticheap.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/trackingheap.cpp
    ${ROOT}/src/libs/radcore/src/radmemory/twowayallocator.cpp
    ${ROOT}/src/libs/radcore/src/radobject/object.cpp
    ${ROOT}/src/libs/radcore/src/radobjectbtree/objectavltree.cpp
    ${ROOT}/src/libs/radcore/src/radobjectbtree/objectbtree.cpp
    ${ROOT}/src/libs/radcore/src/radobjectlist/dynamicarray.cpp
    ${ROOT}/src/libs/radcore/src/radobjectlist/objectlist.cpp
    ${ROOT}/src/libs/radcore/src/radobjectlist/weakcallbackwrapper.cpp
    ${ROOT}/src/libs/radcore/src/radobjectlist/weakinterfacewrapper.cpp
    ${ROOT}/src/libs/radcore/src/radprofiler/microprofile.cpp
    ${ROOT}/src/libs/radcore/src/radremotecommand/functionlist.cpp
    ${ROOT}/src/libs/radcore/src/radremotecommand/remotecommandserver.cpp
    ${ROOT}/src/libs/radcore/src/radremotecommand/remotecommandtarget.cpp
    ${ROOT}/src/libs/radcore/src/radstacktrace/win32/stacktrace.cpp
    ${ROOT}/src/libs/radcore/src/radstats/simplestat.cpp
    ${ROOT}/src/libs/radcore/src/radstats/statmanager.cpp
    ${ROOT}/src/libs/radcore/src/radstring/string.cpp
    ${ROOT}/src/libs/radcore/src/radthread/mutex.cpp
    ${ROOT}/src/libs/radcore/src/radthread/semaphore.cpp
    ${ROOT}/src/libs/radcore/src/radthread/system.cpp
    ${ROOT}/src/libs/radcore/src/radthread/thread.cpp
    # radtime: std::chrono-based, portable. radTime64 typedef guarded for RAD_PSP.
    ${ROOT}/src/libs/radcore/src/radtime/time.cpp
    ${ROOT}/src/libs/radcore/src/radtime/stopwatch.cpp
    # radfile: platform-independent async framework (common/) + PSP stdio backend.
    ${ROOT}/src/libs/radcore/src/radfile/common/radfile.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/drive.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/drivethread.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/file.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/filecache.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/filesystem.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/instancedrive.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/platformdrives.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/requests.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/signeddrive.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/buffereddrive.cpp
    ${ROOT}/src/libs/radcore/src/radfile/common/cementer.cpp
    ${ROOT}/src/libs/radcore/src/radfile/psp/pspdrives.cpp
)

# pddi/gl renderer backend (engine's fixed-function GL path) + PSP EGL display.
add_library(pddigl_psp STATIC
    ${ROOT}/src/libs/pure3d/pddi/base/base_font.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/basecontext.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/baseshader.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/pddiobj.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/decompress.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/glmat.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/gltex.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/gldev.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/glcon.cpp
    ${ROOT}/src/libs/pure3d/pddi/gl/display_psp/gldisplay.cpp
)

# Native PSP sceGU renderer backend. Drives the GE directly (one sceGuDrawArray
# per prim group, native CLUT) instead of pspGL's per-draw shim. Swappable at
# the single pddiCreate() entry point — link this INSTEAD of pddigl_psp.
add_library(pddigu_psp STATIC
    ${ROOT}/src/libs/pure3d/pddi/base/base_font.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/basecontext.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/baseshader.cpp
    ${ROOT}/src/libs/pure3d/pddi/base/pddiobj.cpp
    ${ROOT}/src/libs/pure3d/pddi/psp/gudev.cpp
    ${ROOT}/src/libs/pure3d/pddi/psp/gudisplay.cpp
    ${ROOT}/src/libs/pure3d/pddi/psp/gucon.cpp
    ${ROOT}/src/libs/pure3d/pddi/psp/gutex.cpp
    ${ROOT}/src/libs/pure3d/pddi/psp/gumat.cpp
)

# radcontent / radload — the radLoadStream base of p3d's tFile + load manager.
add_library(radcontent_psp STATIC
    ${ROOT}/src/libs/radcontent/src/radload/hashtable.cpp
    ${ROOT}/src/libs/radcontent/src/radload/inventory.cpp
    ${ROOT}/src/libs/radcontent/src/radload/manager.cpp
    ${ROOT}/src/libs/radcontent/src/radload/object.cpp
    ${ROOT}/src/libs/radcontent/src/radload/queue.cpp
    ${ROOT}/src/libs/radcontent/src/radload/request.cpp
    ${ROOT}/src/libs/radcontent/src/radload/stream.cpp
)

# Pure3D asset + scene layer (p3d/). Minimal set to load a .p3d and render a
# static textured tGeometry. Heavier/optional subsystems (lights chooser,
# shadow, billboards, sprites, fonts, debugdraw) are intentionally excluded.
add_library(pure3d_p3d_psp STATIC
    # context / memory / core entity
    ${ROOT}/src/libs/pure3d/p3d/context.cpp
    ${ROOT}/src/libs/pure3d/p3d/utility.cpp
    ${ROOT}/src/libs/pure3d/p3d/memory.cpp
    ${ROOT}/src/libs/pure3d/p3d/memheap.cpp
    ${ROOT}/src/libs/pure3d/p3d/memorysection.cpp
    ${ROOT}/src/libs/pure3d/p3d/refcounted.cpp
    ${ROOT}/src/libs/pure3d/p3d/entity.cpp
    ${ROOT}/src/libs/pure3d/p3d/error.cpp
    ${ROOT}/src/libs/pure3d/p3d/inventory.cpp
    # file + chunk loading
    ${ROOT}/src/libs/pure3d/p3d/chunkfile.cpp
    ${ROOT}/src/libs/pure3d/p3d/file.cpp
    ${ROOT}/src/libs/pure3d/p3d/fileftt.cpp
    ${ROOT}/src/libs/pure3d/p3d/loadmanager.cpp
    ${ROOT}/src/libs/pure3d/p3d/loaders.cpp
    # geometry render path
    ${ROOT}/src/libs/pure3d/p3d/drawable.cpp
    ${ROOT}/src/libs/pure3d/p3d/geometry.cpp
    ${ROOT}/src/libs/pure3d/p3d/primgroup.cpp
    ${ROOT}/src/libs/pure3d/p3d/vertexlist.cpp
    ${ROOT}/src/libs/pure3d/p3d/shader.cpp
    ${ROOT}/src/libs/pure3d/p3d/texture.cpp
    # PSP platform backend (tPlatform — context creation, friend of tContext)
    ${ROOT}/src/libs/pure3d/p3d/platform/psp/platform.cpp
    # view / camera / matrix
    ${ROOT}/src/libs/pure3d/p3d/matrixstack.cpp
    ${ROOT}/src/libs/pure3d/p3d/view.cpp
    ${ROOT}/src/libs/pure3d/p3d/camera.cpp
    ${ROOT}/src/libs/pure3d/p3d/pointcamera.cpp
    # scene graph — assembles multi-mesh models (vehicles) via a transform
    # hierarchy. scenegraph.cpp also defines Camera/LightGroup/Attachment nodes,
    # which drag in light + pose/skeleton deps below.
    ${ROOT}/src/libs/pure3d/p3d/scenegraph/scenegraph.cpp
    ${ROOT}/src/libs/pure3d/p3d/displaylist.cpp
    ${ROOT}/src/libs/pure3d/p3d/light.cpp
    ${ROOT}/src/libs/pure3d/p3d/lightloader.cpp       # tLightLoader / tLightGroupLoader
    ${ROOT}/src/libs/pure3d/p3d/pointlight.cpp        # scene light subclasses
    ${ROOT}/src/libs/pure3d/p3d/spotlight.cpp
    ${ROOT}/src/libs/pure3d/p3d/directionallight.cpp
    ${ROOT}/src/libs/pure3d/p3d/ambientlight.cpp
    ${ROOT}/src/libs/pure3d/p3d/cameraloader.cpp      # tCameraLoader (camset framing)
    ${ROOT}/src/libs/pure3d/p3d/vectorcamera.cpp      # tVectorCamera (created by cameraloader)
    ${ROOT}/src/libs/pure3d/p3d/anim/drawablepose.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/pose.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/skeleton.cpp
    # composite drawable — assembles characters + vehicles from a skeleton with
    # rigid props (attached to joints) and skinned meshes (tPolySkin, CPU-skinned
    # via primgroup.cpp). Bind-pose render needs no animation/multicontroller.
    ${ROOT}/src/libs/pure3d/p3d/anim/compositedrawable.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/polyskin.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/expressionoffsets.cpp
    # fonts + text (needed by the Scrooby frontend menu — FeText draws via tFont).
    # tTextureFont/tImageFont render through pddiPrimStream (already on pddi).
    ${ROOT}/src/libs/pure3d/p3d/font.cpp
    ${ROOT}/src/libs/pure3d/p3d/texturefont.cpp
    ${ROOT}/src/libs/pure3d/p3d/textstring.cpp
    ${ROOT}/src/libs/pure3d/p3d/imagefont.cpp
    ${ROOT}/src/libs/pure3d/p3d/unicode.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/multicontroller.cpp
    # animation subsystem — lets the frontend menu's 3D room scene (animated
    # "gag" characters, glows, TV, racecar) load and play. animate.cpp is the
    # core (tAnimation/tAnimationLoader + the tFrameController hierarchy +
    # tFrameControllerLoader); the frontend-used frame-controller types are
    # pose (PTRN), texture (TEX), billboard (BQG), scenegraph transform/vis
    # (STRN/SVIS) and pose-visibility (PVIS). Unused types (effect, vertex,
    # expression, camera, light, shader anim) are #ifdef'd out under RAD_PSP.
    ${ROOT}/src/libs/pure3d/p3d/anim/animate.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/channel.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/event.cpp        # tBaseEvent/tGenericEvent (event channels)
    ${ROOT}/src/libs/pure3d/p3d/anim/poseanimation.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/cameraanimation.cpp  # tCameraAnimationController (menu camera intro)
    ${ROOT}/src/libs/pure3d/p3d/anim/textureanimation.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/visibilityanimation.cpp
    ${ROOT}/src/libs/pure3d/p3d/anim/billboardobjectanimation.cpp
    ${ROOT}/src/libs/pure3d/p3d/scenegraph/nodeanimation.cpp
    # billboard quad groups (the menu's glowing collectible sprites) + locators
    ${ROOT}/src/libs/pure3d/p3d/billboardobject.cpp
    ${ROOT}/src/libs/pure3d/p3d/locator.cpp
    # image decode pipeline (textures)
    ${ROOT}/src/libs/pure3d/p3d/image.cpp
    ${ROOT}/src/libs/pure3d/p3d/imageconverter.cpp
    ${ROOT}/src/libs/pure3d/p3d/imagefactory.cpp
    ${ROOT}/src/libs/pure3d/p3d/rawimage.cpp
    ${ROOT}/src/libs/pure3d/p3d/mipmapfilter.cpp
    ${ROOT}/src/libs/pure3d/p3d/png.cpp
    ${ROOT}/src/libs/pure3d/p3d/bmp.cpp
    ${ROOT}/src/libs/pure3d/p3d/targa.cpp
    ${ROOT}/src/libs/pure3d/p3d/dxtn.cpp
    ${ROOT}/src/libs/pure3d/p3d/lzr.cpp
    # tSprite — referenced by imagefactory's image->sprite path
    ${ROOT}/src/libs/pure3d/p3d/sprite.cpp
    # tUidUnaligned — unaligned UID reads used by the inventory/chunk loader
    ${ROOT}/src/game/main/tuidunaligned.cpp
)

# Scrooby — the frontend/UI engine (menus). Runtime subset (33 files) from
# ScroobyLib.vcxproj. Depends only on pure3d/pddi + radcore/radmath.
add_library(scrooby_psp STATIC
    ${ROOT}/src/libs/scrooby/src/FeApp.cpp
    ${ROOT}/src/libs/scrooby/src/FeBoundedDrawable.cpp
    ${ROOT}/src/libs/scrooby/src/FeChunkHandler.cpp
    ${ROOT}/src/libs/scrooby/src/FeDrawable.cpp
    ${ROOT}/src/libs/scrooby/src/FeEntity.cpp
    ${ROOT}/src/libs/scrooby/src/FeGroup.cpp
    ${ROOT}/src/libs/scrooby/src/FeLanguage.cpp
    ${ROOT}/src/libs/scrooby/src/FeLayer.cpp
    ${ROOT}/src/libs/scrooby/src/FeLoaders.cpp
    ${ROOT}/src/libs/scrooby/src/FeMovie.cpp
    ${ROOT}/src/libs/scrooby/src/FeOwner.cpp
    ${ROOT}/src/libs/scrooby/src/FePage.cpp
    ${ROOT}/src/libs/scrooby/src/FeParent.cpp
    ${ROOT}/src/libs/scrooby/src/FePolygon.cpp
    ${ROOT}/src/libs/scrooby/src/FeProject.cpp
    ${ROOT}/src/libs/scrooby/src/FePure3dObject.cpp
    ${ROOT}/src/libs/scrooby/src/FeScreen.cpp
    ${ROOT}/src/libs/scrooby/src/FeSprite.cpp
    ${ROOT}/src/libs/scrooby/src/FeText.cpp
    ${ROOT}/src/libs/scrooby/src/FeTextBible.cpp
    ${ROOT}/src/libs/scrooby/src/FeTextStyle.cpp
    ${ROOT}/src/libs/scrooby/src/ResourceManager/FeResourceManager.cpp
    ${ROOT}/src/libs/scrooby/src/scrooby/App.cpp
    ${ROOT}/src/libs/scrooby/src/strings/pcstring.cpp
    ${ROOT}/src/libs/scrooby/src/strings/stricmp.cpp
    ${ROOT}/src/libs/scrooby/src/strings/UnicodeString.cpp
    ${ROOT}/src/libs/scrooby/src/tLinearTable.cpp
    ${ROOT}/src/libs/scrooby/src/utility/AsyncFileLoader.cpp
    ${ROOT}/src/libs/scrooby/src/utility/BufferReader.cpp
    ${ROOT}/src/libs/scrooby/src/utility/debugMessages.cpp
    ${ROOT}/src/libs/scrooby/src/utility/EnumConversion.cpp
    ${ROOT}/src/libs/scrooby/src/xml/XMLParser.cpp
    ${ROOT}/src/libs/scrooby/src/xml/XMLTree.cpp
)

#=============================================================================
# Test EBOOT — proves Phases 2-3 LINK and runs the engine against real assets.
#=============================================================================
include($ENV{PSPDEV}/psp/share/CreatePBP.cmake)

add_executable(shartest harness/main.cpp)
target_include_directories(shartest PRIVATE
    ${ROOT}/src/libs/radcore/include
    ${ROOT}/src/libs/radcore/src
    ${ROOT}/src/libs/radmath
    ${ROOT}/src/libs/pure3d
    ${ROOT}/src/libs/radcontent
    ${ROOT}/src/libs/radcontent/include
    ${ROOT}/deps/include
)

# Interdependent static libs → wrap in a link group so ld resolves cycles.
target_link_libraries(shartest
    -Wl,--start-group
    scrooby_psp
    pure3d_p3d_psp
    pddigu_psp
    radcontent_psp
    radcore_psp
    radmath_psp
    -Wl,--end-group
    GL pspvram pspvfpu pspgu pspgum pspge pspdisplay
    SDL2
    png16 z
    pspctrl psphprm pspaudio psputility psppower
    m stdc++
)

create_pbp_file(
    TARGET shartest
    TITLE "SHAR Pure3D Test"
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/EBOOT
)
