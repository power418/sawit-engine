#define COBJMACROS

#include "system_monitor.h"

#include "diagnostics.h"

#if defined(_WIN32)

#include <objbase.h>
#include <oleauto.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

enum
{
  SYSTEM_MONITOR_GPU_SAMPLE_INTERVAL_MS = 140U,
  SYSTEM_MONITOR_GPU_MEMORY_SAMPLE_INTERVAL_MS = 260U,
  SYSTEM_MONITOR_THERMAL_SAMPLE_INTERVAL_MS = 2000U
};

static ULONGLONG system_monitor_filetime_to_uint64(FILETIME value);
static float system_monitor_clamp_percent(float value);
static void system_monitor_copy_utf8(char* destination, size_t destination_size, const char* source);
static void system_monitor_copy_wide_to_utf8(char* destination, size_t destination_size, const wchar_t* source);
static void system_monitor_update_cpu(SystemMonitor* monitor);
static int system_monitor_parse_gpu_luid(
  const wchar_t* instance_name,
  unsigned int* out_luid_low_part,
  int* out_luid_high_part,
  char* out_luid_text,
  size_t out_luid_text_size);
static int system_monitor_find_gpu_adapter_index(
  const SystemGpuAdapterSample* adapters,
  int adapter_count,
  unsigned int luid_low_part,
  int luid_high_part);
static int system_monitor_get_or_create_gpu_adapter_slot(
  SystemGpuAdapterSample* adapters,
  int* adapter_count,
  unsigned int luid_low_part,
  int luid_high_part,
  const char* luid_text);
static void system_monitor_smooth_gpu_adapter_samples(
  SystemMonitor* monitor,
  const SystemGpuAdapterSample* raw_adapters,
  int raw_adapter_count,
  int update_usage,
  int update_memory);
static int system_monitor_build_legacy_gpu_percentages(const SystemGpuAdapterSample* adapters, int adapter_count, float* out_gpu0, float* out_gpu1);
static void system_monitor_update_gpu(SystemMonitor* monitor);
static void system_monitor_update_gpu_memory(SystemMonitor* monitor);
static void system_monitor_update_memory(SystemMonitor* monitor);
static int system_monitor_initialize_wmi(SystemMonitor* monitor);
static int system_monitor_connect_wmi_namespace(
  SystemMonitor* monitor,
  const wchar_t* namespace_path,
  IWbemServices** out_services);
static int system_monitor_execute_wmi_query(
  IWbemServices* services,
  const wchar_t* query,
  IEnumWbemClassObject** out_enumerator);
static int system_monitor_variant_to_float(const VARIANT* value, float* out_value);
static int system_monitor_wide_contains_ignore_case(const wchar_t* text, const wchar_t* pattern);
static void system_monitor_update_temperatures(SystemMonitor* monitor);
static void system_monitor_query_thermal_zone(SystemMonitor* monitor);
static void system_monitor_query_gpu_temperature(SystemMonitor* monitor);

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
        &monitor->gpu_engine_counter) == ERROR_SUCCESS &&
      PdhCollectQueryData(monitor->gpu_query) == ERROR_SUCCESS)
    {
      monitor->gpu_query_ready = 1;
      monitor->last_gpu_sample_time_ms = GetTickCount64();
    }
  }

  if (monitor->gpu_query_ready == 0 && monitor->gpu_query != NULL)
  {
    diagnostics_log("system_monitor_create: GPU engine performance counters unavailable");
    (void)PdhCloseQuery(monitor->gpu_query);
    monitor->gpu_query = NULL;
    monitor->gpu_engine_counter = NULL;
  }

  if (PdhOpenQueryW(NULL, 0, &monitor->gpu_memory_query) == ERROR_SUCCESS)
  {
    if (PdhAddEnglishCounterW(
        monitor->gpu_memory_query,
        L"\\GPU Adapter Memory(*)\\Dedicated Usage",
        0,
        &monitor->gpu_memory_counter) == ERROR_SUCCESS &&
      PdhCollectQueryData(monitor->gpu_memory_query) == ERROR_SUCCESS)
    {
      monitor->gpu_memory_query_ready = 1;
      monitor->last_gpu_memory_sample_time_ms = GetTickCount64();
    }
  }

  if (monitor->gpu_memory_query_ready == 0 && monitor->gpu_memory_query != NULL)
  {
    diagnostics_log("system_monitor_create: GPU memory counters unavailable");
    (void)PdhCloseQuery(monitor->gpu_memory_query);
    monitor->gpu_memory_query = NULL;
    monitor->gpu_memory_counter = NULL;
  }

  (void)system_monitor_initialize_wmi(monitor);
  system_monitor_update_memory(monitor);
  system_monitor_update_temperatures(monitor);

  diagnostics_logf(
    "system_monitor_create: thermal_zone=%s gpu_temp_sensor=%s ram_total=%.0fMB",
    monitor->thermal_zone_temperature_available != 0 ? monitor->thermal_zone_name : "unavailable",
    monitor->sensor_services != NULL ? "available" : "unavailable",
    monitor->system_memory_total_mb);
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

  if (monitor->gpu_memory_query != NULL)
  {
    (void)PdhCloseQuery(monitor->gpu_memory_query);
    monitor->gpu_memory_query = NULL;
  }

  if (monitor->sensor_services != NULL)
  {
    IWbemServices_Release(monitor->sensor_services);
    monitor->sensor_services = NULL;
  }

  if (monitor->thermal_services != NULL)
  {
    IWbemServices_Release(monitor->thermal_services);
    monitor->thermal_services = NULL;
  }

  if (monitor->wmi_locator != NULL)
  {
    IWbemLocator_Release(monitor->wmi_locator);
    monitor->wmi_locator = NULL;
  }

  if (monitor->com_initialized != 0)
  {
    CoUninitialize();
    monitor->com_initialized = 0;
  }

  monitor->gpu_engine_counter = NULL;
  monitor->gpu_memory_counter = NULL;
  monitor->gpu_query_ready = 0;
  monitor->gpu_memory_query_ready = 0;
}

void system_monitor_update(SystemMonitor* monitor, SystemUsageSample* out_sample)
{
  if (monitor == NULL || out_sample == NULL)
  {
    return;
  }

  system_monitor_update_cpu(monitor);
  system_monitor_update_gpu(monitor);
  system_monitor_update_gpu_memory(monitor);
  system_monitor_update_memory(monitor);
  system_monitor_update_temperatures(monitor);

  memset(out_sample, 0, sizeof(*out_sample));
  out_sample->cpu_percent = monitor->cpu_percent;
  out_sample->gpu0_percent = monitor->gpu_percent[0];
  out_sample->gpu1_percent = monitor->gpu_percent[1];
  out_sample->system_memory_percent = monitor->system_memory_percent;
  out_sample->system_memory_used_mb = monitor->system_memory_used_mb;
  out_sample->system_memory_total_mb = monitor->system_memory_total_mb;
  out_sample->gpu_adapter_count = monitor->gpu_adapter_count;
  out_sample->thermal_zone_temperature_c = monitor->thermal_zone_temperature_c;
  out_sample->gpu_temperature_c = monitor->gpu_temperature_c;
  out_sample->thermal_zone_temperature_available = monitor->thermal_zone_temperature_available;
  out_sample->gpu_temperature_available = monitor->gpu_temperature_available;
  if (monitor->gpu_adapter_count > 0)
  {
    size_t copy_count = (size_t)monitor->gpu_adapter_count;
    if (copy_count > SYSTEM_MONITOR_MAX_GPU_ADAPTERS)
    {
      copy_count = SYSTEM_MONITOR_MAX_GPU_ADAPTERS;
    }
    memcpy(out_sample->gpu_adapters, monitor->gpu_adapters, copy_count * sizeof(out_sample->gpu_adapters[0]));
    out_sample->gpu0_memory_usage_mb = monitor->gpu_adapters[0].memory_usage_mb;
    out_sample->gpu0_percent = monitor->gpu_adapters[0].usage_percent;
    if (copy_count > 1U)
    {
      out_sample->gpu1_memory_usage_mb = monitor->gpu_adapters[1].memory_usage_mb;
      out_sample->gpu1_percent = monitor->gpu_adapters[1].usage_percent;
    }
  }
  system_monitor_copy_utf8(out_sample->thermal_zone_name, sizeof(out_sample->thermal_zone_name), monitor->thermal_zone_name);
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

static void system_monitor_copy_utf8(char* destination, size_t destination_size, const char* source)
{
  size_t length = 0U;

  if (destination == NULL || destination_size == 0U)
  {
    return;
  }

  destination[0] = '\0';
  if (source == NULL)
  {
    return;
  }

  length = strlen(source);
  if (length >= destination_size)
  {
    length = destination_size - 1U;
  }

  memcpy(destination, source, length);
  destination[length] = '\0';
}

static void system_monitor_copy_wide_to_utf8(char* destination, size_t destination_size, const wchar_t* source)
{
  int converted_length = 0;

  if (destination == NULL || destination_size == 0U)
  {
    return;
  }

  destination[0] = '\0';
  if (source == NULL || source[0] == L'\0')
  {
    return;
  }

  converted_length = WideCharToMultiByte(CP_UTF8, 0, source, -1, destination, (int)destination_size, NULL, NULL);
  if (converted_length <= 0)
  {
    system_monitor_copy_utf8(destination, destination_size, "sensor");
  }
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

static int system_monitor_parse_gpu_luid(
  const wchar_t* instance_name,
  unsigned int* out_luid_low_part,
  int* out_luid_high_part,
  char* out_luid_text,
  size_t out_luid_text_size)
{
  const wchar_t* marker = NULL;
  unsigned int high_part = 0U;
  unsigned int low_part = 0U;

  if (instance_name == NULL || out_luid_low_part == NULL || out_luid_high_part == NULL)
  {
    return 0;
  }

  marker = wcsstr(instance_name, L"luid_0x");
  if (marker == NULL)
  {
    return 0;
  }

  if (swscanf(
      marker,
      L"luid_0x%x_0x%x",
      &high_part,
      &low_part) != 2)
  {
    return 0;
  }

  *out_luid_low_part = low_part;
  *out_luid_high_part = (int)high_part;
  if (out_luid_text != NULL && out_luid_text_size > 0U)
  {
    (void)snprintf(out_luid_text, out_luid_text_size, "0x%08x_0x%08x", high_part, low_part);
  }
  return 1;
}

static int system_monitor_find_gpu_adapter_index(
  const SystemGpuAdapterSample* adapters,
  int adapter_count,
  unsigned int luid_low_part,
  int luid_high_part)
{
  int adapter_index = 0;

  if (adapters == NULL || adapter_count <= 0)
  {
    return -1;
  }

  for (adapter_index = 0; adapter_index < adapter_count; ++adapter_index)
  {
    if (adapters[adapter_index].luid_low_part == luid_low_part &&
      adapters[adapter_index].luid_high_part == luid_high_part)
    {
      return adapter_index;
    }
  }

  return -1;
}

static int system_monitor_get_or_create_gpu_adapter_slot(
  SystemGpuAdapterSample* adapters,
  int* adapter_count,
  unsigned int luid_low_part,
  int luid_high_part,
  const char* luid_text)
{
  int adapter_index = -1;

  if (adapters == NULL || adapter_count == NULL)
  {
    return -1;
  }

  adapter_index = system_monitor_find_gpu_adapter_index(adapters, *adapter_count, luid_low_part, luid_high_part);
  if (adapter_index >= 0)
  {
    return adapter_index;
  }

  if (*adapter_count >= SYSTEM_MONITOR_MAX_GPU_ADAPTERS)
  {
    return -1;
  }

  adapter_index = *adapter_count;
  memset(&adapters[adapter_index], 0, sizeof(adapters[adapter_index]));
  adapters[adapter_index].luid_low_part = luid_low_part;
  adapters[adapter_index].luid_high_part = luid_high_part;
  system_monitor_copy_utf8(adapters[adapter_index].luid_text, sizeof(adapters[adapter_index].luid_text), luid_text);
  *adapter_count += 1;
  return adapter_index;
}

static void system_monitor_smooth_gpu_adapter_samples(
  SystemMonitor* monitor,
  const SystemGpuAdapterSample* raw_adapters,
  int raw_adapter_count,
  int update_usage,
  int update_memory)
{
  int raw_index = 0;

  if (monitor == NULL || raw_adapters == NULL || raw_adapter_count < 0)
  {
    return;
  }

  for (raw_index = 0; raw_index < raw_adapter_count; ++raw_index)
  {
    const SystemGpuAdapterSample* raw_adapter = &raw_adapters[raw_index];
    const int monitor_index = system_monitor_get_or_create_gpu_adapter_slot(
      monitor->gpu_adapters,
      &monitor->gpu_adapter_count,
      raw_adapter->luid_low_part,
      raw_adapter->luid_high_part,
      raw_adapter->luid_text);

    if (monitor_index < 0)
    {
      continue;
    }

    if (update_usage != 0)
    {
      if (monitor->gpu_value_valid[monitor_index] == 0)
      {
        monitor->gpu_adapters[monitor_index].usage_percent = system_monitor_clamp_percent(raw_adapter->usage_percent);
        monitor->gpu_value_valid[monitor_index] = 1;
      }
      else
      {
        monitor->gpu_adapters[monitor_index].usage_percent +=
          (system_monitor_clamp_percent(raw_adapter->usage_percent) - monitor->gpu_adapters[monitor_index].usage_percent) * 0.35f;
      }
    }

    if (update_memory != 0)
    {
      const float clamped_memory = (raw_adapter->memory_usage_mb < 0.0f) ? 0.0f : raw_adapter->memory_usage_mb;

      if (monitor->gpu_memory_value_valid[monitor_index] == 0)
      {
        monitor->gpu_adapters[monitor_index].memory_usage_mb = clamped_memory;
        monitor->gpu_memory_value_valid[monitor_index] = 1;
      }
      else
      {
        monitor->gpu_adapters[monitor_index].memory_usage_mb +=
          (clamped_memory - monitor->gpu_adapters[monitor_index].memory_usage_mb) * 0.35f;
      }
    }
  }

  (void)system_monitor_build_legacy_gpu_percentages(
    monitor->gpu_adapters,
    monitor->gpu_adapter_count,
    &monitor->gpu_percent[0],
    &monitor->gpu_percent[1]);
}

static int system_monitor_build_legacy_gpu_percentages(
  const SystemGpuAdapterSample* adapters,
  int adapter_count,
  float* out_gpu0,
  float* out_gpu1)
{
  float gpu0 = 0.0f;
  float gpu1 = 0.0f;

  if (out_gpu0 != NULL)
  {
    *out_gpu0 = 0.0f;
  }
  if (out_gpu1 != NULL)
  {
    *out_gpu1 = 0.0f;
  }
  if (adapters == NULL || adapter_count <= 0)
  {
    return 0;
  }

  if (adapter_count > 0)
  {
    gpu0 = adapters[0].usage_percent;
  }
  if (adapter_count > 1)
  {
    gpu1 = adapters[1].usage_percent;
  }

  if (out_gpu0 != NULL)
  {
    *out_gpu0 = gpu0;
  }
  if (out_gpu1 != NULL)
  {
    *out_gpu1 = gpu1;
  }
  return 1;
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
  if (monitor->last_gpu_sample_time_ms != 0U &&
    current_tick - monitor->last_gpu_sample_time_ms < SYSTEM_MONITOR_GPU_SAMPLE_INTERVAL_MS)
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
    SystemGpuAdapterSample raw_adapters[SYSTEM_MONITOR_MAX_GPU_ADAPTERS] = { 0 };
    int raw_adapter_count = 0;
    DWORD item_index = 0U;

    for (item_index = 0U; item_index < item_count; ++item_index)
    {
      const double value = items[item_index].FmtValue.doubleValue;
      unsigned int luid_low_part = 0U;
      int luid_high_part = 0;
      char luid_text[SYSTEM_MONITOR_GPU_LUID_TEXT_LENGTH] = { 0 };
      int adapter_index = -1;

      if (items[item_index].FmtValue.CStatus != ERROR_SUCCESS ||
        !system_monitor_parse_gpu_luid(
          items[item_index].szName,
          &luid_low_part,
          &luid_high_part,
          luid_text,
          sizeof(luid_text)))
      {
        continue;
      }

      adapter_index = system_monitor_get_or_create_gpu_adapter_slot(
        raw_adapters,
        &raw_adapter_count,
        luid_low_part,
        luid_high_part,
        luid_text);
      if (adapter_index < 0)
      {
        continue;
      }

      if ((float)value > raw_adapters[adapter_index].usage_percent)
      {
        raw_adapters[adapter_index].usage_percent = (float)value;
      }
    }

    system_monitor_smooth_gpu_adapter_samples(monitor, raw_adapters, raw_adapter_count, 1, 0);
  }

  free(items);
}

static void system_monitor_update_gpu_memory(SystemMonitor* monitor)
{
  DWORD buffer_size = 0U;
  DWORD item_count = 0U;
  PDH_STATUS status = ERROR_SUCCESS;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = NULL;
  ULONGLONG current_tick = 0U;

  if (monitor == NULL ||
    monitor->gpu_memory_query_ready == 0 ||
    monitor->gpu_memory_query == NULL ||
    monitor->gpu_memory_counter == NULL)
  {
    return;
  }

  current_tick = GetTickCount64();
  if (monitor->last_gpu_memory_sample_time_ms != 0U &&
    current_tick - monitor->last_gpu_memory_sample_time_ms < SYSTEM_MONITOR_GPU_MEMORY_SAMPLE_INTERVAL_MS)
  {
    return;
  }

  monitor->last_gpu_memory_sample_time_ms = current_tick;
  status = PdhCollectQueryData(monitor->gpu_memory_query);
  if (status != ERROR_SUCCESS)
  {
    return;
  }

  status = PdhGetFormattedCounterArrayW(
    monitor->gpu_memory_counter,
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
    monitor->gpu_memory_counter,
    PDH_FMT_DOUBLE,
    &buffer_size,
    &item_count,
    items);
  if (status == ERROR_SUCCESS)
  {
    SystemGpuAdapterSample raw_adapters[SYSTEM_MONITOR_MAX_GPU_ADAPTERS] = { 0 };
    int raw_adapter_count = 0;
    DWORD item_index = 0U;

    for (item_index = 0U; item_index < item_count; ++item_index)
    {
      const double value = items[item_index].FmtValue.doubleValue;
      const float memory_usage_mb = (float)(value / (1024.0 * 1024.0));
      unsigned int luid_low_part = 0U;
      int luid_high_part = 0;
      char luid_text[SYSTEM_MONITOR_GPU_LUID_TEXT_LENGTH] = { 0 };
      int adapter_index = -1;

      if (items[item_index].FmtValue.CStatus != ERROR_SUCCESS ||
        !system_monitor_parse_gpu_luid(
          items[item_index].szName,
          &luid_low_part,
          &luid_high_part,
          luid_text,
          sizeof(luid_text)))
      {
        continue;
      }

      adapter_index = system_monitor_get_or_create_gpu_adapter_slot(
        raw_adapters,
        &raw_adapter_count,
        luid_low_part,
        luid_high_part,
        luid_text);
      if (adapter_index < 0)
      {
        continue;
      }

      if (memory_usage_mb > raw_adapters[adapter_index].memory_usage_mb)
      {
        raw_adapters[adapter_index].memory_usage_mb = memory_usage_mb;
      }
    }

    system_monitor_smooth_gpu_adapter_samples(monitor, raw_adapters, raw_adapter_count, 0, 1);
  }

  free(items);
}

static void system_monitor_update_memory(SystemMonitor* monitor)
{
  MEMORYSTATUSEX memory_status = { 0 };

  if (monitor == NULL)
  {
    return;
  }

  memory_status.dwLength = sizeof(memory_status);
  if (GlobalMemoryStatusEx(&memory_status) == FALSE)
  {
    return;
  }

  monitor->system_memory_total_mb = (float)((double)memory_status.ullTotalPhys / (1024.0 * 1024.0));
  monitor->system_memory_used_mb =
    (float)((double)(memory_status.ullTotalPhys - memory_status.ullAvailPhys) / (1024.0 * 1024.0));
  monitor->system_memory_percent = system_monitor_clamp_percent((float)memory_status.dwMemoryLoad);
}

static int system_monitor_initialize_wmi(SystemMonitor* monitor)
{
  HRESULT result = S_OK;
  int com_ready = 0;

  if (monitor == NULL)
  {
    return 0;
  }

  result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (SUCCEEDED(result) || result == S_FALSE)
  {
    monitor->com_initialized = 1;
    com_ready = 1;
  }
  else if (result == RPC_E_CHANGED_MODE)
  {
    com_ready = 1;
  }
  else
  {
    diagnostics_logf("system_monitor_create: CoInitializeEx failed hr=0x%08lx", (unsigned long)result);
    return 0;
  }

  result = CoInitializeSecurity(
    NULL,
    -1,
    NULL,
    NULL,
    RPC_C_AUTHN_LEVEL_DEFAULT,
    RPC_C_IMP_LEVEL_IMPERSONATE,
    NULL,
    EOAC_NONE,
    NULL);
  if (FAILED(result) && result != RPC_E_TOO_LATE)
  {
    diagnostics_logf("system_monitor_create: CoInitializeSecurity failed hr=0x%08lx", (unsigned long)result);
    return 0;
  }

  result = CoCreateInstance(
    &CLSID_WbemLocator,
    NULL,
    CLSCTX_INPROC_SERVER,
    &IID_IWbemLocator,
    (LPVOID*)&monitor->wmi_locator);
  if (FAILED(result) || monitor->wmi_locator == NULL)
  {
    diagnostics_logf("system_monitor_create: WMI locator unavailable hr=0x%08lx", (unsigned long)result);
    return 0;
  }

  if (com_ready == 0)
  {
    return 0;
  }

  (void)system_monitor_connect_wmi_namespace(monitor, L"ROOT\\WMI", &monitor->thermal_services);
  if (!system_monitor_connect_wmi_namespace(monitor, L"ROOT\\LibreHardwareMonitor", &monitor->sensor_services))
  {
    (void)system_monitor_connect_wmi_namespace(monitor, L"ROOT\\OpenHardwareMonitor", &monitor->sensor_services);
  }

  monitor->wmi_available = (monitor->thermal_services != NULL || monitor->sensor_services != NULL);
  return monitor->wmi_available;
}

static int system_monitor_connect_wmi_namespace(
  SystemMonitor* monitor,
  const wchar_t* namespace_path,
  IWbemServices** out_services)
{
  BSTR namespace_name = NULL;
  IWbemServices* services = NULL;
  HRESULT result = S_OK;

  if (monitor == NULL || monitor->wmi_locator == NULL || namespace_path == NULL || out_services == NULL)
  {
    return 0;
  }

  *out_services = NULL;
  namespace_name = SysAllocString(namespace_path);
  if (namespace_name == NULL)
  {
    return 0;
  }

  result = IWbemLocator_ConnectServer(
    monitor->wmi_locator,
    namespace_name,
    NULL,
    NULL,
    NULL,
    0L,
    NULL,
    NULL,
    &services);
  SysFreeString(namespace_name);
  if (FAILED(result) || services == NULL)
  {
    return 0;
  }

  result = CoSetProxyBlanket(
    (IUnknown*)services,
    RPC_C_AUTHN_WINNT,
    RPC_C_AUTHZ_NONE,
    NULL,
    RPC_C_AUTHN_LEVEL_CALL,
    RPC_C_IMP_LEVEL_IMPERSONATE,
    NULL,
    EOAC_NONE);
  if (FAILED(result))
  {
    IWbemServices_Release(services);
    return 0;
  }

  *out_services = services;
  return 1;
}

static int system_monitor_execute_wmi_query(
  IWbemServices* services,
  const wchar_t* query,
  IEnumWbemClassObject** out_enumerator)
{
  BSTR query_language = NULL;
  BSTR query_text = NULL;
  HRESULT result = S_OK;

  if (services == NULL || query == NULL || out_enumerator == NULL)
  {
    return 0;
  }

  *out_enumerator = NULL;
  query_language = SysAllocString(L"WQL");
  query_text = SysAllocString(query);
  if (query_language == NULL || query_text == NULL)
  {
    if (query_language != NULL)
    {
      SysFreeString(query_language);
    }
    if (query_text != NULL)
    {
      SysFreeString(query_text);
    }
    return 0;
  }

  result = IWbemServices_ExecQuery(
    services,
    query_language,
    query_text,
    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
    NULL,
    out_enumerator);
  SysFreeString(query_language);
  SysFreeString(query_text);
  return SUCCEEDED(result) && *out_enumerator != NULL;
}

static int system_monitor_variant_to_float(const VARIANT* value, float* out_value)
{
  if (value == NULL || out_value == NULL)
  {
    return 0;
  }

  switch (value->vt)
  {
    case VT_R4:
      *out_value = value->fltVal;
      return 1;
    case VT_R8:
      *out_value = (float)value->dblVal;
      return 1;
    case VT_I4:
      *out_value = (float)value->lVal;
      return 1;
    case VT_UI4:
      *out_value = (float)value->ulVal;
      return 1;
    case VT_I2:
      *out_value = (float)value->iVal;
      return 1;
    case VT_UI2:
      *out_value = (float)value->uiVal;
      return 1;
    case VT_I8:
      *out_value = (float)value->llVal;
      return 1;
    case VT_UI8:
      *out_value = (float)value->ullVal;
      return 1;
    default:
      return 0;
  }
}

static int system_monitor_wide_contains_ignore_case(const wchar_t* text, const wchar_t* pattern)
{
  size_t text_length = 0U;
  size_t pattern_length = 0U;
  size_t start_index = 0U;

  if (text == NULL || pattern == NULL || pattern[0] == L'\0')
  {
    return 0;
  }

  text_length = wcslen(text);
  pattern_length = wcslen(pattern);
  if (pattern_length > text_length)
  {
    return 0;
  }

  for (start_index = 0U; start_index + pattern_length <= text_length; ++start_index)
  {
    size_t pattern_index = 0U;
    for (pattern_index = 0U; pattern_index < pattern_length; ++pattern_index)
    {
      if (towlower(text[start_index + pattern_index]) != towlower(pattern[pattern_index]))
      {
        break;
      }
    }

    if (pattern_index == pattern_length)
    {
      return 1;
    }
  }

  return 0;
}

static void system_monitor_update_temperatures(SystemMonitor* monitor)
{
  const ULONGLONG current_tick = GetTickCount64();

  if (monitor == NULL || monitor->wmi_available == 0)
  {
    return;
  }

  if (monitor->last_thermal_sample_time_ms != 0U &&
    current_tick - monitor->last_thermal_sample_time_ms < SYSTEM_MONITOR_THERMAL_SAMPLE_INTERVAL_MS)
  {
    return;
  }

  monitor->last_thermal_sample_time_ms = current_tick;
  system_monitor_query_thermal_zone(monitor);
  system_monitor_query_gpu_temperature(monitor);
}

static void system_monitor_query_thermal_zone(SystemMonitor* monitor)
{
  IEnumWbemClassObject* enumerator = NULL;
  int found_temperature = 0;
  float best_temperature_c = 0.0f;
  char best_zone_name[SYSTEM_MONITOR_THERMAL_LABEL_LENGTH] = { 0 };

  if (monitor == NULL || monitor->thermal_services == NULL)
  {
    return;
  }

  if (!system_monitor_execute_wmi_query(
      monitor->thermal_services,
      L"SELECT CurrentTemperature, InstanceName FROM MSAcpi_ThermalZoneTemperature",
      &enumerator))
  {
    return;
  }

  while (enumerator != NULL)
  {
    IWbemClassObject* object = NULL;
    ULONG returned = 0U;
    VARIANT temperature_value;
    VARIANT name_value;
    float raw_temperature = 0.0f;

    VariantInit(&temperature_value);
    VariantInit(&name_value);
    if (IEnumWbemClassObject_Next(enumerator, WBEM_INFINITE, 1, &object, &returned) != WBEM_S_NO_ERROR || returned == 0U)
    {
      VariantClear(&temperature_value);
      VariantClear(&name_value);
      break;
    }

    if (SUCCEEDED(IWbemClassObject_Get(object, L"CurrentTemperature", 0L, &temperature_value, NULL, NULL)) &&
      system_monitor_variant_to_float(&temperature_value, &raw_temperature))
    {
      const float temperature_c = raw_temperature / 10.0f - 273.15f;

      if (temperature_c > -40.0f && temperature_c < 160.0f)
      {
        if (found_temperature == 0 || temperature_c > best_temperature_c)
        {
          best_temperature_c = temperature_c;

          if (SUCCEEDED(IWbemClassObject_Get(object, L"InstanceName", 0L, &name_value, NULL, NULL)) &&
            name_value.vt == VT_BSTR)
          {
            system_monitor_copy_wide_to_utf8(
              best_zone_name,
              sizeof(best_zone_name),
              name_value.bstrVal);
          }
          else if (best_zone_name[0] == '\0')
          {
            system_monitor_copy_utf8(
              best_zone_name,
              sizeof(best_zone_name),
              "thermal-zone");
          }
        }

        found_temperature = 1;
      }
    }

    VariantClear(&temperature_value);
    VariantClear(&name_value);
    IWbemClassObject_Release(object);
  }

  if (enumerator != NULL)
  {
    IEnumWbemClassObject_Release(enumerator);
  }

  if (found_temperature != 0)
  {
    monitor->thermal_zone_temperature_c = best_temperature_c;
    monitor->thermal_zone_temperature_available = 1;
    system_monitor_copy_utf8(monitor->thermal_zone_name, sizeof(monitor->thermal_zone_name), best_zone_name);
  }
}

static void system_monitor_query_gpu_temperature(SystemMonitor* monitor)
{
  IEnumWbemClassObject* enumerator = NULL;
  int found_temperature = 0;
  float best_temperature_c = 0.0f;

  if (monitor == NULL || monitor->sensor_services == NULL)
  {
    return;
  }

  if (!system_monitor_execute_wmi_query(
      monitor->sensor_services,
      L"SELECT Name, Parent, SensorType, Value FROM Sensor",
      &enumerator))
  {
    return;
  }

  while (enumerator != NULL)
  {
    IWbemClassObject* object = NULL;
    ULONG returned = 0U;
    VARIANT name_value;
    VARIANT parent_value;
    VARIANT sensor_type_value;
    VARIANT sensor_value;
    float candidate_temperature = 0.0f;
    int is_gpu_sensor = 0;
    int is_temperature_sensor = 0;

    VariantInit(&name_value);
    VariantInit(&parent_value);
    VariantInit(&sensor_type_value);
    VariantInit(&sensor_value);
    if (IEnumWbemClassObject_Next(enumerator, WBEM_INFINITE, 1, &object, &returned) != WBEM_S_NO_ERROR || returned == 0U)
    {
      VariantClear(&name_value);
      VariantClear(&parent_value);
      VariantClear(&sensor_type_value);
      VariantClear(&sensor_value);
      break;
    }

    if (SUCCEEDED(IWbemClassObject_Get(object, L"SensorType", 0L, &sensor_type_value, NULL, NULL)) &&
      sensor_type_value.vt == VT_BSTR &&
      system_monitor_wide_contains_ignore_case(sensor_type_value.bstrVal, L"temperature"))
    {
      is_temperature_sensor = 1;
    }

    if (is_temperature_sensor != 0 &&
      SUCCEEDED(IWbemClassObject_Get(object, L"Name", 0L, &name_value, NULL, NULL)) &&
      name_value.vt == VT_BSTR &&
      system_monitor_wide_contains_ignore_case(name_value.bstrVal, L"gpu"))
    {
      is_gpu_sensor = 1;
    }

    if (is_temperature_sensor != 0 &&
      is_gpu_sensor == 0 &&
      SUCCEEDED(IWbemClassObject_Get(object, L"Parent", 0L, &parent_value, NULL, NULL)) &&
      parent_value.vt == VT_BSTR &&
      system_monitor_wide_contains_ignore_case(parent_value.bstrVal, L"gpu"))
    {
      is_gpu_sensor = 1;
    }

    if (is_gpu_sensor != 0 &&
      SUCCEEDED(IWbemClassObject_Get(object, L"Value", 0L, &sensor_value, NULL, NULL)) &&
      system_monitor_variant_to_float(&sensor_value, &candidate_temperature) &&
      candidate_temperature > 0.0f &&
      candidate_temperature < 160.0f)
    {
      if (found_temperature == 0 || candidate_temperature > best_temperature_c)
      {
        best_temperature_c = candidate_temperature;
      }

      found_temperature = 1;
    }

    VariantClear(&name_value);
    VariantClear(&parent_value);
    VariantClear(&sensor_type_value);
    VariantClear(&sensor_value);
    IWbemClassObject_Release(object);
  }

  if (enumerator != NULL)
  {
    IEnumWbemClassObject_Release(enumerator);
  }

  if (found_temperature != 0)
  {
    monitor->gpu_temperature_c = best_temperature_c;
    monitor->gpu_temperature_available = 1;
  }
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
  diagnostics_log("system_monitor_create: using non-Windows stub implementation");
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

  memset(out_sample, 0, sizeof(*out_sample));
  out_sample->cpu_percent = monitor->cpu_percent;
  out_sample->gpu0_percent = monitor->gpu_percent[0];
  out_sample->gpu1_percent = monitor->gpu_percent[1];
}

#endif
