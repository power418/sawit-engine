#include "system_monitor.h"

#include "diagnostics.h"

#if defined(_WIN32)

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static ULONGLONG system_monitor_filetime_to_uint64(FILETIME value);
static float system_monitor_clamp_percent(float value);
static void system_monitor_update_cpu(SystemMonitor* monitor);
static int system_monitor_parse_gpu_index(const wchar_t* instance_name);
static void system_monitor_update_gpu(SystemMonitor* monitor);

void system_monitor_create(SystemMonitor* monitor)
{
  FILETIME idle_time = { 0 };
  FILETIME kernel_time = { 0 };
  FILETIME user_time = { 0 };

  if (monitor == NULL)
  {
    return;
  }

  memset(monitor, 0, sizeof(*monitor));
  if (GetSystemTimes(&idle_time, &kernel_time, &user_time) != FALSE)
  {
    monitor->previous_idle_time = system_monitor_filetime_to_uint64(idle_time);
    monitor->previous_kernel_time = system_monitor_filetime_to_uint64(kernel_time);
    monitor->previous_user_time = system_monitor_filetime_to_uint64(user_time);
    monitor->cpu_time_valid = 1;
  }

  if (PdhOpenQueryW(NULL, 0, &monitor->gpu_query) == ERROR_SUCCESS)
  {
    if (PdhAddEnglishCounterW(
        monitor->gpu_query,
        L"\\GPU Engine(*)\\Utilization Percentage",
        0,
        &monitor->gpu_engine_counter) == ERROR_SUCCESS)
    {
      if (PdhCollectQueryData(monitor->gpu_query) == ERROR_SUCCESS)
      {
        monitor->gpu_query_ready = 1;
        monitor->last_gpu_sample_time_ms = GetTickCount64();
      }
    }
  }

  if (monitor->gpu_query_ready == 0 && monitor->gpu_query != NULL)
  {
    diagnostics_log("system_monitor_create: GPU performance counters unavailable");
    (void)PdhCloseQuery(monitor->gpu_query);
    monitor->gpu_query = NULL;
    monitor->gpu_engine_counter = NULL;
  }
}

void system_monitor_destroy(SystemMonitor* monitor)
{
  if (monitor == NULL)
  {
    return;
  }

  if (monitor->gpu_query != NULL)
  {
    (void)PdhCloseQuery(monitor->gpu_query);
    monitor->gpu_query = NULL;
  }

  monitor->gpu_engine_counter = NULL;
  monitor->gpu_query_ready = 0;
}

void system_monitor_update(SystemMonitor* monitor, SystemUsageSample* out_sample)
{
  if (monitor == NULL || out_sample == NULL)
  {
    return;
  }

  system_monitor_update_cpu(monitor);
  system_monitor_update_gpu(monitor);

  out_sample->cpu_percent = monitor->cpu_percent;
  out_sample->gpu0_percent = monitor->gpu_percent[0];
  out_sample->gpu1_percent = monitor->gpu_percent[1];
}

static ULONGLONG system_monitor_filetime_to_uint64(FILETIME value)
{
  ULARGE_INTEGER wide_value = { 0 };
  wide_value.LowPart = value.dwLowDateTime;
  wide_value.HighPart = value.dwHighDateTime;
  return wide_value.QuadPart;
}

static float system_monitor_clamp_percent(float value)
{
  if (value < 0.0f)
  {
    return 0.0f;
  }
  if (value > 100.0f)
  {
    return 100.0f;
  }
  return value;
}

static void system_monitor_update_cpu(SystemMonitor* monitor)
{
  FILETIME idle_time = { 0 };
  FILETIME kernel_time = { 0 };
  FILETIME user_time = { 0 };

  if (monitor == NULL || GetSystemTimes(&idle_time, &kernel_time, &user_time) == FALSE)
  {
    return;
  }

  {
    const ULONGLONG current_idle = system_monitor_filetime_to_uint64(idle_time);
    const ULONGLONG current_kernel = system_monitor_filetime_to_uint64(kernel_time);
    const ULONGLONG current_user = system_monitor_filetime_to_uint64(user_time);

    if (monitor->cpu_time_valid != 0)
    {
      const ULONGLONG idle_delta = current_idle - monitor->previous_idle_time;
      const ULONGLONG kernel_delta = current_kernel - monitor->previous_kernel_time;
      const ULONGLONG user_delta = current_user - monitor->previous_user_time;
      const ULONGLONG total_delta = kernel_delta + user_delta;

      if (total_delta > 0U)
      {
        const ULONGLONG busy_delta = (total_delta > idle_delta) ? (total_delta - idle_delta) : 0U;
        const float raw_cpu = (float)((double)busy_delta * 100.0 / (double)total_delta);

        if (monitor->cpu_value_valid == 0)
        {
          monitor->cpu_percent = system_monitor_clamp_percent(raw_cpu);
          monitor->cpu_value_valid = 1;
        }
        else
        {
          monitor->cpu_percent += (system_monitor_clamp_percent(raw_cpu) - monitor->cpu_percent) * 0.30f;
        }
      }
    }

    monitor->previous_idle_time = current_idle;
    monitor->previous_kernel_time = current_kernel;
    monitor->previous_user_time = current_user;
    monitor->cpu_time_valid = 1;
  }
}

static int system_monitor_parse_gpu_index(const wchar_t* instance_name)
{
  const wchar_t* marker = NULL;
  wchar_t* end = NULL;
  long parsed_value = 0;

  if (instance_name == NULL)
  {
    return -1;
  }

  marker = wcsstr(instance_name, L"phys_");
  if (marker == NULL)
  {
    return -1;
  }

  marker += 5;
  parsed_value = wcstol(marker, &end, 10);
  if (end == marker || parsed_value < 0L || parsed_value > 1L)
  {
    return -1;
  }

  return (int)parsed_value;
}

static void system_monitor_update_gpu(SystemMonitor* monitor)
{
  DWORD buffer_size = 0U;
  DWORD item_count = 0U;
  PDH_STATUS status = ERROR_SUCCESS;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = NULL;
  ULONGLONG current_tick = 0U;

  if (monitor == NULL || monitor->gpu_query_ready == 0 || monitor->gpu_query == NULL || monitor->gpu_engine_counter == NULL)
  {
    return;
  }

  current_tick = GetTickCount64();
  if (monitor->last_gpu_sample_time_ms != 0U && current_tick - monitor->last_gpu_sample_time_ms < 140U)
  {
    return;
  }

  monitor->last_gpu_sample_time_ms = current_tick;
  status = PdhCollectQueryData(monitor->gpu_query);
  if (status != ERROR_SUCCESS)
  {
    return;
  }

  status = PdhGetFormattedCounterArrayW(
    monitor->gpu_engine_counter,
    PDH_FMT_DOUBLE,
    &buffer_size,
    &item_count,
    NULL);
  if (status != (PDH_STATUS)PDH_MORE_DATA || buffer_size == 0U)
  {
    return;
  }

  items = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(buffer_size);
  if (items == NULL)
  {
    return;
  }

  status = PdhGetFormattedCounterArrayW(
    monitor->gpu_engine_counter,
    PDH_FMT_DOUBLE,
    &buffer_size,
    &item_count,
    items);
  if (status == ERROR_SUCCESS)
  {
    float raw_gpu[2] = { 0.0f, 0.0f };
    DWORD item_index = 0U;

    for (item_index = 0U; item_index < item_count; ++item_index)
    {
      const int gpu_index = system_monitor_parse_gpu_index(items[item_index].szName);
      const double value = items[item_index].FmtValue.doubleValue;

      if (gpu_index < 0 || gpu_index > 1 || items[item_index].FmtValue.CStatus != ERROR_SUCCESS)
      {
        continue;
      }

      if ((float)value > raw_gpu[gpu_index])
      {
        raw_gpu[gpu_index] = (float)value;
      }
    }

    {
      int gpu_index = 0;
      for (gpu_index = 0; gpu_index < 2; ++gpu_index)
      {
        const float clamped_value = system_monitor_clamp_percent(raw_gpu[gpu_index]);
        if (monitor->gpu_value_valid[gpu_index] == 0)
        {
          monitor->gpu_percent[gpu_index] = clamped_value;
          monitor->gpu_value_valid[gpu_index] = 1;
        }
        else
        {
          monitor->gpu_percent[gpu_index] += (clamped_value - monitor->gpu_percent[gpu_index]) * 0.35f;
        }
      }
    }
  }

  free(items);
}

#else

#include <string.h>

void system_monitor_create(SystemMonitor* monitor)
{
  if (monitor == NULL)
  {
    return;
  }

  memset(monitor, 0, sizeof(*monitor));
  diagnostics_log("system_monitor_create: using macOS stub implementation");
}

void system_monitor_destroy(SystemMonitor* monitor)
{
  (void)monitor;
}

void system_monitor_update(SystemMonitor* monitor, SystemUsageSample* out_sample)
{
  if (monitor == NULL || out_sample == NULL)
  {
    return;
  }

  out_sample->cpu_percent = monitor->cpu_percent;
  out_sample->gpu0_percent = monitor->gpu_percent[0];
  out_sample->gpu1_percent = monitor->gpu_percent[1];
}

#endif
