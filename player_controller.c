#include "player_controller.h"

#include "terrain.h"

#include <math.h>
#include <string.h>

static const float k_player_eye_height = 1.62f;
static const float k_player_crouch_eye_height = 1.02f;
static const float k_player_head_height = 0.18f;
static const float k_player_radius = 0.34f;
static const float k_player_walk_speed = 6.2f;
static const float k_player_creative_speed = 13.5f;
static const float k_player_fast_multiplier = 1.85f;
static const float k_player_crouch_speed_multiplier = 0.42f;
static const float k_player_gravity = 28.0f;
static const float k_player_jump_velocity = 10.5f;
static const float k_player_max_fall_speed = 24.0f;
static const float k_player_max_step_height = 0.75f;
static const float k_player_terrain_probe_radius = 0.16f;

void player_controller_init(PlayerController* controller, const SceneSettings* settings)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;

  if (controller == NULL)
  {
    return;
  }

  memset(controller, 0, sizeof(*controller));
  controller->camera.x = 0.0f;
  controller->camera.z = -3.0f;
  controller->camera.yaw = 3.14159265f;
  controller->camera.pitch = -0.18f;
  controller->camera.y = terrain_get_render_height(controller->camera.x, controller->camera.z, active_settings) + k_player_eye_height + 0.05f;
  controller->eye_height = k_player_eye_height;
  controller->mode = PLAYER_MODE_SURVIVAL;
  controller->selected_block = BLOCK_TYPE_GRASS;
  controller->on_ground = 1;
  controller->crouching = 0;
}

void player_controller_apply_look(PlayerController* controller, const PlatformInput* input)
{
  if (controller == NULL || input == NULL)
  {
    return;
  }

  controller->camera.yaw -= input->look_x * 0.01f;
  controller->camera.pitch = camera_clamp_pitch(controller->camera.pitch - input->look_y * 0.01f);
}

void player_controller_update(
  PlayerController* controller,
  const PlatformInput* input,
  float delta_seconds,
  const BlockWorld* world,
  const SceneSettings* settings
)
{
  if (controller == NULL || input == NULL || delta_seconds <= 0.0f)
  {
    return;
  }

  if (controller->mode == PLAYER_MODE_CREATIVE)
  {
    player_controller_update_creative(controller, input, delta_seconds);
    return;
  }

  player_controller_update_survival(controller, input, delta_seconds, world, settings);
}

void player_controller_sync_to_world(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings)
{
  float support_floor = 0.0f;
  int attempts = 0;

  if (controller == NULL || controller->mode != PLAYER_MODE_SURVIVAL)
  {
    return;
  }

  support_floor = player_controller_get_support_floor(controller, world, settings);
  if (controller->on_ground != 0 || player_controller_get_feet_y(controller) < support_floor)
  {
    controller->camera.y = support_floor + player_controller_get_eye_height(controller);
    controller->vertical_velocity = 0.0f;
    controller->on_ground = 1;
  }

  while (player_controller_position_collides_with_blocks(controller, world, controller->camera.x, controller->camera.y, controller->camera.z) && attempts < 32)
  {
    controller->camera.y += 0.25f;
    attempts += 1;
  }
}

void player_controller_toggle_mode(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings)
{
  if (controller == NULL)
  {
    return;
  }

  player_controller_set_mode(
    controller,
    (controller->mode == PLAYER_MODE_CREATIVE) ? PLAYER_MODE_SURVIVAL : PLAYER_MODE_CREATIVE,
    world,
    settings);
}

void player_controller_set_mode(PlayerController* controller, PlayerMode mode, const BlockWorld* world, const SceneSettings* settings)
{
  if (controller == NULL)
  {
    return;
  }

  controller->mode = mode;
  controller->vertical_velocity = 0.0f;
  controller->on_ground = 0;
  controller->eye_height = k_player_eye_height;
  controller->crouching = 0;

  if (mode == PLAYER_MODE_SURVIVAL)
  {
    player_controller_snap_to_safe_ground(controller, world, settings);
  }
}

void player_controller_set_selected_block(PlayerController* controller, BlockType type)
{
  if (controller == NULL || type <= BLOCK_TYPE_NONE || type >= BLOCK_TYPE_COUNT)
  {
    return;
  }

  controller->selected_block = type;
}

float player_controller_get_reach_distance(const PlayerController* controller)
{
  if (controller != NULL && controller->mode == PLAYER_MODE_CREATIVE)
  {
    return 11.0f;
  }

  return 7.0f;
}

float player_controller_get_eye_height(const PlayerController* controller)
{
  if (controller == NULL || controller->eye_height <= 0.01f)
  {
    return k_player_eye_height;
  }

  return controller->eye_height;
}

const char* player_controller_get_mode_label(PlayerMode mode)
{
  switch (mode)
  {
    case PLAYER_MODE_CREATIVE:
      return "Creative";
    case PLAYER_MODE_SURVIVAL:
    default:
      return "Survival";
  }
}

void player_controller_get_aabb(
  const PlayerController* controller,
  float* out_min_x,
  float* out_min_y,
  float* out_min_z,
  float* out_max_x,
  float* out_max_y,
  float* out_max_z
)
{
  if (controller == NULL)
  {
    return;
  }

  player_controller_get_aabb_for_position(
    controller->camera.x,
    controller->camera.y,
    controller->camera.z,
    player_controller_get_eye_height(controller),
    out_min_x,
    out_min_y,
    out_min_z,
    out_max_x,
    out_max_y,
    out_max_z);
}

int player_controller_would_overlap_block(const PlayerController* controller, int x, int y, int z)
{
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;

  if (controller == NULL)
  {
    return 0;
  }

  player_controller_get_aabb(controller, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z);
  if (max_x <= (float)x || min_x >= (float)x + 1.0f)
  {
    return 0;
  }
  if (max_y <= (float)y || min_y >= (float)y + 1.0f)
  {
    return 0;
  }
  if (max_z <= (float)z || min_z >= (float)z + 1.0f)
  {
    return 0;
  }

  return 1;
}

static float player_controller_get_feet_y(const PlayerController* controller)
{
  return (controller != NULL) ? (controller->camera.y - player_controller_get_eye_height(controller)) : 0.0f;
}

static float player_controller_sample_terrain_floor(float x, float z, const SceneSettings* settings)
{
  const float diagonal_offset = k_player_terrain_probe_radius * 0.70710678f;
  const float sample_offsets[9][2] = {
    { 0.0f, 0.0f },
    { k_player_terrain_probe_radius, 0.0f },
    { -k_player_terrain_probe_radius, 0.0f },
    { 0.0f, k_player_terrain_probe_radius },
    { 0.0f, -k_player_terrain_probe_radius },
    { diagonal_offset, diagonal_offset },
    { diagonal_offset, -diagonal_offset },
    { -diagonal_offset, diagonal_offset },
    { -diagonal_offset, -diagonal_offset }
  };
  float highest_floor = terrain_get_render_height(x, z, settings);
  int i = 0;

  for (i = 0; i < 9; ++i)
  {
    const float sample_height = terrain_get_render_height(x + sample_offsets[i][0], z + sample_offsets[i][1], settings);
    if (sample_height > highest_floor)
    {
      highest_floor = sample_height;
    }
  }

  return highest_floor;
}

static int player_controller_position_collides_with_blocks(const PlayerController* controller, const BlockWorld* world, float x, float y, float z)
{
  return player_controller_position_collides_with_eye_height(world, x, y, z, player_controller_get_eye_height(controller));
}

static int player_controller_position_collides_with_eye_height(const BlockWorld* world, float x, float y, float z, float eye_height)
{
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;

  player_controller_get_aabb_for_position(x, y, z, eye_height, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z);
  return block_world_box_intersects(world, min_x, min_y, min_z, max_x, max_y, max_z);
}

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
)
{
  if (out_min_x != NULL)
  {
    *out_min_x = x - k_player_radius;
  }
  if (out_min_y != NULL)
  {
    *out_min_y = y - eye_height;
  }
  if (out_min_z != NULL)
  {
    *out_min_z = z - k_player_radius;
  }
  if (out_max_x != NULL)
  {
    *out_max_x = x + k_player_radius;
  }
  if (out_max_y != NULL)
  {
    *out_max_y = y + k_player_head_height;
  }
  if (out_max_z != NULL)
  {
    *out_max_z = z + k_player_radius;
  }
}

static void player_controller_update_crouch_state(PlayerController* controller, const PlatformInput* input, const BlockWorld* world)
{
  const float feet_y = player_controller_get_feet_y(controller);

  if (controller == NULL)
  {
    return;
  }

  if (controller->mode != PLAYER_MODE_SURVIVAL)
  {
    controller->crouching = 0;
    controller->eye_height = k_player_eye_height;
    return;
  }

  if (input != NULL && input->crouch_held != 0)
  {
    controller->crouching = 1;
    controller->eye_height = k_player_crouch_eye_height;
    controller->camera.y = feet_y + controller->eye_height;
    return;
  }

  if (controller->crouching != 0 &&
    !player_controller_position_collides_with_eye_height(world, controller->camera.x, feet_y + k_player_eye_height, controller->camera.z, k_player_eye_height))
  {
    controller->crouching = 0;
    controller->eye_height = k_player_eye_height;
    controller->camera.y = feet_y + controller->eye_height;
  }
}

static void player_controller_update_creative(PlayerController* controller, const PlatformInput* input, float delta_seconds)
{
  const float speed = k_player_creative_speed * (input->move_fast_held ? k_player_fast_multiplier : 1.0f);
  float forward_x = 0.0f;
  float forward_y = 0.0f;
  float forward_z = -1.0f;
  float right_x = 1.0f;
  float right_z = 0.0f;
  float move_x = 0.0f;
  float move_y = 0.0f;
  float move_z = 0.0f;
  float move_length = 0.0f;

  camera_get_forward_vector(&controller->camera, &forward_x, &forward_y, &forward_z);
  camera_get_right_vector(&controller->camera, &right_x, &right_z);
  move_x = forward_x * input->move_forward + right_x * input->move_right;
  move_y = forward_y * input->move_forward + (float)(input->jump_held - input->move_down_held);
  move_z = forward_z * input->move_forward + right_z * input->move_right;
  move_length = sqrtf(move_x * move_x + move_y * move_y + move_z * move_z);

  if (move_length > 0.0001f)
  {
    const float move_scale = (speed * delta_seconds) / move_length;
    controller->camera.x += move_x * move_scale;
    controller->camera.y += move_y * move_scale;
    controller->camera.z += move_z * move_scale;
  }

  controller->eye_height = k_player_eye_height;
  controller->crouching = 0;
  controller->vertical_velocity = 0.0f;
  controller->on_ground = 0;
}

static void player_controller_update_survival(
  PlayerController* controller,
  const PlatformInput* input,
  float delta_seconds,
  const BlockWorld* world,
  const SceneSettings* settings
)
{
  const float speed = k_player_walk_speed *
    (input->move_fast_held ? k_player_fast_multiplier : 1.0f) *
    (input->crouch_held ? k_player_crouch_speed_multiplier : 1.0f);
  float forward_x = 0.0f;
  float forward_z = -1.0f;
  float right_x = 1.0f;
  float right_z = 0.0f;
  float move_x = 0.0f;
  float move_z = 0.0f;
  float move_length = 0.0f;
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;
  float terrain_floor = 0.0f;
  float feet_y = 0.0f;
  float candidate_y = 0.0f;
  float candidate_feet_y = 0.0f;
  float block_floor = 0.0f;
  float ceiling_y = 0.0f;
  int block_floor_found = 0;

  player_controller_update_crouch_state(controller, input, world);
  camera_get_flat_forward_vector(&controller->camera, &forward_x, &forward_z);
  camera_get_right_vector(&controller->camera, &right_x, &right_z);
  move_x = forward_x * input->move_forward + right_x * input->move_right;
  move_z = forward_z * input->move_forward + right_z * input->move_right;
  move_length = sqrtf(move_x * move_x + move_z * move_z);

  if (move_length > 0.0001f)
  {
    const float move_scale = (speed * delta_seconds) / move_length;
    player_controller_move_horizontal_axis(controller, world, settings, move_x * move_scale, 1);
    player_controller_move_horizontal_axis(controller, world, settings, move_z * move_scale, 0);
  }

  if (controller->on_ground != 0 && input->jump_pressed != 0)
  {
    controller->vertical_velocity = k_player_jump_velocity;
    controller->on_ground = 0;
  }

  controller->vertical_velocity -= k_player_gravity * delta_seconds;
  if (controller->vertical_velocity < -k_player_max_fall_speed)
  {
    controller->vertical_velocity = -k_player_max_fall_speed;
  }

  candidate_y = controller->camera.y + controller->vertical_velocity * delta_seconds;
  player_controller_get_aabb(controller, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z);
  feet_y = player_controller_get_feet_y(controller);
  candidate_feet_y = candidate_y - player_controller_get_eye_height(controller);

  if (controller->vertical_velocity > 0.0f &&
    block_world_find_ceiling(world, min_x, max_x, min_z, max_z, controller->camera.y + k_player_head_height, candidate_y + k_player_head_height, &ceiling_y))
  {
    controller->camera.y = ceiling_y - k_player_head_height;
    controller->vertical_velocity = 0.0f;
  }
  else
  {
    controller->camera.y = candidate_y;
  }

  terrain_floor = player_controller_get_support_floor(controller, world, settings);
  block_floor_found = block_world_find_floor(
    world,
    controller->camera.x - k_player_radius,
    controller->camera.x + k_player_radius,
    controller->camera.z - k_player_radius,
    controller->camera.z + k_player_radius,
    candidate_feet_y - 0.25f,
    feet_y + 0.25f,
    &block_floor);
  if (block_floor_found && block_floor > terrain_floor)
  {
    terrain_floor = block_floor;
  }

  if (player_controller_get_feet_y(controller) <= terrain_floor)
  {
    controller->camera.y = terrain_floor + player_controller_get_eye_height(controller);
    controller->vertical_velocity = 0.0f;
    controller->on_ground = 1;
  }
  else
  {
    controller->on_ground = 0;
  }
}

static void player_controller_move_horizontal_axis(
  PlayerController* controller,
  const BlockWorld* world,
  const SceneSettings* settings,
  float delta,
  int move_x_axis
)
{
  const float step_size = 0.10f;
  const int step_count = (int)(fabsf(delta) / step_size) + 1;
  const float step_delta = delta / (float)step_count;
  int step_index = 0;

  for (step_index = 0; step_index < step_count; ++step_index)
  {
    const float candidate_x = move_x_axis ? (controller->camera.x + step_delta) : controller->camera.x;
    const float candidate_z = move_x_axis ? controller->camera.z : (controller->camera.z + step_delta);
    const float current_feet_y = player_controller_get_feet_y(controller);
    const float candidate_floor = player_controller_sample_terrain_floor(candidate_x, candidate_z, settings);

    if (controller->on_ground != 0 && candidate_floor > current_feet_y + k_player_max_step_height)
    {
      break;
    }
    if (player_controller_position_collides_with_blocks(controller, world, candidate_x, controller->camera.y, candidate_z))
    {
      break;
    }

    controller->camera.x = candidate_x;
    controller->camera.z = candidate_z;
  }
}

static float player_controller_get_support_floor(const PlayerController* controller, const BlockWorld* world, const SceneSettings* settings)
{
  float terrain_floor = 0.0f;
  float block_floor = 0.0f;
  float min_x = 0.0f;
  float min_y = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  float max_z = 0.0f;

  if (controller == NULL)
  {
    return 0.0f;
  }

  terrain_floor = player_controller_sample_terrain_floor(controller->camera.x, controller->camera.z, settings);
  player_controller_get_aabb(controller, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z);
  if (block_world_find_floor(world, min_x, max_x, min_z, max_z, min_y - 2.0f, max_y + 2.0f, &block_floor) && block_floor > terrain_floor)
  {
    terrain_floor = block_floor;
  }

  return terrain_floor;
}

static void player_controller_snap_to_safe_ground(PlayerController* controller, const BlockWorld* world, const SceneSettings* settings)
{
  float terrain_floor = 0.0f;
  int attempts = 0;

  if (controller == NULL)
  {
    return;
  }

  terrain_floor = player_controller_get_support_floor(controller, world, settings);
  if (player_controller_get_feet_y(controller) < terrain_floor)
  {
    controller->camera.y = terrain_floor + player_controller_get_eye_height(controller);
  }

  while (player_controller_position_collides_with_blocks(controller, world, controller->camera.x, controller->camera.y, controller->camera.z) && attempts < 32)
  {
    controller->camera.y += 0.25f;
    attempts += 1;
  }

  controller->on_ground = 1;
}
