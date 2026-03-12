#ifndef GRAPHICS_BACKEND_H
#define GRAPHICS_BACKEND_H

#include <stddef.h>

typedef enum GraphicsBackend
{
  GRAPHICS_BACKEND_OPENGL = 0,
  GRAPHICS_BACKEND_METAL,
  GRAPHICS_BACKEND_COUNT
} GraphicsBackend;

GraphicsBackend graphics_backend_get_default(void);
GraphicsBackend graphics_backend_resolve_requested(void);
const char* graphics_backend_get_name(GraphicsBackend backend);
int graphics_backend_is_supported_on_platform(GraphicsBackend backend);
int graphics_backend_build_error_message(GraphicsBackend backend, char* out_message, size_t out_message_size);

#endif
