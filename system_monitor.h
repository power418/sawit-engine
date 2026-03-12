#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#else

#include <stdint.h>
typedef uint64_t ULONGLONG;

#endif

typedef struct SystemUsageSample
{
  float cpu_percent;
  float gpu0_percent;
  float gpu1_percent;
} SystemUsageSample;

typedef struct SystemMonitor
{
#if defined(_WIN32)
  ULONGLONG previous_idle_time;
  ULONGLONG previous_kernel_time;
  ULONGLONG previous_user_time;
  ULONGLONG last_gpu_sample_time_ms;
  PDH_HQUERY gpu_query;
  PDH_HCOUNTER gpu_engine_counter;
  float cpu_percent;
  float gpu_percent[2];
  int cpu_time_valid;
  int cpu_value_valid;
  int gpu_query_ready;
  int gpu_value_valid[2];
#else
  float cpu_percent;
  float gpu_percent[2];
#endif
} SystemMonitor;

void system_monitor_create(SystemMonitor* monitor);
void system_monitor_destroy(SystemMonitor* monitor);
void system_monitor_update(SystemMonitor* monitor, SystemUsageSample* out_sample);

#endif
