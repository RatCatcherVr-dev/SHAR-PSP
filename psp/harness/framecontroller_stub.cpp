//=============================================================================
// PSP link stub for tFrameController.
//
// tMultiController (multicontroller.cpp) is referenced by Scrooby's
// FeResourceManager / FePure3dObject for OPTIONAL animated "gag" objects in the
// frontend. Its base class tFrameController is defined in anim/animate.cpp —
// but compiling that whole translation unit drags in the entire animation
// controller subsystem (pose/light/camera/shader/vertex/expression/billboard/
// visibility controllers + tAnimatedObject/tVectorCamera/tBillboardQuadGroup/
// tExpressionMixer, and their dependencies).
//
// A static frontend render never instantiates a tMultiController: we register
// no animation loader, so p3d::find<tMultiController>() returns NULL and gag
// objects display statically. We therefore only need tFrameController's
// out-of-line symbols (its ctors, dtor, and hence its vtable/typeinfo) to
// satisfy the linker. These bodies mirror anim/animate.cpp exactly and never
// execute at runtime.
//=============================================================================

#include <p3d/anim/animate.hpp>

tFrameController::tFrameController() :
    tEntity()
{
}

tFrameController::tFrameController(tFrameController* c) :
    tEntity()
{
    CopyName(c);
}

tFrameController::~tFrameController()
{
}
