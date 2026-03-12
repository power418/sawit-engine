#include "platform_support.h"

#include <stdio.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

int platform_support_get_executable_path(char* out_path, size_t out_path_size)
{
  if (out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

#if defined(_WIN32)
  {
    const DWORD length = GetModuleFileNameA(NULL, out_path, (DWORD)out_path_size);
    if (length == 0U || length >= (DWORD)out_path_size)
    {
      out_path[0] = '\0';
      return 0;
    }
  }
  return 1;
#else
  {
    uint32_t size = (uint32_t)out_path_size;
    if (_NSGetExecutablePath(out_path, &size) != 0)
    {
      out_path[0] = '\0';
      return 0;
    }
  }
  return 1;
#endif
}

int platform_support_get_current_directory(char* out_path, size_t out_path_size)
{
  if (out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

#if defined(_WIN32)
  if (GetCurrentDirectoryA((DWORD)out_path_size, out_path) == 0U)
  {
    out_path[0] = '\0';
    return 0;
  }
#else
  if (getcwd(out_path, out_path_size) == NULL)
  {
    out_path[0] = '\0';
    return 0;
  }
#endif

  return 1;
}

int platform_support_file_exists(const char* path)
{
  if (path == NULL || path[0] == '\0')
  {
    return 0;
  }

#if defined(_WIN32)
  {
    const DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
      return 0;
    }

    return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U;
  }
#else
  {
    struct stat status = { 0 };
    if (stat(path, &status) != 0)
    {
      return 0;
    }

    return S_ISREG(status.st_mode);
  }
#endif
}

void platform_support_sleep_ms(unsigned int milliseconds)
{
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  struct timespec duration = {
    (time_t)(milliseconds / 1000U),
    (long)((milliseconds % 1000U) * 1000000UL)
  };
  while (nanosleep(&duration, &duration) != 0)
  {
  }
#endif
}

void platform_support_show_error_dialog(const char* title, const char* message)
{
  const char* safe_title = (title != NULL) ? title : "Error";
  const char* safe_message = (message != NULL) ? message : "Unknown error";

#if defined(_WIN32)
  (void)MessageBoxA(NULL, safe_message, safe_title, MB_ICONERROR | MB_OK);
#else
  (void)fprintf(stderr, "%s: %s\n", safe_title, safe_message);
#endif
}
