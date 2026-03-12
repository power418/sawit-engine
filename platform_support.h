#ifndef PLATFORM_SUPPORT_H
#define PLATFORM_SUPPORT_H

#include <stddef.h>

#if defined(_WIN32)
#define PLATFORM_PATH_MAX 260
#else
#include <limits.h>
#if defined(PATH_MAX)
#define PLATFORM_PATH_MAX PATH_MAX
#else
#define PLATFORM_PATH_MAX 4096
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

int platform_support_get_executable_path(char* out_path, size_t out_path_size);
int platform_support_get_current_directory(char* out_path, size_t out_path_size);
int platform_support_file_exists(const char* path);
void platform_support_sleep_ms(unsigned int milliseconds);
void platform_support_show_error_dialog(const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif
