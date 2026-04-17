#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#define SYSTEM_MONITOR_THERMAL_LABEL_LENGTH 96
#define SYSTEM_MONITOR_MAX_GPU_ADAPTERS 8
#define SYSTEM_MONITOR_GPU_LUID_TEXT_LENGTH 32

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <wbemidl.h>

#else

#include <stdint.h>
typedef uint64_t ULONGLONG;

#endif

typedef struct SystemGpuAdapterSample
{
  float usage_percent;
  float memory_usage_mb;
  unsigned int luid_low_part;
  int luid_high_part;
  char luid_text[SYSTEM_MONITOR_GPU_LUID_TEXT_LENGTH];
} SystemGpuAdapterSample;

typedef struct SystemUsageSample
{
  float cpu_percent;
  float gpu0_percent;
  float gpu1_percent;
  float system_memory_percent;
  float system_memory_used_mb;
  float system_memory_total_mb;
  float gpu0_memory_usage_mb;
  float gpu1_memory_usage_mb;
  float thermal_zone_temperature_c;
  float gpu_temperature_c;
  int thermal_zone_temperature_available;
  int gpu_temperature_available;
  int gpu_adapter_count;
  SystemGpuAdapterSample gpu_adapters[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  char thermal_zone_name[SYSTEM_MONITOR_THERMAL_LABEL_LENGTH];
} SystemUsageSample;

typedef struct SystemMonitor
{
#if defined(_WIN32)
  ULONGLONG previous_idle_time;
  ULONGLONG previous_kernel_time;
  ULONGLONG previous_user_time;
  ULONGLONG last_gpu_sample_time_ms;
  ULONGLONG last_gpu_memory_sample_time_ms;
  ULONGLONG last_thermal_sample_time_ms;
  PDH_HQUERY gpu_query;
  PDH_HCOUNTER gpu_engine_counter;
  PDH_HQUERY gpu_memory_query;
  PDH_HCOUNTER gpu_memory_counter;
  IWbemLocator* wmi_locator;
  IWbemServices* thermal_services;
  IWbemServices* sensor_services;
  float cpu_percent;
  float gpu_percent[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  float gpu_memory_usage_mb[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  float thermal_zone_temperature_c;
  float gpu_temperature_c;
  float system_memory_percent;
  float system_memory_used_mb;
  float system_memory_total_mb;
  int gpu_adapter_count;
  SystemGpuAdapterSample gpu_adapters[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  int cpu_time_valid;
  int cpu_value_valid;
  int gpu_query_ready;
  int gpu_value_valid[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  int gpu_memory_query_ready;
  int gpu_memory_value_valid[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
  int thermal_zone_temperature_available;
  int gpu_temperature_available;
  int com_initialized;
  int wmi_available;
  char thermal_zone_name[SYSTEM_MONITOR_THERMAL_LABEL_LENGTH];
#else
  float cpu_percent;
  float gpu_percent[SYSTEM_MONITOR_MAX_GPU_ADAPTERS];
#endif
} SystemMonitor;

void system_monitor_create(SystemMonitor* monitor);
void system_monitor_destroy(SystemMonitor* monitor);
void system_monitor_update(SystemMonitor* monitor, SystemUsageSample* out_sample);

#endif
