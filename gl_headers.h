#ifndef GL_HEADERS_H
#define GL_HEADERS_H

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
#include <GL/glew.h>
#if defined(_WIN32)
#include <GL/wglew.h>
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
