#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include "block_world.h"
#include "camera.h"
#include "platform_win32.h"
#include "scene_settings.h"

typedef enum PlayerMode
{
  PLAYER_MODE_CREATIVE = 0,
  PLAYER_MODE_SURVIVAL
} PlayerMode;

typedef struct PlayerController
{
  CameraState camera;
  PlayerMode mode;
  BlockType selected_block;
  float vertical_velocity;
  float eye_height;
  int on_ground;
  int crouching;
} PlayerController;

static float player_controller_get_feet_y(const PlayerController* controller);
static float player_controller_sample_terrain_floor(float x, float z, const SceneSettings* settings);
static int player_controller_position_collides_with_blocks(const PlayerController* controller, const BlockWorld* world, float x, float y, float z);
static int player_controller_position_collides_with_eye_height(const BlockWorld* world, float x, float y, float z, float eye_height);
static void player_controller_get_aabb_for_position(
  float x,
  float y,
  float z,
  float eye_height,
  float* out_min_x,
  float* out_min_y,
  float* out_min_z,
  float* out_max_x,
  float* out_max_y,
  float* out_max_z
);
static void player_controller_update_crouch_state(PlayerController* controller, const PlatformInput* input, const BlockWorld* world);
static void player_controller_update_creative(PlayerController* controller, const PlatformInput* input, float delta_seconds);
static void player_controller_update_survival(
  PlayerController* controller,
  const PlatformInput* input,
  float delta_seconds,
  const BlockWorld* world,
  const SceneSettings* settings
);
static void player_controller_move_horizontal_axis(
  PlayerController* controller,
  const BlockWorld* world,
  const SceneSettings* settings,
  float delta,
  int move_x_axis
);
static float player_controller_get_support_floor(const PlayerController* controller, const BlockWorld* world, const SceneSettings* settings);
static void player_controller_snap_to_safe_ground(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings);

void player_controller_init(PlayerController* controller, const SceneSettings* settings);
void player_controller_apply_look(PlayerController* controller, const PlatformInput* input);
void player_controller_update(
  PlayerController* controller,
  const PlatformInput* input,
  float delta_seconds,
  const BlockWorld* world,
  const SceneSettings* settings
);
void player_controller_sync_to_world(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings);
void player_controller_toggle_mode(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings);
void player_controller_set_mode(PlayerController* controller, PlayerMode mode, const BlockWorld* world, const SceneSettings* settings);
void player_controller_set_selected_block(PlayerController* controller, BlockType type);
float player_controller_get_reach_distance(const PlayerController* controller);
float player_controller_get_eye_height(const PlayerController* controller);
const char* player_controller_get_mode_label(PlayerMode mode);
void player_controller_get_aabb(
  const PlayerController* controller,
  float* out_min_x,
  float* out_min_y,
  float* out_min_z,
  float* out_max_x,
  float* out_max_y,
  float* out_max_z
);
int player_controller_would_overlap_block(const PlayerController* controller, int x, int y, int z);

#endif
