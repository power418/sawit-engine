#include "app.h"

#include "block_world.h"
#include "diagnostics.h"
#include "graphics_backend.h"
#include "gpu_preferences.h"
#include "player_controller.h"
#include "platform_support.h"
#include "renderer.h"
#include "scene_settings.h"
#include "system_monitor.h"
#include "terrain.h"

#if defined(_WIN32)
#include "platform_win32.h"
#elif defined(__APPLE__)
#include "platform_cocoa.h"
#else
#error Unsupported platform
#endif

#include <math.h>
#include <stdio.h>

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
  OverlayMetrics stats_display_metrics;
  float atmosphere_elapsed_seconds;
} AppState;

static int app_terrain_settings_changed(const SceneSettings* previous_settings, const SceneSettings* current_settings);
static float app_wrap_unit_interval(float value);
static float app_wrap_degrees(float value);
static BlockType app_get_block_type_for_slot(int slot);
static void app_apply_world_actions(AppState* app, const PlatformInput* input);
static void app_update_day_cycle(DayCycleState* cycle, const PlatformInput* input, float delta_seconds, int freeze_time_enabled);
static void app_update_atmosphere(
  const DayCycleState* cycle,
  const SceneSettings* settings,
  AtmosphereState* atmosphere,
  float cloud_time_seconds
);
static void app_update_window_title(const AppState* app);

static const float k_app_default_time_of_day = 0.23f;
static const float k_app_default_day_cycle_speed = 1.0f / 180.0f;
static const float k_app_stats_update_interval_seconds = 0.25f;

int app_run(void)
{
  AppState app = { 0 };
  GraphicsBackend graphics_backend = GRAPHICS_BACKEND_OPENGL;
  char graphics_error_message[256] = { 0 };

  diagnostics_log("app_run: startup begin");
  graphics_backend = graphics_backend_resolve_requested();
  diagnostics_logf("app_run: requested_graphics_backend=%s", graphics_backend_get_name(graphics_backend));

  if (!graphics_backend_is_supported_on_platform(graphics_backend))
  {
    if (!graphics_backend_build_error_message(graphics_backend, graphics_error_message, sizeof(graphics_error_message)))
    {
      (void)snprintf(graphics_error_message, sizeof(graphics_error_message), "Graphics backend '%s' tidak didukung.", graphics_backend_get_name(graphics_backend));
    }
    diagnostics_logf("app_run: unsupported_graphics_backend=%s", graphics_backend_get_name(graphics_backend));
    platform_support_show_error_dialog("Graphics Backend Not Supported", graphics_error_message);
    return 1;
  }

#if defined(__APPLE__)
  if (!graphics_backend_build_error_message(graphics_backend, graphics_error_message, sizeof(graphics_error_message)))
  {
    (void)snprintf(
      graphics_error_message,
      sizeof(graphics_error_message),
      "Backend '%s' belum siap dipakai di macOS.",
      graphics_backend_get_name(graphics_backend));
  }
  diagnostics_logf("app_run: apple_backend_guard=%s", graphics_backend_get_name(graphics_backend));
  platform_support_show_error_dialog("Metal Renderer Required", graphics_error_message);
  return 1;
#endif

  app.scene_settings = scene_settings_default();
  app.day_cycle.time_of_day = k_app_default_time_of_day;
  app.day_cycle.cycles_per_second = 1.0f / app.scene_settings.cycle_duration_seconds;
  app.day_cycle.auto_advance = 1;
  player_controller_init(&app.player, &app.scene_settings);
  block_world_init(&app.block_world, &app.scene_settings);

  if (!platform_create(&app.platform, "OpenGL Sky", 1280, 720))
  {
    diagnostics_log("app_run: platform_create failed");
    return 1;
  }

  if (!renderer_create(&app.renderer, app.platform.width, app.platform.height))
  {
    diagnostics_log("app_run: renderer_create failed");
    platform_destroy(&app.platform);
    return 1;
  }

  system_monitor_create(&app.system_monitor);
  platform_set_scene_settings(&app.platform, &app.scene_settings);
  renderer_sync_terrain_render_sampling(&app.renderer, &app.player.camera);
  app.previous_time_seconds = platform_get_time_seconds(&app.platform);
  diagnostics_log("app_run: entering main loop");

  while (app.platform.running)
  {
    PlatformInput input = { 0 };
    GpuPreferenceMode requested_gpu_preference = GPU_PREFERENCE_MODE_AUTO;
    const SceneSettings previous_scene_settings = app.scene_settings;
    float current_time_seconds = 0.0f;
    float delta_seconds = 0.0f;

    platform_pump_messages(&app.platform, &input);
    if (!app.platform.running)
    {
      break;
    }

    if (platform_consume_gpu_switch_request(&app.platform, &requested_gpu_preference))
    {
      if (gpu_preferences_apply_and_relaunch(requested_gpu_preference))
      {
        app.platform.running = 0;
        continue;
      }

      platform_refresh_gpu_info(&app.platform);
      platform_show_error_message("GPU Switch", "Failed to apply the GPU preference and relaunch the app.");
    }

    if (input.escape_pressed != 0)
    {
      platform_request_close(&app.platform);
      continue;
    }

    if (app.platform.resized != 0)
    {
      if (!renderer_resize(&app.renderer, app.platform.width, app.platform.height))
      {
        platform_request_close(&app.platform);
        continue;
      }
      app.platform.resized = 0;
    }

    current_time_seconds = platform_get_time_seconds(&app.platform);
    app.platform.overlay.ui_time_seconds = current_time_seconds;
    delta_seconds = current_time_seconds - app.previous_time_seconds;
    if (delta_seconds < 0.0f)
    {
      delta_seconds = 0.0f;
    }
    else if (delta_seconds > 0.25f)
    {
      delta_seconds = 0.25f;
    }
    app.previous_time_seconds = current_time_seconds;
    system_monitor_update(&app.system_monitor, &app.system_usage);

    if (app.platform.width <= 0 || app.platform.height <= 0)
    {
      platform_support_sleep_ms(16U);
      continue;
    }

    platform_get_scene_settings(&app.platform, &app.scene_settings);
    app.scene_settings.sun_orbit_degrees = app_wrap_degrees(app.scene_settings.sun_orbit_degrees);
    if (app.scene_settings.cycle_duration_seconds < 60.0f)
    {
      app.scene_settings.cycle_duration_seconds = 60.0f;
    }
    renderer_sync_terrain_render_sampling(&app.renderer, &app.player.camera);
    block_world_refresh(&app.block_world, &app.scene_settings);
    if (app_terrain_settings_changed(&previous_scene_settings, &app.scene_settings))
    {
      player_controller_sync_to_world(&app.player, &app.block_world, &app.scene_settings);
    }

    {
      PlayerMode desired_mode = platform_get_god_mode_enabled(&app.platform) ? PLAYER_MODE_CREATIVE : PLAYER_MODE_SURVIVAL;

      if (input.toggle_player_mode_pressed != 0)
      {
        desired_mode = (app.player.mode == PLAYER_MODE_CREATIVE) ? PLAYER_MODE_SURVIVAL : PLAYER_MODE_CREATIVE;
        platform_set_god_mode_enabled(&app.platform, desired_mode == PLAYER_MODE_CREATIVE);
      }

      if (app.player.mode != desired_mode)
      {
        player_controller_set_mode(&app.player, desired_mode, &app.block_world, &app.scene_settings);
      }
    }

    if (input.selected_block_slot >= 0)
    {
      player_controller_set_selected_block(&app.player, app_get_block_type_for_slot(input.selected_block_slot));
    }

    player_controller_apply_look(&app.player, &input);
    player_controller_update(&app.player, &input, delta_seconds, &app.block_world, &app.scene_settings);
    renderer_sync_terrain_render_sampling(&app.renderer, &app.player.camera);

    app.day_cycle.cycles_per_second = 1.0f / app.scene_settings.cycle_duration_seconds;
    if (app.platform.overlay.freeze_time_enabled != 0)
    {
      const float normalized_orbit = app.scene_settings.sun_orbit_degrees / 360.0f;
      app.day_cycle.time_of_day = (normalized_orbit >= 0.9997f) ? 0.9997f : normalized_orbit;
    }
    app_update_day_cycle(&app.day_cycle, &input, delta_seconds, app.platform.overlay.freeze_time_enabled);
    if (app.day_cycle.cycles_per_second > 0.0f)
    {
      app.scene_settings.cycle_duration_seconds = 1.0f / app.day_cycle.cycles_per_second;
    }
    app.scene_settings.sun_orbit_degrees = app_wrap_degrees(app.day_cycle.time_of_day * 360.0f);
    platform_set_scene_settings(&app.platform, &app.scene_settings);

    if (app.platform.overlay.freeze_time_enabled == 0)
    {
      app.atmosphere_elapsed_seconds += delta_seconds;
    }
    app_update_atmosphere(&app.day_cycle, &app.scene_settings, &app.atmosphere, app.atmosphere_elapsed_seconds);
    block_world_update_target(
      &app.block_world,
      &app.player.camera,
      &app.scene_settings,
      player_controller_get_reach_distance(&app.player));
    app_apply_world_actions(&app, &input);
    block_world_update_target(
      &app.block_world,
      &app.player.camera,
      &app.scene_settings,
      player_controller_get_reach_distance(&app.player));

    {
      OverlayMetrics metrics = app.stats_display_metrics;
      const float terrain_floor = terrain_get_render_height(app.player.camera.x, app.player.camera.z, &app.scene_settings);
      const BlockRaycastTarget* target = block_world_get_target(&app.block_world);

      if (delta_seconds > 0.0001f)
      {
        app.stats_update_elapsed_seconds += delta_seconds;
        app.stats_accumulated_delta_seconds += delta_seconds;
        app.stats_accumulated_frames += 1;

        if (app.stats_display_metrics.stats_sample_index == 0U ||
          app.stats_update_elapsed_seconds >= k_app_stats_update_interval_seconds)
        {
          const float averaged_delta_seconds = app.stats_accumulated_delta_seconds / (float)app.stats_accumulated_frames;
          const float fps_sample = (averaged_delta_seconds > 0.0001f) ? (1.0f / averaged_delta_seconds) : 0.0f;

          app.stats_display_metrics.frames_per_second = fps_sample;
          app.stats_display_metrics.frame_time_ms = averaged_delta_seconds * 1000.0f;
          app.stats_display_metrics.cpu_usage_percent = app.system_usage.cpu_percent;
          app.stats_display_metrics.gpu0_usage_percent = app.system_usage.gpu0_percent;
          app.stats_display_metrics.gpu1_usage_percent = app.system_usage.gpu1_percent;
          app.stats_display_metrics.stats_sample_index += 1U;

          app.stats_update_elapsed_seconds = 0.0f;
          app.stats_accumulated_delta_seconds = 0.0f;
          app.stats_accumulated_frames = 0;
        }
      }

      app.stats_display_metrics.sun_distance_mkm = app.scene_settings.sun_distance_mkm;
      app.stats_display_metrics.daylight_duration_seconds = app.scene_settings.cycle_duration_seconds * app.scene_settings.daylight_fraction;
      app.stats_display_metrics.night_duration_seconds =
        app.scene_settings.cycle_duration_seconds - app.stats_display_metrics.daylight_duration_seconds;
      app.stats_display_metrics.player_mode = (int)app.player.mode;
      app.stats_display_metrics.selected_block_type = (int)app.player.selected_block;
      app.stats_display_metrics.placed_block_count = block_world_get_cell_count(&app.block_world);
      app.stats_display_metrics.target_active = (target != NULL && target->valid != 0);

      metrics = app.stats_display_metrics;
      platform_update_overlay_metrics(&app.platform, &metrics);

      if (app.player.mode == PLAYER_MODE_SURVIVAL && app.player.camera.y < terrain_floor + player_controller_get_eye_height(&app.player))
      {
        app.player.camera.y = terrain_floor + player_controller_get_eye_height(&app.player);
      }
    }

    app_update_window_title(&app);
    renderer_render(
      &app.renderer,
      &app.player.camera,
      &app.atmosphere,
      &app.scene_settings,
      &app.platform.overlay,
      &app.block_world);
    platform_swap_buffers(&app.platform);
  }

  system_monitor_destroy(&app.system_monitor);
  renderer_destroy(&app.renderer);
  platform_destroy(&app.platform);
  diagnostics_log("app_run: shutdown complete");
  return 0;
}

static float app_wrap_unit_interval(float value)
{
  value = fmodf(value, 1.0f);
  if (value < 0.0f)
  {
    value += 1.0f;
  }
  return value;
}

static float app_wrap_degrees(float value)
{
  value = fmodf(value, 360.0f);
  if (value < 0.0f)
  {
    value += 360.0f;
  }
  return value;
}

static int app_terrain_settings_changed(const SceneSettings* previous_settings, const SceneSettings* current_settings)
{
  if (previous_settings == NULL || current_settings == NULL)
  {
    return 0;
  }

  return previous_settings->terrain_base_height != current_settings->terrain_base_height ||
    previous_settings->terrain_height_scale != current_settings->terrain_height_scale ||
    previous_settings->terrain_roughness != current_settings->terrain_roughness ||
    previous_settings->terrain_ridge_strength != current_settings->terrain_ridge_strength;
}

static BlockType app_get_block_type_for_slot(int slot)
{
  switch (slot)
  {
    case 0:
      return BLOCK_TYPE_GRASS;
    case 1:
      return BLOCK_TYPE_STONE;
    case 2:
      return BLOCK_TYPE_WOOD;
    case 3:
      return BLOCK_TYPE_GLOW;
    default:
      return BLOCK_TYPE_GRASS;
  }
}

static void app_apply_world_actions(AppState* app, const PlatformInput* input)
{
  const BlockRaycastTarget* target = NULL;

  if (app == NULL || input == NULL)
  {
    return;
  }

  target = block_world_get_target(&app->block_world);
  if (target == NULL || target->valid == 0)
  {
    return;
  }

  if (input->remove_block_pressed != 0 && target->kind == BLOCK_RAYCAST_BLOCK)
  {
    (void)block_world_remove_block(&app->block_world, target->block_x, target->block_y, target->block_z);
  }

  if (input->place_block_pressed != 0 &&
    !block_world_is_occupied(&app->block_world, target->place_x, target->place_y, target->place_z) &&
    !player_controller_would_overlap_block(&app->player, target->place_x, target->place_y, target->place_z))
  {
    (void)block_world_place_block(
      &app->block_world,
      target->place_x,
      target->place_y,
      target->place_z,
      app->player.selected_block,
      &app->scene_settings);
  }
}

static void app_update_day_cycle(DayCycleState* cycle, const PlatformInput* input, float delta_seconds, int freeze_time_enabled)
{
  const float min_cycles_per_second = 1.0f / 600.0f;
  const float max_cycles_per_second = 1.0f / 20.0f;
  const float speed_multiplier = 1.5f;
  const float scrub_cycles_per_second = input->scrub_fast_held ? 0.20f : 0.06f;
  float cycle_delta = 0.0f;

  if (cycle->cycles_per_second <= 0.0f)
  {
    cycle->cycles_per_second = k_app_default_day_cycle_speed;
  }

  if (input->toggle_cycle_pressed != 0)
  {
    cycle->auto_advance = (cycle->auto_advance == 0);
  }

  if (input->reset_cycle_pressed != 0)
  {
    cycle->time_of_day = k_app_default_time_of_day;
    cycle->cycles_per_second = k_app_default_day_cycle_speed;
    cycle->auto_advance = 1;
  }

  if (input->increase_cycle_speed_pressed != 0)
  {
    cycle->cycles_per_second *= speed_multiplier;
    if (cycle->cycles_per_second > max_cycles_per_second)
    {
      cycle->cycles_per_second = max_cycles_per_second;
    }
  }

  if (input->decrease_cycle_speed_pressed != 0)
  {
    cycle->cycles_per_second /= speed_multiplier;
    if (cycle->cycles_per_second < min_cycles_per_second)
    {
      cycle->cycles_per_second = min_cycles_per_second;
    }
  }

  if (freeze_time_enabled == 0 && cycle->auto_advance != 0)
  {
    cycle_delta += cycle->cycles_per_second * delta_seconds;
  }
  if (freeze_time_enabled == 0 && input->scrub_backward_held != 0)
  {
    cycle_delta -= scrub_cycles_per_second * delta_seconds;
  }
  if (freeze_time_enabled == 0 && input->scrub_forward_held != 0)
  {
    cycle_delta += scrub_cycles_per_second * delta_seconds;
  }

  cycle->time_of_day = app_wrap_unit_interval(cycle->time_of_day + cycle_delta);
}

static void app_update_atmosphere(
  const DayCycleState* cycle,
  const SceneSettings* settings,
  AtmosphereState* atmosphere,
  float cloud_time_seconds
)
{
  const float daylight_fraction =
    (settings->daylight_fraction < 0.25f) ? 0.25f : ((settings->daylight_fraction > 0.75f) ? 0.75f : settings->daylight_fraction);
  const float sunrise = 0.5f - daylight_fraction * 0.5f;
  const float sunset = 0.5f + daylight_fraction * 0.5f;
  const float safe_night_duration = ((1.0f + sunrise) - sunset > 0.001f) ? ((1.0f + sunrise) - sunset) : 0.001f;
  const float orbital_azimuth_radians = -0.52f;
  float wrapped_time = cycle->time_of_day;
  float orbit_angle = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float length = 0.0f;
  float cloud_drift_speed = 0.0f;

  if (wrapped_time < sunrise)
  {
    wrapped_time += 1.0f;
  }

  if (wrapped_time < sunset)
  {
    orbit_angle = ((wrapped_time - sunrise) / (sunset - sunrise)) * 3.14159265f;
  }
  else
  {
    orbit_angle = 3.14159265f + ((wrapped_time - sunset) / safe_night_duration) * 3.14159265f;
  }

  /* Sample-style solar orbit in the sky dome, rotated a bit so shadows are not perfectly front-back. */
  x = cosf(orbit_angle) * sinf(orbital_azimuth_radians);
  y = sinf(orbit_angle);
  z = cosf(orbit_angle) * cosf(orbital_azimuth_radians);
  length = sqrtf(x * x + y * y + z * z);
  cloud_drift_speed = 0.55f + 0.45f / ((settings->cloud_spacing > 0.45f) ? settings->cloud_spacing : 0.45f);

  atmosphere->cloud_time_seconds = cloud_time_seconds * cloud_drift_speed;
  atmosphere->time_of_day = cycle->time_of_day;
  atmosphere->sun_direction[0] = x / length;
  atmosphere->sun_direction[1] = y / length;
  atmosphere->sun_direction[2] = z / length;
}

static void app_update_window_title(const AppState* app)
{
  char title[256] = { 0 };
  const float cycles_per_second = app->day_cycle.cycles_per_second;
  const float cycle_duration_seconds = (cycles_per_second > 0.0f) ? (1.0f / cycles_per_second) : 0.0f;
  const int total_minutes = ((int)(app->day_cycle.time_of_day * 24.0f * 60.0f + 0.5f)) % (24 * 60);
  const int hours = total_minutes / 60;
  const int minutes = total_minutes % 60;

  #if defined(__APPLE__)
  (void)snprintf(
    title,
    sizeof(title),
    "OpenGL Sky | %02d:%02d | %s | %s | block %s | cycle %.0fs | G mode | 1-4 blocks | LMB break | RMB place | Alt free cursor",
    hours,
    minutes,
    (app->platform.overlay.freeze_time_enabled != 0) ? "Frozen" : ((app->day_cycle.auto_advance != 0) ? "Auto" : "Paused"),
    player_controller_get_mode_label(app->player.mode),
    block_world_get_block_label(app->player.selected_block),
    cycle_duration_seconds);
  #else
  (void)snprintf(
    title,
    sizeof(title),
    "OpenGL Sky | %02d:%02d | %s | %s | block %s | cycle %.0fs | G mode | 1-4 blocks | LMB break | RMB place | Alt overlay",
    hours,
    minutes,
    (app->platform.overlay.freeze_time_enabled != 0) ? "Frozen" : ((app->day_cycle.auto_advance != 0) ? "Auto" : "Paused"),
    player_controller_get_mode_label(app->player.mode),
    block_world_get_block_label(app->player.selected_block),
    cycle_duration_seconds);
  #endif
  platform_set_window_title(&app->platform, title);
}
