#include "block_world.h"
#include "terrain.h"

#include <float.h>
#include <math.h>
#include <string.h>

static int block_world_find_cell_index(const BlockWorld* world, int x, int y, int z);
static int block_world_get_support_height(int x, int z, const SceneSettings* settings);
static int block_world_can_place_block(const BlockWorld* world, int x, int y, int z, BlockType type, const SceneSettings* settings);
static void block_world_seed_spawn_marker(BlockWorld* world, const SceneSettings* settings);
static int block_world_box_overlaps(
  float min_ax,
  float min_ay,
  float min_az,
  float max_ax,
  float max_ay,
  float max_az,
  float min_bx,
  float min_by,
  float min_bz,
  float max_bx,
  float max_by,
  float max_bz
);

void block_world_init(BlockWorld* world, const SceneSettings* settings)
{
  if (world == NULL)
  {
    return;
  }

  memset(world, 0, sizeof(*world));
  block_world_seed_spawn_marker(world, settings);
  block_world_refresh(world, settings);
}

void block_world_refresh(BlockWorld* world, const SceneSettings* settings)
{
  int cell_index = 0;

  if (world == NULL)
  {
    return;
  }

  for (cell_index = 0; cell_index < world->cell_count; ++cell_index)
  {
    BlockWorldCell* cell = &world->cells[cell_index];
    cell->y = block_world_get_support_height(cell->x, cell->z, settings) + cell->terrain_offset_y;
  }
}

void block_world_update_target(BlockWorld* world, const CameraState* camera, const SceneSettings* settings, float max_distance)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  const float step_distance = 0.12f;
  float direction_x = 0.0f;
  float direction_y = 0.0f;
  float direction_z = -1.0f;
  float sample_distance = 0.0f;
  int previous_cell_valid = 0;
  int previous_x = 0;
  int previous_y = 0;
  int previous_z = 0;

  if (world == NULL)
  {
    return;
  }

  memset(&world->target, 0, sizeof(world->target));
  if (camera == NULL)
  {
    return;
  }

  camera_get_forward_vector(camera, &direction_x, &direction_y, &direction_z);
  if (max_distance < 1.0f)
  {
    max_distance = 1.0f;
  }

  for (sample_distance = 0.25f; sample_distance <= max_distance; sample_distance += step_distance)
  {
    const float sample_x = camera->x + direction_x * sample_distance;
    const float sample_y = camera->y + direction_y * sample_distance;
    const float sample_z = camera->z + direction_z * sample_distance;
    const float terrain_height = terrain_get_render_height(sample_x, sample_z, active_settings);
    const int cell_x = (int)floorf(sample_x);
    const int cell_y = (int)floorf(sample_y);
    const int cell_z = (int)floorf(sample_z);
    const int cell_index = block_world_find_cell_index(world, cell_x, cell_y, cell_z);

    if (cell_index >= 0)
    {
      world->target.valid = 1;
      world->target.kind = BLOCK_RAYCAST_BLOCK;
      world->target.block_x = cell_x;
      world->target.block_y = cell_y;
      world->target.block_z = cell_z;
      world->target.place_x = previous_cell_valid ? previous_x : cell_x;
      world->target.place_y = previous_cell_valid ? previous_y : (cell_y + 1);
      world->target.place_z = previous_cell_valid ? previous_z : cell_z;
      world->target.distance = sample_distance;
      world->target.block_type = world->cells[cell_index].type;
      return;
    }

    if (sample_y <= terrain_height)
    {
      world->target.valid = 1;
      world->target.kind = BLOCK_RAYCAST_TERRAIN;
      world->target.block_x = (int)floorf(sample_x);
      world->target.block_y = (int)floorf(terrain_height);
      world->target.block_z = (int)floorf(sample_z);
      world->target.place_x = world->target.block_x;
      world->target.place_y = block_world_get_support_height(world->target.place_x, world->target.place_z, active_settings);
      world->target.place_z = world->target.block_z;
      world->target.distance = sample_distance;
      world->target.block_type = BLOCK_TYPE_NONE;
      return;
    }

    previous_cell_valid = 1;
    previous_x = cell_x;
    previous_y = cell_y;
    previous_z = cell_z;
  }
}

const BlockRaycastTarget* block_world_get_target(const BlockWorld* world)
{
  if (world == NULL)
  {
    return NULL;
  }

  return &world->target;
}

int block_world_place_block(BlockWorld* world, int x, int y, int z, BlockType type, const SceneSettings* settings)
{
  if (world == NULL || !block_world_can_place_block(world, x, y, z, type, settings))
  {
    return 0;
  }

  world->cells[world->cell_count].x = x;
  world->cells[world->cell_count].y = y;
  world->cells[world->cell_count].z = z;
  world->cells[world->cell_count].terrain_offset_y = y - block_world_get_support_height(x, z, settings);
  world->cells[world->cell_count].type = type;
  world->cell_count += 1;
  return 1;
}

int block_world_remove_block(BlockWorld* world, int x, int y, int z)
{
  const int cell_index = block_world_find_cell_index(world, x, y, z);

  if (world == NULL || cell_index < 0)
  {
    return 0;
  }

  world->cell_count -= 1;
  world->cells[cell_index] = world->cells[world->cell_count];
  memset(&world->cells[world->cell_count], 0, sizeof(world->cells[world->cell_count]));
  return 1;
}

int block_world_is_occupied(const BlockWorld* world, int x, int y, int z)
{
  return block_world_find_cell_index(world, x, y, z) >= 0;
}

int block_world_box_intersects(const BlockWorld* world, float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
{
  int i = 0;

  if (world == NULL)
  {
    return 0;
  }

  for (i = 0; i < world->cell_count; ++i)
  {
    const BlockWorldCell* cell = &world->cells[i];
    if (block_world_box_overlaps(
          min_x,
          min_y,
          min_z,
          max_x,
          max_y,
          max_z,
          (float)cell->x,
          (float)cell->y,
          (float)cell->z,
          (float)cell->x + 1.0f,
          (float)cell->y + 1.0f,
          (float)cell->z + 1.0f))
    {
      return 1;
    }
  }

  return 0;
}

int block_world_find_floor(
  const BlockWorld* world,
  float min_x,
  float max_x,
  float min_z,
  float max_z,
  float min_y,
  float max_y,
  float* out_floor_y
)
{
  float best_floor = -FLT_MAX;
  int found = 0;
  int i = 0;

  if (world == NULL)
  {
    return 0;
  }

  for (i = 0; i < world->cell_count; ++i)
  {
    const BlockWorldCell* cell = &world->cells[i];
    const float block_top = (float)cell->y + 1.0f;

    if ((float)cell->x + 1.0f <= min_x || (float)cell->x >= max_x || (float)cell->z + 1.0f <= min_z || (float)cell->z >= max_z)
    {
      continue;
    }
    if (block_top < min_y || block_top > max_y)
    {
      continue;
    }
    if (!found || block_top > best_floor)
    {
      best_floor = block_top;
      found = 1;
    }
  }

  if (found && out_floor_y != NULL)
  {
    *out_floor_y = best_floor;
  }
  return found;
}

int block_world_find_ceiling(
  const BlockWorld* world,
  float min_x,
  float max_x,
  float min_z,
  float max_z,
  float min_y,
  float max_y,
  float* out_ceiling_y
)
{
  float best_ceiling = FLT_MAX;
  int found = 0;
  int i = 0;

  if (world == NULL)
  {
    return 0;
  }

  for (i = 0; i < world->cell_count; ++i)
  {
    const BlockWorldCell* cell = &world->cells[i];
    const float block_bottom = (float)cell->y;

    if ((float)cell->x + 1.0f <= min_x || (float)cell->x >= max_x || (float)cell->z + 1.0f <= min_z || (float)cell->z >= max_z)
    {
      continue;
    }
    if (block_bottom < min_y || block_bottom > max_y)
    {
      continue;
    }
    if (!found || block_bottom < best_ceiling)
    {
      best_ceiling = block_bottom;
      found = 1;
    }
  }

  if (found && out_ceiling_y != NULL)
  {
    *out_ceiling_y = best_ceiling;
  }
  return found;
}

const BlockWorldCell* block_world_get_cells(const BlockWorld* world, int* out_count)
{
  if (out_count != NULL)
  {
    *out_count = (world != NULL) ? world->cell_count : 0;
  }

  return (world != NULL) ? world->cells : NULL;
}

int block_world_get_cell_count(const BlockWorld* world)
{
  return (world != NULL) ? world->cell_count : 0;
}

const char* block_world_get_block_label(BlockType type)
{
  switch (type)
  {
    case BLOCK_TYPE_GRASS:
      return "Grass";
    case BLOCK_TYPE_STONE:
      return "Stone";
    case BLOCK_TYPE_WOOD:
      return "Wood";
    case BLOCK_TYPE_GLOW:
      return "Glow";
    case BLOCK_TYPE_NONE:
    case BLOCK_TYPE_COUNT:
    default:
      return "None";
  }
}

void block_world_get_block_color(BlockType type, float* out_r, float* out_g, float* out_b)
{
  float r = 0.70f;
  float g = 0.72f;
  float b = 0.76f;

  switch (type)
  {
    case BLOCK_TYPE_GRASS:
      r = 0.33f;
      g = 0.64f;
      b = 0.24f;
      break;

    case BLOCK_TYPE_STONE:
      r = 0.56f;
      g = 0.58f;
      b = 0.62f;
      break;

    case BLOCK_TYPE_WOOD:
      r = 0.63f;
      g = 0.44f;
      b = 0.24f;
      break;

    case BLOCK_TYPE_GLOW:
      r = 0.96f;
      g = 0.78f;
      b = 0.30f;
      break;

    case BLOCK_TYPE_NONE:
    case BLOCK_TYPE_COUNT:
    default:
      break;
  }

  if (out_r != NULL)
  {
    *out_r = r;
  }
  if (out_g != NULL)
  {
    *out_g = g;
  }
  if (out_b != NULL)
  {
    *out_b = b;
  }
}

static int block_world_find_cell_index(const BlockWorld* world, int x, int y, int z)
{
  int i = 0;

  if (world == NULL)
  {
    return -1;
  }

  for (i = 0; i < world->cell_count; ++i)
  {
    const BlockWorldCell* cell = &world->cells[i];
    if (cell->x == x && cell->y == y && cell->z == z)
    {
      return i;
    }
  }

  return -1;
}

static int block_world_get_support_height(int x, int z, const SceneSettings* settings)
{
  const float sample_offsets[9][2] = {
    { 0.15f, 0.15f },
    { 0.50f, 0.15f },
    { 0.85f, 0.15f },
    { 0.15f, 0.50f },
    { 0.50f, 0.50f },
    { 0.85f, 0.50f },
    { 0.15f, 0.85f },
    { 0.50f, 0.85f },
    { 0.85f, 0.85f }
  };
  float highest_terrain = terrain_get_render_height((float)x + 0.5f, (float)z + 0.5f, settings);
  int sample_index = 0;

  for (sample_index = 0; sample_index < 9; ++sample_index)
  {
    const float sample_height = terrain_get_render_height((float)x + sample_offsets[sample_index][0], (float)z + sample_offsets[sample_index][1], settings);
    if (sample_height > highest_terrain)
    {
      highest_terrain = sample_height;
    }
  }

  return (int)ceilf(highest_terrain - 0.001f);
}

static int block_world_can_place_block(const BlockWorld* world, int x, int y, int z, BlockType type, const SceneSettings* settings)
{
  const int minimum_y = block_world_get_support_height(x, z, settings);

  if (world == NULL || type <= BLOCK_TYPE_NONE || type >= BLOCK_TYPE_COUNT)
  {
    return 0;
  }
  if (world->cell_count >= BLOCK_WORLD_MAX_BLOCKS)
  {
    return 0;
  }
  if (block_world_find_cell_index(world, x, y, z) >= 0)
  {
    return 0;
  }
  if (y < minimum_y)
  {
    return 0;
  }

  return 1;
}

static void block_world_seed_spawn_marker(BlockWorld* world, const SceneSettings* settings)
{
  int x = 0;
  int z = 0;
  int center_y = 0;

  if (world == NULL)
  {
    return;
  }

  for (z = 2; z <= 6; ++z)
  {
    for (x = -2; x <= 2; ++x)
    {
      const int y = block_world_get_support_height(x, z, settings);
      (void)block_world_place_block(world, x, y, z, BLOCK_TYPE_STONE, settings);
    }
  }

  center_y = block_world_get_support_height(0, 4, settings);
  (void)block_world_place_block(world, 0, center_y + 1, 4, BLOCK_TYPE_WOOD, settings);
  (void)block_world_place_block(world, 0, center_y + 2, 4, BLOCK_TYPE_WOOD, settings);
  (void)block_world_place_block(world, 0, center_y + 3, 4, BLOCK_TYPE_GLOW, settings);
}

static int block_world_box_overlaps(
  float min_ax,
  float min_ay,
  float min_az,
  float max_ax,
  float max_ay,
  float max_az,
  float min_bx,
  float min_by,
  float min_bz,
  float max_bx,
  float max_by,
  float max_bz
)
{
  if (max_ax <= min_bx || min_ax >= max_bx)
  {
    return 0;
  }
  if (max_ay <= min_by || min_ay >= max_by)
  {
    return 0;
  }
  if (max_az <= min_bz || min_az >= max_bz)
  {
    return 0;
  }

  return 1;
}
