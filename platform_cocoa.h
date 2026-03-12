#ifndef PLATFORM_COCOA_H
#define PLATFORM_COCOA_H

#include "gpu_preferences.h"
#include "overlay_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PlatformInput
{
  float look_x;
  float look_y;
  float move_forward;
  float move_right;
  int escape_pressed;
  int toggle_cycle_pressed;
  int reset_cycle_pressed;
  int increase_cycle_speed_pressed;
  int decrease_cycle_speed_pressed;
  int scrub_backward_held;
  int scrub_forward_held;
  int scrub_fast_held;
  int move_fast_held;
  int crouch_held;
  int jump_pressed;
  int jump_held;
  int move_down_held;
  int toggle_player_mode_pressed;
  int remove_block_pressed;
  int place_block_pressed;
  int selected_block_slot;
} PlatformInput;

typedef struct PlatformApp
{
  void* application;
  void* window;
  void* view;
  void* gl_context;
  void* window_delegate;
  double timer_start;
  int mouse_dx;
  int mouse_dy;
  int width;
  int height;
  int running;
  int resized;
  int escape_requested;
  int previous_toggle_cycle_down;
  int previous_reset_cycle_down;
  int previous_increase_speed_down;
  int previous_decrease_speed_down;
  int previous_jump_down;
  int previous_player_mode_down;
  int previous_alt_down;
  int previous_fullscreen_down;
  int cursor_hidden;
  int mouse_captured;
  int cursor_mode_enabled;
  int suppress_next_mouse_delta;
  int previous_world_left_button_down;
  int previous_world_right_button_down;
  int left_button_down;
  int right_button_down;
  int gpu_switch_requested;
  GpuPreferenceMode requested_gpu_preference;
  OverlayState overlay;
  unsigned char key_down[256];
} PlatformApp;

int platform_create(PlatformApp* app, const char* title, int width, int height);
void platform_destroy(PlatformApp* app);
void platform_pump_messages(PlatformApp* app, PlatformInput* input);
void platform_request_close(PlatformApp* app);
float platform_get_time_seconds(const PlatformApp* app);
void platform_swap_buffers(const PlatformApp* app);
void platform_set_window_title(const PlatformApp* app, const char* title);
void platform_get_scene_settings(const PlatformApp* app, SceneSettings* out_settings);
void platform_set_scene_settings(PlatformApp* app, const SceneSettings* settings);
int platform_get_god_mode_enabled(const PlatformApp* app);
void platform_set_god_mode_enabled(PlatformApp* app, int enabled);
void platform_update_overlay_metrics(PlatformApp* app, const OverlayMetrics* metrics);
int platform_consume_gpu_switch_request(PlatformApp* app, GpuPreferenceMode* out_mode);
void platform_refresh_gpu_info(PlatformApp* app);
void platform_show_error_message(const char* title, const char* message);

#ifdef __cplusplus
}
#endif

#endif
