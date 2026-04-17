#include "app.h"
#include "audio.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int app_run(void)
{
  AppState app = { 0 };
  AudioState music_audio = { 0 };
  GraphicsBackend graphics_backend = GRAPHICS_BACKEND_OPENGL;
  char graphics_error_message[256] = { 0 };
  int music_started = 0;

  audio_init(&music_audio);

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
    audio_shutdown(&music_audio);
    platform_destroy(&app.platform);
    return 1;
  }

  app_apply_renderer_quality_defaults(&app);
  platform_set_render_quality_preset(&app.platform, renderer_get_quality_preset(&app.renderer));
  system_monitor_create(&app.system_monitor);
  system_monitor_update(&app.system_monitor, &app.system_usage);
  app_log_hardware_profile(&app);
  platform_set_scene_settings(&app.platform, &app.scene_settings);
  renderer_sync_terrain_render_sampling(&app.renderer, &app.player.camera);
  app.previous_time_seconds = platform_get_time_seconds(&app.platform);
  diagnostics_log("app_run: entering main loop");

  while (app.platform.running)
  {
    PlatformInput input = { 0 };
    GpuPreferenceMode requested_gpu_preference = GPU_PREFERENCE_MODE_AUTO;
    RendererQualityPreset requested_quality_preset = RENDER_QUALITY_PRESET_HIGH;
    const SceneSettings previous_scene_settings = app.scene_settings;
    float current_time_seconds = 0.0f;
    float delta_seconds = 0.0f;

    platform_pump_messages(&app.platform, &input);
    if (!app.platform.running)
    {
      break;
    }
    audio_update(&music_audio);

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

    if (platform_consume_render_quality_request(&app.platform, &requested_quality_preset))
    {
      if (!renderer_set_quality_preset(&app.renderer, requested_quality_preset))
      {
        platform_show_error_message("Render Quality", "Failed to apply the selected render quality preset.");
        platform_request_close(&app.platform);
        continue;
      }

      app_apply_renderer_quality_defaults(&app);
      platform_set_render_quality_preset(&app.platform, renderer_get_quality_preset(&app.renderer));
      platform_set_scene_settings(&app.platform, &app.scene_settings);
      renderer_sync_terrain_render_sampling(&app.renderer, &app.player.camera);
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
        app.runtime_log_elapsed_seconds += delta_seconds;

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
      app.stats_display_metrics.night_duration_seconds = app.scene_settings.cycle_duration_seconds - app.stats_display_metrics.daylight_duration_seconds;
      app.stats_display_metrics.player_position_x = app.player.camera.x;
      app.stats_display_metrics.player_position_y = app.player.camera.y;
      app.stats_display_metrics.player_position_z = app.player.camera.z;
      app.stats_display_metrics.player_mode = (int)app.player.mode;
      app.stats_display_metrics.selected_block_type = (int)app.player.selected_block;
      app.stats_display_metrics.placed_block_count = block_world_get_cell_count(&app.block_world);
      app.stats_display_metrics.target_active = (target != NULL && target->valid != 0);
      app_update_runtime_telemetry(&app);

      metrics = app.stats_display_metrics;
      platform_update_overlay_metrics(&app.platform, &metrics);

      if (app.stats_display_metrics.stats_sample_index != 0U &&
        (app.last_health_status == OVERLAY_HEALTH_STATUS_UNKNOWN ||
          app.runtime_log_elapsed_seconds >= k_app_runtime_log_interval_seconds ||
          app.stats_display_metrics.health_status != app.last_health_status))
      {
        app_log_runtime_telemetry(&app);
        app.runtime_log_elapsed_seconds = 0.0f;
        app.last_health_status = app.stats_display_metrics.health_status;
      }

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
    if (music_started == 0)
    {
      (void)audio_start_music(&music_audio);
      music_started = 1;
    }
  }

  audio_shutdown(&music_audio);
  system_monitor_destroy(&app.system_monitor);
  renderer_destroy(&app.renderer);
  platform_destroy(&app.platform);
  diagnostics_log("app_run: shutdown complete");
  return 0;
}

static void app_apply_renderer_quality_defaults(AppState* app)
{
  if (app == NULL)
  {
    return;
  }

  if (app->renderer.quality.preset == RENDER_QUALITY_PRESET_ULTRA_LOW)
  {
    int changed = 0;

    if (app->scene_settings.clouds_enabled != 0)
    {
      app->scene_settings.clouds_enabled = 0;
      changed = 1;
    }
    if (app->scene_settings.palm_render_radius > 96.0f)
    {
      app->scene_settings.palm_render_radius = 96.0f;
      changed = 1;
    }

    if (changed != 0)
    {
      diagnostics_logf(
        "app_run: applied low-end gpu defaults quality=%s clouds=%d palm_radius=%.1f",
        render_quality_preset_get_label(app->renderer.quality.preset),
        app->scene_settings.clouds_enabled,
        app->scene_settings.palm_render_radius);
    }
  }
}

static void app_copy_utf8(char* destination, size_t destination_size, const char* source)
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

static const char* app_get_health_status_label(int status)
{
  switch ((OverlayHealthStatus)status)
  {
    case OVERLAY_HEALTH_STATUS_STABLE:
      return "STABLE";
    case OVERLAY_HEALTH_STATUS_WARM:
      return "WARM";
    case OVERLAY_HEALTH_STATUS_STRESSED:
      return "STRESSED";
    case OVERLAY_HEALTH_STATUS_CRITICAL:
      return "CRITICAL";
    case OVERLAY_HEALTH_STATUS_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

static const SystemGpuAdapterSample* app_find_system_gpu_adapter_sample(
  const SystemUsageSample* system_usage,
  unsigned int luid_low_part,
  int luid_high_part)
{
  int adapter_index = 0;

  if (system_usage == NULL)
  {
    return NULL;
  }

  for (adapter_index = 0; adapter_index < system_usage->gpu_adapter_count; ++adapter_index)
  {
    const SystemGpuAdapterSample* adapter = &system_usage->gpu_adapters[adapter_index];
    if (adapter->luid_low_part == luid_low_part && adapter->luid_high_part == luid_high_part)
    {
      return adapter;
    }
  }

  return NULL;
}

static int app_evaluate_health_status(
  const OverlayMetrics* metrics,
  const SystemUsageSample* system_usage,
  char* out_summary,
  size_t out_summary_size)
{
  const float fps = (metrics != NULL) ? metrics->frames_per_second : 0.0f;
  const float frame_time_ms = (metrics != NULL) ? metrics->frame_time_ms : 0.0f;
  const float cpu_percent = (system_usage != NULL) ? system_usage->cpu_percent : 0.0f;
  const float gpu_percent =
    (metrics != NULL && metrics->gpu0_usage_percent > metrics->gpu1_usage_percent)
      ? metrics->gpu0_usage_percent
      : ((metrics != NULL) ? metrics->gpu1_usage_percent : 0.0f);
  const float memory_percent = (system_usage != NULL) ? system_usage->system_memory_percent : 0.0f;
  const int thermal_available =
    (system_usage != NULL) &&
    (system_usage->gpu_temperature_available != 0 || system_usage->thermal_zone_temperature_available != 0);
  const float temperature_c =
    (system_usage != NULL && system_usage->gpu_temperature_available != 0)
      ? system_usage->gpu_temperature_c
      : ((system_usage != NULL) ? system_usage->thermal_zone_temperature_c : 0.0f);

  if (out_summary != NULL && out_summary_size > 0U)
  {
    out_summary[0] = '\0';
  }

  if (fps <= 0.01f && frame_time_ms <= 0.01f)
  {
    app_copy_utf8(out_summary, out_summary_size, "Collecting baseline");
    return OVERLAY_HEALTH_STATUS_UNKNOWN;
  }

  if ((thermal_available != 0 && temperature_c >= 88.0f) ||
    frame_time_ms >= 50.0f ||
    (fps > 0.0f && fps < 20.0f) ||
    memory_percent >= 95.0f)
  {
    if (thermal_available != 0 && temperature_c >= 88.0f)
    {
      app_copy_utf8(out_summary, out_summary_size, "Thermal critical");
    }
    else if (memory_percent >= 95.0f)
    {
      app_copy_utf8(out_summary, out_summary_size, "RAM nearly full");
    }
    else
    {
      app_copy_utf8(out_summary, out_summary_size, "Frame pacing critical");
    }
    return OVERLAY_HEALTH_STATUS_CRITICAL;
  }

  if ((thermal_available != 0 && temperature_c >= 80.0f) ||
    frame_time_ms >= 33.0f ||
    (fps > 0.0f && fps < 30.0f) ||
    cpu_percent >= 96.0f ||
    gpu_percent >= 98.0f ||
    memory_percent >= 90.0f)
  {
    if (thermal_available != 0 && temperature_c >= 80.0f)
    {
      app_copy_utf8(out_summary, out_summary_size, "Thermal elevated");
    }
    else if (memory_percent >= 90.0f)
    {
      app_copy_utf8(out_summary, out_summary_size, "RAM pressure high");
    }
    else
    {
      app_copy_utf8(out_summary, out_summary_size, "Load saturation");
    }
    return OVERLAY_HEALTH_STATUS_STRESSED;
  }

  if ((thermal_available != 0 && temperature_c >= 72.0f) ||
    frame_time_ms >= 22.0f ||
    (fps > 0.0f && fps < 50.0f) ||
    cpu_percent >= 82.0f ||
    gpu_percent >= 86.0f ||
    memory_percent >= 80.0f)
  {
    if (thermal_available != 0 && temperature_c >= 72.0f)
    {
      app_copy_utf8(out_summary, out_summary_size, "Thermal warming up");
    }
    else
    {
      app_copy_utf8(out_summary, out_summary_size, "Performance dipping");
    }
    return OVERLAY_HEALTH_STATUS_WARM;
  }

  app_copy_utf8(out_summary, out_summary_size, "Stable");
  return OVERLAY_HEALTH_STATUS_STABLE;
}

static void app_update_runtime_telemetry(AppState* app)
{
  OverlayMetrics* metrics = NULL;
  const GpuPreferenceInfo* gpu_info = NULL;
  const GpuAdapterInfo* gpu0_adapter = NULL;
  const GpuAdapterInfo* gpu1_adapter = NULL;
  const SystemGpuAdapterSample* gpu0_sample = NULL;
  const SystemGpuAdapterSample* gpu1_sample = NULL;
  int adapter_index = 0;
  int active_gpu_task_manager_index = GPU_PREFERENCES_INVALID_TASK_MANAGER_INDEX;
  char health_summary[OVERLAY_METRICS_HEALTH_SUMMARY_LENGTH] = { 0 };
  const char* active_gpu_name = NULL;

  if (app == NULL)
  {
    return;
  }

  metrics = &app->stats_display_metrics;
  gpu_info = &app->platform.overlay.gpu_info;

  metrics->gpu0_usage_percent = 0.0f;
  metrics->gpu1_usage_percent = 0.0f;
  metrics->gpu0_memory_usage_mb = 0.0f;
  metrics->gpu1_memory_usage_mb = 0.0f;
  metrics->active_gpu_task_manager_index = GPU_PREFERENCES_INVALID_TASK_MANAGER_INDEX;
  metrics->gpu0_name[0] = '\0';
  metrics->gpu1_name[0] = '\0';

  metrics->system_memory_percent = app->system_usage.system_memory_percent;
  metrics->system_memory_used_mb = app->system_usage.system_memory_used_mb;
  metrics->system_memory_total_mb = app->system_usage.system_memory_total_mb;
  metrics->thermal_zone_temperature_c = app->system_usage.thermal_zone_temperature_c;
  metrics->gpu_temperature_c = app->system_usage.gpu_temperature_c;
  metrics->thermal_zone_temperature_available = app->system_usage.thermal_zone_temperature_available;
  metrics->gpu_temperature_available = app->system_usage.gpu_temperature_available;
  app_copy_utf8(metrics->thermal_zone_label, sizeof(metrics->thermal_zone_label), app->system_usage.thermal_zone_name);

  gpu0_adapter = gpu_preferences_find_adapter_by_task_manager_index(gpu_info, 0);
  gpu1_adapter = gpu_preferences_find_adapter_by_task_manager_index(gpu_info, 1);
  if (gpu0_adapter != NULL)
  {
    gpu0_sample = app_find_system_gpu_adapter_sample(&app->system_usage, gpu0_adapter->luid_low_part, gpu0_adapter->luid_high_part);
    app_copy_utf8(metrics->gpu0_name, sizeof(metrics->gpu0_name), gpu0_adapter->name);
    if (gpu0_sample != NULL)
    {
      metrics->gpu0_usage_percent = gpu0_sample->usage_percent;
      metrics->gpu0_memory_usage_mb = gpu0_sample->memory_usage_mb;
    }
  }
  if (gpu1_adapter != NULL)
  {
    gpu1_sample = app_find_system_gpu_adapter_sample(&app->system_usage, gpu1_adapter->luid_low_part, gpu1_adapter->luid_high_part);
    app_copy_utf8(metrics->gpu1_name, sizeof(metrics->gpu1_name), gpu1_adapter->name);
    if (gpu1_sample != NULL)
    {
      metrics->gpu1_usage_percent = gpu1_sample->usage_percent;
      metrics->gpu1_memory_usage_mb = gpu1_sample->memory_usage_mb;
    }
  }

  active_gpu_name =
    (gpu_info->current_renderer[0] != '\0')
      ? gpu_info->current_renderer
      : ((gpu_info->adapter_count > 0) ? gpu_info->adapters[0].name : gpu_info->current_vendor);
  app_copy_utf8(metrics->active_gpu_name, sizeof(metrics->active_gpu_name), active_gpu_name);

  for (adapter_index = 0; adapter_index < gpu_info->adapter_count; ++adapter_index)
  {
    const GpuAdapterInfo* adapter = &gpu_info->adapters[adapter_index];
    if (adapter->name[0] == '\0' || adapter->task_manager_index < 0)
    {
      continue;
    }

    if (strstr(active_gpu_name, adapter->name) != NULL || strstr(adapter->name, active_gpu_name) != NULL)
    {
      active_gpu_task_manager_index = adapter->task_manager_index;
      break;
    }
  }

  if (active_gpu_task_manager_index < 0)
  {
    if (gpu1_adapter != NULL &&
      (strstr(active_gpu_name, "NVIDIA") != NULL || strstr(active_gpu_name, "Radeon") != NULL || strstr(active_gpu_name, "GeForce") != NULL))
    {
      active_gpu_task_manager_index = gpu1_adapter->task_manager_index;
    }
    else if (gpu0_adapter != NULL)
    {
      active_gpu_task_manager_index = gpu0_adapter->task_manager_index;
    }
  }

  metrics->active_gpu_task_manager_index = active_gpu_task_manager_index;

  metrics->health_status = app_evaluate_health_status(metrics, &app->system_usage, health_summary, sizeof(health_summary));
  app_copy_utf8(metrics->health_summary, sizeof(metrics->health_summary), health_summary);
}

static void app_log_hardware_profile(const AppState* app)
{
  const GpuPreferenceInfo* gpu_info = NULL;
  int adapter_index = 0;
  char gpu_temp_text[64] = { 0 };
  char zone_temp_text[96] = { 0 };

  if (app == NULL)
  {
    return;
  }

  gpu_info = &app->platform.overlay.gpu_info;
  if (app->system_usage.gpu_temperature_available != 0)
  {
    (void)snprintf(gpu_temp_text, sizeof(gpu_temp_text), "%.1fC", app->system_usage.gpu_temperature_c);
  }
  else
  {
    (void)snprintf(gpu_temp_text, sizeof(gpu_temp_text), "unavailable");
  }
  if (app->system_usage.thermal_zone_temperature_available != 0)
  {
    (void)snprintf(
      zone_temp_text,
      sizeof(zone_temp_text),
      "%.1fC (%s)",
      app->system_usage.thermal_zone_temperature_c,
      app->system_usage.thermal_zone_name);
  }
  else
  {
    (void)snprintf(zone_temp_text, sizeof(zone_temp_text), "unavailable");
  }

  diagnostics_logf(
    "hardware_profile: build=%s renderer=%s vendor=%s gpu_route=%s ram_total=%.0fMB gpu_temp=%s zone_temp=%s",
#if defined(NDEBUG)
    "Release",
#else
    "Debug",
#endif
    gpu_info->current_renderer[0] != '\0' ? gpu_info->current_renderer : "OpenGL",
    gpu_info->current_vendor[0] != '\0' ? gpu_info->current_vendor : "unknown",
    gpu_preferences_get_mode_label(gpu_info->selected_mode),
    app->system_usage.system_memory_total_mb,
    gpu_temp_text,
    zone_temp_text);

  for (adapter_index = 0; adapter_index < gpu_info->adapter_count; ++adapter_index)
  {
    const GpuAdapterInfo* adapter = &gpu_info->adapters[adapter_index];
    diagnostics_logf(
      "hardware_profile: adapter[%d]=%s vram=%uMB eco=%d high=%d",
      adapter_index,
      adapter->name,
      adapter->dedicated_video_memory_mb,
      adapter->is_minimum_power_candidate,
      adapter->is_high_performance_candidate);
    if (adapter->task_manager_index >= 0)
    {
      diagnostics_logf(
        "hardware_profile: taskmgr_gpu%d=%s luid=0x%08x_0x%08x",
        adapter->task_manager_index,
        adapter->name,
        (unsigned int)adapter->luid_high_part,
        adapter->luid_low_part);
    }
  }
}

static void app_log_runtime_telemetry(AppState* app)
{
  const OverlayMetrics* metrics = NULL;
  char gpu_temp_text[64] = { 0 };
  char zone_temp_text[96] = { 0 };
  char active_gpu_text[192] = { 0 };

  if (app == NULL)
  {
    return;
  }

  metrics = &app->stats_display_metrics;
  if (metrics->gpu_temperature_available != 0)
  {
    (void)snprintf(gpu_temp_text, sizeof(gpu_temp_text), "%.1fC", metrics->gpu_temperature_c);
  }
  else
  {
    (void)snprintf(gpu_temp_text, sizeof(gpu_temp_text), "unavailable");
  }

  if (metrics->thermal_zone_temperature_available != 0)
  {
    (void)snprintf(
      zone_temp_text,
      sizeof(zone_temp_text),
      "%.1fC (%s)",
      metrics->thermal_zone_temperature_c,
      metrics->thermal_zone_label[0] != '\0' ? metrics->thermal_zone_label : "thermal-zone");
  }
  else
  {
    (void)snprintf(zone_temp_text, sizeof(zone_temp_text), "unavailable");
  }

  if (metrics->active_gpu_task_manager_index >= 0)
  {
    (void)snprintf(
      active_gpu_text,
      sizeof(active_gpu_text),
      "GPU %d (%s)",
      metrics->active_gpu_task_manager_index,
      metrics->active_gpu_name[0] != '\0' ? metrics->active_gpu_name : "OpenGL");
  }
  else
  {
    (void)snprintf(
      active_gpu_text,
      sizeof(active_gpu_text),
      "%s",
      metrics->active_gpu_name[0] != '\0' ? metrics->active_gpu_name : "OpenGL");
  }

  diagnostics_logf(
    "telemetry: health=%s reason=%s fps=%.0f frame=%.2fms cpu=%.0f%% gpu0=\"%s\" %.0f%% gpu1=\"%s\" %.0f%% ram=%.0f%% (%.0f/%.0fMB) vram=%.0f/%.0fMB active_gpu=%s gpu_temp=%s zone_temp=%s pos=(%.1f,%.1f,%.1f)",
    app_get_health_status_label(metrics->health_status),
    metrics->health_summary[0] != '\0' ? metrics->health_summary : "n/a",
    metrics->frames_per_second,
    metrics->frame_time_ms,
    metrics->cpu_usage_percent,
    metrics->gpu0_name[0] != '\0' ? metrics->gpu0_name : "GPU 0",
    metrics->gpu0_usage_percent,
    metrics->gpu1_name[0] != '\0' ? metrics->gpu1_name : "GPU 1",
    metrics->gpu1_usage_percent,
    metrics->system_memory_percent,
    metrics->system_memory_used_mb,
    metrics->system_memory_total_mb,
    metrics->gpu0_memory_usage_mb,
    metrics->gpu1_memory_usage_mb,
    active_gpu_text,
    gpu_temp_text,
    zone_temp_text,
    metrics->player_position_x,
    metrics->player_position_y,
    metrics->player_position_z);
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
