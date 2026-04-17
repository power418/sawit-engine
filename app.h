#ifndef APP_H
#define APP_H

#include "block_world.h"
#include "diagnostics.h"
#include "graphics_backend.h"
#include "gpu_preferences.h"
#include "player_controller.h"
#include "platform.h"
#include "platform_support.h"
#include "renderer.h"
#include "scene_settings.h"
#include "system_monitor.h"
#include "terrain.h"

#include <stddef.h>

typedef struct DayCycleState
{
  float time_of_day;
  float cycles_per_second;
  int auto_advance;
} DayCycleState;

typedef struct AppState
{
  PlatformApp platform;
  Renderer renderer;
  PlayerController player;
  BlockWorld block_world;
  AtmosphereState atmosphere;
  DayCycleState day_cycle;
  SceneSettings scene_settings;
  SystemMonitor system_monitor;
  SystemUsageSample system_usage;
  float previous_time_seconds;
  float stats_update_elapsed_seconds;
  float stats_accumulated_delta_seconds;
  int stats_accumulated_frames;
  float runtime_log_elapsed_seconds;
  int last_health_status;
  OverlayMetrics stats_display_metrics;
  float atmosphere_elapsed_seconds;
} AppState;

static int app_terrain_settings_changed(const SceneSettings* previous_settings, const SceneSettings* current_settings);
static void app_apply_renderer_quality_defaults(AppState* app);
static float app_wrap_unit_interval(float value);
static float app_wrap_degrees(float value);
static BlockType app_get_block_type_for_slot(int slot);
static void app_apply_world_actions(AppState* app, const PlatformInput* input);
static void app_update_day_cycle(DayCycleState* cycle, const PlatformInput* input, float delta_seconds, int freeze_time_enabled);
static void app_update_atmosphere(const DayCycleState* cycle, const SceneSettings* settings, AtmosphereState* atmosphere, float cloud_time_seconds);
static void app_update_window_title(const AppState* app);
static void app_copy_utf8(char* destination, size_t destination_size, const char* source);
static const char* app_get_health_status_label(int status);
static const SystemGpuAdapterSample* app_find_system_gpu_adapter_sample(
  const SystemUsageSample* system_usage,
  unsigned int luid_low_part,
  int luid_high_part);
static int app_evaluate_health_status(const OverlayMetrics* metrics, const SystemUsageSample* system_usage, char* out_summary, size_t out_summary_size);
static void app_update_runtime_telemetry(AppState* app);
static void app_log_hardware_profile(const AppState* app);
static void app_log_runtime_telemetry(AppState* app);

static const float k_app_default_time_of_day = 0.23f;
static const float k_app_default_day_cycle_speed = 1.0f / 180.0f;
static const float k_app_stats_update_interval_seconds = 0.25f;
#if defined(NDEBUG)
static const float k_app_runtime_log_interval_seconds = 5.0f;
#else
static const float k_app_runtime_log_interval_seconds = 2.0f;
#endif

int app_run(void);

#endif
