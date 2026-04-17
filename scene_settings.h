#ifndef SCENE_SETTINGS_H
#define SCENE_SETTINGS_H

enum
{
  OVERLAY_METRICS_ACTIVE_GPU_NAME_LENGTH = 128,
  OVERLAY_METRICS_THERMAL_LABEL_LENGTH = 96,
  OVERLAY_METRICS_HEALTH_SUMMARY_LENGTH = 96
};

typedef enum OverlayHealthStatus
{
  OVERLAY_HEALTH_STATUS_UNKNOWN = 0,
  OVERLAY_HEALTH_STATUS_STABLE,
  OVERLAY_HEALTH_STATUS_WARM,
  OVERLAY_HEALTH_STATUS_STRESSED,
  OVERLAY_HEALTH_STATUS_CRITICAL
} OverlayHealthStatus;

typedef struct SceneSettings
{
  float sun_distance_mkm;
  float sun_orbit_degrees;
  float cycle_duration_seconds;
  float daylight_fraction;
  float camera_fov_degrees;
  float fog_density;
  float cloud_amount;
  float cloud_spacing;
  float terrain_base_height;
  float terrain_height_scale;
  float terrain_roughness;
  float terrain_ridge_strength;
  float palm_size;
  float palm_count;
  float palm_fruit_density;
  float palm_render_radius;
  int clouds_enabled;
} SceneSettings;

typedef struct OverlayMetrics
{
  float sun_distance_mkm;
  float daylight_duration_seconds;
  float night_duration_seconds;
  float frames_per_second;
  float frame_time_ms;
  float cpu_usage_percent;
  float gpu0_usage_percent;
  float gpu1_usage_percent;
  float player_position_x;
  float player_position_y;
  float player_position_z;
  int player_mode;
  int selected_block_type;
  int placed_block_count;
  int target_active;
  unsigned int stats_sample_index;
  float system_memory_percent;
  float system_memory_used_mb;
  float system_memory_total_mb;
  float gpu0_memory_usage_mb;
  float gpu1_memory_usage_mb;
  float thermal_zone_temperature_c;
  float gpu_temperature_c;
  int thermal_zone_temperature_available;
  int gpu_temperature_available;
  int health_status;
  int active_gpu_task_manager_index;
  char gpu0_name[OVERLAY_METRICS_ACTIVE_GPU_NAME_LENGTH];
  char gpu1_name[OVERLAY_METRICS_ACTIVE_GPU_NAME_LENGTH];
  char active_gpu_name[OVERLAY_METRICS_ACTIVE_GPU_NAME_LENGTH];
  char thermal_zone_label[OVERLAY_METRICS_THERMAL_LABEL_LENGTH];
  char health_summary[OVERLAY_METRICS_HEALTH_SUMMARY_LENGTH];
} OverlayMetrics;

static inline SceneSettings scene_settings_default(void)
{
  SceneSettings settings = {
    149.6f,
    82.8f,
    180.0f,
    0.5f,
    65.0f,
    0.24f,
    0.54f,
    1.0f,
    -40.0f,
    0.35f,
    0.55f,
    0.0f,
    0.55f,
    0.0f,
    0.0f,
    120.0f,
    1
  };
  return settings;
}

#endif
