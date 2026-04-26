#ifndef TERRAIN_H
#define TERRAIN_H

#include "scene_settings.h"

enum
{
  TERRAIN_CONTACT_PATCH_CAPACITY = 16
};

typedef struct TerrainRenderSamplingConfig
{
  float origin_x;
  float origin_z;
  float mesh_step;
  float half_extent;
  int valid;
} TerrainRenderSamplingConfig;

typedef struct TerrainContactPatch
{
  float x;
  float z;
  float target_y;
  float inner_radius;
  float outer_radius;
  float strength;
} TerrainContactPatch;

float terrain_get_base_height(float x, float z, const SceneSettings* settings);
float terrain_get_height(float x, float z, const SceneSettings* settings);
void terrain_set_render_sampling(const TerrainRenderSamplingConfig* config);
float terrain_get_render_base_height(float x, float z, const SceneSettings* settings);
float terrain_get_render_height(float x, float z, const SceneSettings* settings);
void terrain_set_contact_patches(const TerrainContactPatch* patches, int patch_count);
int terrain_get_contact_patches(TerrainContactPatch* out_patches, int max_patch_count);

#endif
