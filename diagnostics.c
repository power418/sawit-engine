#include "diagnostics.h"
#include "platform_support.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
  DIAGNOSTICS_RECENT_MESSAGE_COUNT = 14,
  DIAGNOSTICS_RECENT_MESSAGE_LENGTH = 192
};

static char g_diagnostics_recent_messages[DIAGNOSTICS_RECENT_MESSAGE_COUNT][DIAGNOSTICS_RECENT_MESSAGE_LENGTH];
static int g_diagnostics_recent_write_index = 0;
static int g_diagnostics_recent_count = 0;

static int diagnostics_build_log_path(char* out_path, size_t out_path_size)
{
  char* last_separator = NULL;

  if (!platform_support_get_executable_path(out_path, out_path_size))
  {
    return 0;
  }

  last_separator = strrchr(out_path, '\\');
  if (last_separator == NULL)
  {
    last_separator = strrchr(out_path, '/');
  }

  if (last_separator == NULL)
  {
    return 0;
  }

  last_separator[1] = '\0';
  if (strlen(out_path) + strlen("opengl_sky.log") + 1U > out_path_size)
  {
    return 0;
  }

  (void)snprintf(out_path + strlen(out_path), out_path_size - strlen(out_path), "%s", "opengl_sky.log");
  return 1;
}

static void diagnostics_store_recent_message(const char* line)
{
  size_t length = 0U;
  char* destination = NULL;

  if (line == NULL || line[0] == '\0')
  {
    return;
  }

  destination = g_diagnostics_recent_messages[g_diagnostics_recent_write_index];
  length = strlen(line);
  if (length >= DIAGNOSTICS_RECENT_MESSAGE_LENGTH)
  {
    length = DIAGNOSTICS_RECENT_MESSAGE_LENGTH - 1U;
  }

  memcpy(destination, line, length);
  destination[length] = '\0';

  g_diagnostics_recent_write_index = (g_diagnostics_recent_write_index + 1) % DIAGNOSTICS_RECENT_MESSAGE_COUNT;
  if (g_diagnostics_recent_count < DIAGNOSTICS_RECENT_MESSAGE_COUNT)
  {
    g_diagnostics_recent_count += 1;
  }
}

void diagnostics_log(const char* message)
{
  char log_path[PLATFORM_PATH_MAX] = { 0 };
  char formatted_line[1024] = { 0 };
  FILE* file = NULL;
  time_t now_seconds = 0;
  struct tm now_local = { 0 };
  unsigned int milliseconds = 0U;

  if (message == NULL || !diagnostics_build_log_path(log_path, sizeof(log_path)))
  {
    return;
  }

  #if defined(_WIN32)
  {
    SYSTEMTIME now = { 0 };
    GetLocalTime(&now);
    now_seconds = time(NULL);
    milliseconds = (unsigned int)now.wMilliseconds;
  }
  if (localtime_s(&now_local, &now_seconds) != 0)
  {
    return;
  }
  #else
  {
    struct timeval now = { 0 };
    (void)gettimeofday(&now, NULL);
    now_seconds = now.tv_sec;
    milliseconds = (unsigned int)(now.tv_usec / 1000U);
  }
  if (localtime_r(&now_seconds, &now_local) == NULL)
  {
    return;
  }
  #endif

  #if defined(_MSC_VER)
  if (fopen_s(&file, log_path, "a") != 0)
  {
    file = NULL;
  }
  #else
  file = fopen(log_path, "a");
  #endif

  if (file == NULL)
  {
    (void)snprintf(
      formatted_line,
      sizeof(formatted_line),
      "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s",
      (unsigned)(now_local.tm_year + 1900),
      (unsigned)(now_local.tm_mon + 1),
      (unsigned)now_local.tm_mday,
      (unsigned)now_local.tm_hour,
      (unsigned)now_local.tm_min,
      (unsigned)now_local.tm_sec,
      milliseconds,
      message);
    diagnostics_store_recent_message(formatted_line);
    return;
  }

  (void)snprintf(
    formatted_line,
    sizeof(formatted_line),
    "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s",
    (unsigned)(now_local.tm_year + 1900),
    (unsigned)(now_local.tm_mon + 1),
    (unsigned)now_local.tm_mday,
    (unsigned)now_local.tm_hour,
    (unsigned)now_local.tm_min,
    (unsigned)now_local.tm_sec,
    milliseconds,
    message
  );
  diagnostics_store_recent_message(formatted_line);
  (void)fprintf(file, "%s\n", formatted_line);
  fclose(file);
}

void diagnostics_logf(const char* format, ...)
{
  char buffer[1024] = { 0 };
  va_list args;

  if (format == NULL)
  {
    return;
  }

  va_start(args, format);
  (void)vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  diagnostics_log(buffer);
}

int diagnostics_get_recent_message_count(void)
{
  return g_diagnostics_recent_count;
}

const char* diagnostics_get_recent_message(int index)
{
  const int oldest_index =
    (g_diagnostics_recent_write_index - g_diagnostics_recent_count + DIAGNOSTICS_RECENT_MESSAGE_COUNT) %
    DIAGNOSTICS_RECENT_MESSAGE_COUNT;

  if (index < 0 || index >= g_diagnostics_recent_count)
  {
    return "";
  }

  return g_diagnostics_recent_messages[(oldest_index + index) % DIAGNOSTICS_RECENT_MESSAGE_COUNT];
}
