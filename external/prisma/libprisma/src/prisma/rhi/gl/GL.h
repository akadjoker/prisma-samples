#pragma once

#ifdef PRISMA_GLES
#include "prisma/rhi/gl/loader/OpenGLES3.h"
using namespace gles;
#else
#include "prisma/rhi/gl/loader/OpenGL.h"
#endif
