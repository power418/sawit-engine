#ifndef GPU_PREFERENCES_H
#define GPU_PREFERENCES_H

#ifdef __cplusplus
extern "C" {
#endif

#define GPU_PREFERENCES_MAX_ADAPTERS 8
#define GPU_PREFERENCES_MAX_NAME_LENGTH 96
#define GPU_PREFERENCES_MAX_RENDERER_LENGTH 128
#define GPU_PREFERENCES_INVALID_TASK_MANAGER_INDEX (-1)

typedef enum GpuPreferenceMode
{
  GPU_PREFERENCE_MODE_AUTO = 0,
  GPU_PREFERENCE_MODE_MINIMUM_POWER,
  GPU_PREFERENCE_MODE_HIGH_PERFORMANCE,
  GPU_PREFERENCE_MODE_COUNT
} GpuPreferenceMode;

typedef struct GpuAdapterInfo
{
  char name[GPU_PREFERENCES_MAX_NAME_LENGTH];
  unsigned int dedicated_video_memory_mb;
  unsigned int luid_low_part;
  int luid_high_part;
  int is_minimum_power_candidate;
  int is_high_performance_candidate;
  int task_manager_index;
} GpuAdapterInfo;

typedef struct GpuPreferenceInfo
{
  int available;
  int adapter_count;
  int total_adapter_count;
  int minimum_power_index;
  int high_performance_index;
  GpuPreferenceMode selected_mode;
  char current_renderer[GPU_PREFERENCES_MAX_RENDERER_LENGTH];
  char current_vendor[GPU_PREFERENCES_MAX_NAME_LENGTH];
  char status_message[GPU_PREFERENCES_MAX_RENDERER_LENGTH];
  GpuAdapterInfo adapters[GPU_PREFERENCES_MAX_ADAPTERS];
} GpuPreferenceInfo;

int gpu_preferences_query(GpuPreferenceInfo* out_info);
void gpu_preferences_set_current_renderer(GpuPreferenceInfo* info, const char* renderer_name, const char* vendor_name);
int gpu_preferences_apply_and_relaunch(GpuPreferenceMode mode);
const char* gpu_preferences_get_mode_label(GpuPreferenceMode mode);
const char* gpu_preferences_get_mode_short_label(GpuPreferenceMode mode);
const GpuAdapterInfo* gpu_preferences_find_adapter_by_task_manager_index(const GpuPreferenceInfo* info, int task_manager_index);
const GpuAdapterInfo* gpu_preferences_find_adapter_by_luid(
  const GpuPreferenceInfo* info,
  unsigned int luid_low_part,
  int luid_high_part);

#ifdef __cplusplus
}
#endif

#endif
