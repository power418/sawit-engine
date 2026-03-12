#include "graphics_backend.h"

#include <stdlib.h>
#include <string.h>

static int graphics_backend_equals_ignore_case(const char* a, const char* b);

GraphicsBackend graphics_backend_get_default(void)
{
#if defined(__APPLE__)
  return GRAPHICS_BACKEND_METAL;
#else
  return GRAPHICS_BACKEND_OPENGL;
#endif
}

GraphicsBackend graphics_backend_resolve_requested(void)
{
  const char* environment_value = getenv("OPENGL_SKY_BACKEND");

  if (environment_value == NULL || environment_value[0] == '\0')
  {
    return graphics_backend_get_default();
  }

  if (graphics_backend_equals_ignore_case(environment_value, "opengl"))
  {
    return GRAPHICS_BACKEND_OPENGL;
  }
  if (graphics_backend_equals_ignore_case(environment_value, "metal"))
  {
    return GRAPHICS_BACKEND_METAL;
  }

  return graphics_backend_get_default();
}

const char* graphics_backend_get_name(GraphicsBackend backend)
{
  switch (backend)
  {
    case GRAPHICS_BACKEND_METAL:
      return "Metal";
    case GRAPHICS_BACKEND_OPENGL:
    default:
      return "OpenGL";
  }
}

int graphics_backend_is_supported_on_platform(GraphicsBackend backend)
{
#if defined(__APPLE__)
  return backend == GRAPHICS_BACKEND_METAL;
#else
  return backend == GRAPHICS_BACKEND_OPENGL;
#endif
}

int graphics_backend_build_error_message(GraphicsBackend backend, char* out_message, size_t out_message_size)
{
  const char* message = "Unsupported graphics backend.";

  if (out_message == NULL || out_message_size == 0U)
  {
    return 0;
  }

#if defined(__APPLE__)
  if (backend == GRAPHICS_BACKEND_OPENGL)
  {
    message = "Cocoa backend sekarang dikunci ke Metal-only. OpenGL untuk macOS dinonaktifkan.";
  }
  else
  {
    message = "Backend Metal dipilih untuk Cocoa/macOS, tapi renderer Metal belum diimplementasikan di project ini.";
  }
#else
  if (backend == GRAPHICS_BACKEND_METAL)
  {
    message = "Backend Metal/Cocoa hanya didukung di macOS. Untuk Windows/Linux gunakan backend OpenGL/GNU.";
  }
  else
  {
    message = "Backend yang dipilih tidak cocok dengan platform ini.";
  }
#endif

  if (strlen(message) + 1U > out_message_size)
  {
    return 0;
  }

  memcpy(out_message, message, strlen(message) + 1U);
  return 1;
}

static int graphics_backend_equals_ignore_case(const char* a, const char* b)
{
  size_t index = 0U;

  if (a == NULL || b == NULL)
  {
    return 0;
  }

  while (a[index] != '\0' && b[index] != '\0')
  {
    char left = a[index];
    char right = b[index];

    if (left >= 'A' && left <= 'Z')
    {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z')
    {
      right = (char)(right - 'A' + 'a');
    }
    if (left != right)
    {
      return 0;
    }
    ++index;
  }

  return a[index] == '\0' && b[index] == '\0';
}
