#ifndef PALM_RENDER_H
#define PALM_RENDER_H

#include "camera.h"
#include "gl_headers.h"
#include "render_quality.h"
#include "scene_settings.h"
#include "terrain.h"
#include "view_frustum.h"

#include <stddef.h>

enum
{
  PALM_RENDER_MAX_VARIANTS = 10
};

typedef enum PalmRenderCategory
{
  PALM_RENDER_CATEGORY_PALM = 0,
  PALM_RENDER_CATEGORY_TREE = 1,
  PALM_RENDER_CATEGORY_GRASS = 2,
  PALM_RENDER_CATEGORY_MOUNTAIN = 3,
  PALM_RENDER_CATEGORY_HOUSE = 4
} PalmRenderCategory;

typedef struct PalmRenderVariant
{
  GLuint vao;
  GLuint vertex_buffer;
  GLuint instance_buffer;
  GLuint diffuse_texture;
  GLsizei vertex_count;
  GLsizei instance_count;
  GLsizei visible_instance_count;
  void* cpu_instances;
  void* cpu_visible_instances;
  size_t cpu_instance_capacity;
  size_t cpu_visible_instance_capacity;
  float model_height;
  float model_radius;
  int category;
  float desired_height_min;
  float desired_height_max;
  float scale_jitter_min;
  float scale_jitter_max;
  float embed_depth_min;
  float embed_depth_max;
  float slope_limit;
} PalmRenderVariant;

typedef struct PalmRenderMesh
{
  PalmRenderVariant variants[PALM_RENDER_MAX_VARIANTS];
  int variant_count;
  int cache_valid;
  int cache_grid_min_x;
  int cache_grid_max_x;
  int cache_grid_min_z;
  int cache_grid_max_z;
  int cache_house_grid_min_x;
  int cache_house_grid_max_x;
  int cache_house_grid_min_z;
  int cache_house_grid_max_z;
  float cache_radius;
  float cache_cell_size;
  float cache_house_radius;
  float cache_house_cell_size;
  float cache_palm_size;
  float cache_palm_count;
  float cache_palm_fruit_density;
  float cache_palm_render_radius;
  float cache_terrain_base_height;
  float cache_terrain_height_scale;
  float cache_terrain_roughness;
  float cache_terrain_ridge_strength;
} PalmRenderMesh;

int palm_render_create(PalmRenderMesh* mesh);
int palm_render_create_category(PalmRenderMesh* mesh, PalmRenderCategory category);
void palm_render_destroy(PalmRenderMesh* mesh);
int palm_render_update_category(
  PalmRenderMesh* mesh,
  PalmRenderCategory category,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
int palm_render_update_category_with_frustum(
  PalmRenderMesh* mesh,
  PalmRenderCategory category,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality,
  const ViewFrustum* frustum);
int palm_render_update(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
int palm_render_collect_contact_patches(
  const PalmRenderMesh* mesh,
  float camera_x,
  float camera_z,
  TerrainContactPatch* patches,
  float* patch_distances,
  int patch_count,
  int patch_capacity);
void palm_render_draw(const PalmRenderMesh* mesh);

#endif
