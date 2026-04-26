#ifndef RENDERER_H
#define RENDERER_H

#include "atmosphere.h"
#include "block_world.h"
#include "camera.h"
#include "console_overlay.h"
#include "grass_render.h"
#include "gl_headers.h"
#include "mountain_render.h"
#include "overlay_ui.h"
#include "palm_render.h"
#include "render_quality.h"
#include "stats_overlay.h"
#include "tree_render.h"

typedef struct Renderer
{
  GLuint sky_vao;
  GLuint post_vao;
  GLuint terrain_vao;
  GLuint terrain_vertex_buffer;
  GLuint terrain_index_buffer;
  GLuint shadow_terrain_vao;
  GLuint shadow_terrain_vertex_buffer;
  GLuint shadow_terrain_index_buffer;
  GLuint palm_program;
  GLuint palm_shadow_program;
  GLuint sky_program;
  GLuint post_program;
  GLuint terrain_program;
  GLuint shadow_program;
  GLuint framebuffer;
  GLuint color_texture;
  GLuint depth_texture;
  GLuint shadow_framebuffer;
  GLuint shadow_texture;
  GLint sky_projection_location;
  GLint sky_view_location;
  GLint sky_cloud_time_location;
  GLint sky_sun_direction_location;
  GLint sky_sun_distance_location;
  GLint sky_cirrus_location;
  GLint sky_cumulus_location;
  GLint sky_cloud_settings_location;
  GLint sky_quality_location;
  GLint terrain_projection_location;
  GLint terrain_view_location;
  GLint terrain_light_view_projection_location;
  GLint terrain_origin_location;
  GLint terrain_shape_location;
  GLint terrain_sun_direction_location;
  GLint terrain_sun_distance_location;
  GLint terrain_camera_position_location;
  GLint terrain_shadow_map_location;
  GLint terrain_lighting_quality_location;
  GLint terrain_environment_location;
  GLint terrain_contact_count_location;
  GLint terrain_contact_data_location;
  GLint terrain_contact_params_location;
  GLint palm_projection_location;
  GLint palm_view_location;
  GLint palm_light_view_projection_location;
  GLint palm_sun_direction_location;
  GLint palm_sun_distance_location;
  GLint palm_camera_position_location;
  GLint palm_shadow_map_location;
  GLint palm_diffuse_map_location;
  GLint palm_terrain_shape_location;
  GLint palm_environment_location;
  GLint palm_lighting_quality_location;
  GLint palm_shadow_light_view_projection_location;
  GLint shadow_light_view_projection_location;
  GLint shadow_origin_location;
  GLint shadow_shape_location;
  GLint shadow_contact_count_location;
  GLint shadow_contact_data_location;
  GLint shadow_contact_params_location;
  GLint post_quality_location;
  PalmRenderMesh palm_mesh;
  MountainRenderMesh mountain_mesh;
  TreeRenderMesh tree_mesh;
  GrassRenderMesh grass_mesh;
  ConsoleOverlay console_overlay;
  StatsOverlay stats_overlay;
  RendererQualityProfile quality;
  char renderer_name[256];
  char vendor_name[256];
  GLsizei terrain_index_count;
  GLsizei shadow_terrain_index_count;
  unsigned int frame_index;
  int shadow_ready;
  int width;
  int height;
  int framebuffer_width;
  int framebuffer_height;
} Renderer;

int renderer_create(Renderer* renderer, int width, int height);
void renderer_destroy(Renderer* renderer);
int renderer_resize(Renderer* renderer, int width, int height);
int renderer_set_quality_preset(Renderer* renderer, RendererQualityPreset preset);
RendererQualityPreset renderer_get_quality_preset(const Renderer* renderer);
void renderer_render(
  Renderer* renderer,
  const CameraState* camera,
  const AtmosphereState* atmosphere,
  const SceneSettings* settings,
  const OverlayState* overlay,
  const BlockWorld* block_world
);
void renderer_sync_terrain_render_sampling(const Renderer* renderer, const CameraState* camera);
float renderer_get_terrain_height(float x, float z, const SceneSettings* settings);

#endif
