//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


// stub OpenGL header, all pddi gl code uses this instead of '#include <GL/gl.h>
#if defined(RAD_PSP)
// PSP: pspGL exposes GL1.x symbols directly (no loader). glext brings the
// VBO / blend-equation entry points the desktop path gets via glad.
// GL_GLEXT_PROTOTYPES unhides the VBO (GL 1.5) prototypes; pspGL defines them.
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <glad/glad.h>
#endif