#include "renderer.h"

#include "block_render.h"
#include "diagnostics.h"
#include "grass_render.h"
#include "math3d.h"
#include "mountain_render.h"
#include "palm_render.h"
#include "platform_support.h"
#include "terrain.h"
#include "tree_render.h"
#include "view_frustum.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TerrainVertex
{
  float position[2];
} TerrainVertex;

typedef struct RendererTextBuffer
{
  char* data;
  size_t length;
  size_t capacity;
} RendererTextBuffer;

static const float k_renderer_terrain_half_extent = 3200.0f;

static void renderer_show_error(const char* title, const char* message);
static const char* renderer_find_last_path_separator(const char* path);
static int renderer_file_exists(const char* path);
static int renderer_build_relative_path(const char* base_path, const char* relative_path, char* out_path, size_t out_path_size);
static int renderer_build_shader_path(const char* relative_path, char* out_path, size_t out_path_size);
static int renderer_text_buffer_append(RendererTextBuffer* buffer, const char* text, size_t text_length);
static int renderer_build_include_path(const char* source_path, const char* include_path, char* out_path, size_t out_path_size);
static int renderer_append_text_file_recursive(const char* path, const char* label, int depth, RendererTextBuffer* out_buffer);
static int renderer_load_raw_text_file(const char* path, const char* label, char** out_source);
static int renderer_load_text_file(const char* path, const char* label, char** out_source);
static int renderer_check_shader(GLuint shader, const char* label);
static int renderer_check_program(GLuint program, const char* label);
static GLuint renderer_compile_shader(GLenum shader_type, const char* source, const char* label);
static GLuint renderer_create_program_from_files(const char* vertex_path, const char* fragment_path, const char* label);
static void renderer_destroy_framebuffer(Renderer* renderer);
static int renderer_create_framebuffer(Renderer* renderer, int width, int height);
static void renderer_destroy_shadow_map(Renderer* renderer);
static int renderer_create_shadow_map(Renderer* renderer);
static void renderer_destroy_terrain(Renderer* renderer);
static int renderer_create_heightfield_mesh(
  GLuint* out_vao,
  GLuint* out_vertex_buffer,
  GLuint* out_index_buffer,
  GLsizei* out_index_count,
  int resolution);
static int renderer_create_terrain(Renderer* renderer);
static float renderer_get_terrain_step_for_resolution(int resolution);
static float renderer_get_terrain_step(const Renderer* renderer);
static double renderer_estimate_gpu_memory_mb(const Renderer* renderer, int framebuffer_width, int framebuffer_height);
static void renderer_get_terrain_origin(const Renderer* renderer, const CameraState* camera, float* out_x, float* out_z);
static void renderer_transform_point(const Matrix* matrix, float x, float y, float z, float w, float* out_x, float* out_y, float* out_z, float* out_w);
static Matrix renderer_get_stabilized_shadow_matrix(const Renderer* renderer, const Matrix* light_view, const Matrix* light_projection);
static Matrix renderer_get_light_view_projection_matrix(const Renderer* renderer, const CameraState* camera, const AtmosphereState* atmosphere, const SceneSettings* settings);
static void renderer_upload_terrain_contact_uniforms(GLint count_location, GLint data_location, GLint params_location, const TerrainContactPatch* patches, int patch_count);
static void renderer_log_quality_profile(const char* context, const Renderer* renderer);

int renderer_create(Renderer* renderer, int width, int height)
{
  const char* renderer_name = NULL;
  const char* vendor_name = NULL;

  memset(renderer, 0, sizeof(*renderer));
  renderer_name = (const char*)glGetString(GL_RENDERER);
  vendor_name = (const char*)glGetString(GL_VENDOR);
  (void)snprintf(renderer->renderer_name, sizeof(renderer->renderer_name), "%s", (renderer_name != NULL) ? renderer_name : "unknown");
  (void)snprintf(renderer->vendor_name, sizeof(renderer->vendor_name), "%s", (vendor_name != NULL) ? vendor_name : "unknown");
  renderer->quality = render_quality_pick(renderer_name, vendor_name);
  renderer_log_quality_profile("renderer_create", renderer);

  renderer->sky_program = renderer_create_program_from_files("shaders/sky.vert.glsl", "shaders/sky.frag.glsl", "Sky");
  if (renderer->sky_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  renderer->post_program = renderer_create_program_from_files("shaders/post.vert.glsl", "shaders/post.frag.glsl", "Post");
  if (renderer->post_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  renderer->terrain_program = renderer_create_program_from_files("shaders/terrain.vert.glsl", "shaders/terrain.frag.glsl", "Terrain");
  if (renderer->terrain_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  renderer->palm_program = renderer_create_program_from_files("shaders/palm.vert.glsl", "shaders/palm.frag.glsl", "Palm");
  if (renderer->palm_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  renderer->shadow_program = renderer_create_program_from_files("shaders/shadow.vert.glsl", "shaders/shadow.frag.glsl", "Shadow");
  if (renderer->shadow_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  renderer->palm_shadow_program = renderer_create_program_from_files("shaders/palm_shadow.vert.glsl", "shaders/palm_shadow.frag.glsl", "Palm Shadow");
  if (renderer->palm_shadow_program == 0U)
  {
    renderer_destroy(renderer);
    return 0;
  }

  glGenVertexArrays(1, &renderer->sky_vao);
  glGenVertexArrays(1, &renderer->post_vao);

  renderer->sky_projection_location = glGetUniformLocation(renderer->sky_program, "P");
  renderer->sky_view_location = glGetUniformLocation(renderer->sky_program, "V");
  renderer->sky_cloud_time_location = glGetUniformLocation(renderer->sky_program, "cloud_time");
  renderer->sky_sun_direction_location = glGetUniformLocation(renderer->sky_program, "sun_direction");
  renderer->sky_sun_distance_location = glGetUniformLocation(renderer->sky_program, "sun_distance_mkm");
  renderer->sky_cirrus_location = glGetUniformLocation(renderer->sky_program, "cirrus");
  renderer->sky_cumulus_location = glGetUniformLocation(renderer->sky_program, "cumulus");
  renderer->sky_cloud_settings_location = glGetUniformLocation(renderer->sky_program, "cloud_settings");
  renderer->sky_quality_location = glGetUniformLocation(renderer->sky_program, "sky_quality");
  renderer->terrain_projection_location = glGetUniformLocation(renderer->terrain_program, "P");
  renderer->terrain_view_location = glGetUniformLocation(renderer->terrain_program, "V");
  renderer->terrain_light_view_projection_location = glGetUniformLocation(renderer->terrain_program, "light_vp");
  renderer->terrain_origin_location = glGetUniformLocation(renderer->terrain_program, "terrain_origin");
  renderer->terrain_shape_location = glGetUniformLocation(renderer->terrain_program, "terrain_shape");
  renderer->terrain_sun_direction_location = glGetUniformLocation(renderer->terrain_program, "sun_direction");
  renderer->terrain_sun_distance_location = glGetUniformLocation(renderer->terrain_program, "sun_distance_mkm");
  renderer->terrain_camera_position_location = glGetUniformLocation(renderer->terrain_program, "camera_position");
  renderer->terrain_shadow_map_location = glGetUniformLocation(renderer->terrain_program, "shadow_map");
  renderer->terrain_lighting_quality_location = glGetUniformLocation(renderer->terrain_program, "lighting_quality");
  renderer->terrain_environment_location = glGetUniformLocation(renderer->terrain_program, "environment_settings");
  renderer->terrain_contact_count_location = glGetUniformLocation(renderer->terrain_program, "terrain_contact_count");
  renderer->terrain_contact_data_location = glGetUniformLocation(renderer->terrain_program, "terrain_contact_data[0]");
  renderer->terrain_contact_params_location = glGetUniformLocation(renderer->terrain_program, "terrain_contact_params[0]");
  renderer->palm_projection_location = glGetUniformLocation(renderer->palm_program, "P");
  renderer->palm_view_location = glGetUniformLocation(renderer->palm_program, "V");
  renderer->palm_light_view_projection_location = glGetUniformLocation(renderer->palm_program, "light_vp");
  renderer->palm_sun_direction_location = glGetUniformLocation(renderer->palm_program, "sun_direction");
  renderer->palm_sun_distance_location = glGetUniformLocation(renderer->palm_program, "sun_distance_mkm");
  renderer->palm_camera_position_location = glGetUniformLocation(renderer->palm_program, "camera_position");
  renderer->palm_shadow_map_location = glGetUniformLocation(renderer->palm_program, "shadow_map");
  renderer->palm_diffuse_map_location = glGetUniformLocation(renderer->palm_program, "diffuse_map");
  renderer->palm_terrain_shape_location = glGetUniformLocation(renderer->palm_program, "terrain_shape");
  renderer->palm_environment_location = glGetUniformLocation(renderer->palm_program, "environment_settings");
  renderer->palm_lighting_quality_location = glGetUniformLocation(renderer->palm_program, "lighting_quality");
  renderer->palm_shadow_light_view_projection_location = glGetUniformLocation(renderer->palm_shadow_program, "light_vp");
  renderer->shadow_light_view_projection_location = glGetUniformLocation(renderer->shadow_program, "light_vp");
  renderer->shadow_origin_location = glGetUniformLocation(renderer->shadow_program, "terrain_origin");
  renderer->shadow_shape_location = glGetUniformLocation(renderer->shadow_program, "terrain_shape");
  renderer->shadow_contact_count_location = glGetUniformLocation(renderer->shadow_program, "terrain_contact_count");
  renderer->shadow_contact_data_location = glGetUniformLocation(renderer->shadow_program, "terrain_contact_data[0]");
  renderer->shadow_contact_params_location = glGetUniformLocation(renderer->shadow_program, "terrain_contact_params[0]");
  renderer->post_quality_location = glGetUniformLocation(renderer->post_program, "post_quality");

  glUseProgram(renderer->post_program);
  {
    const GLint texture_units[2] = { 0, 1 };
    const GLint texture_location = glGetUniformLocation(renderer->post_program, "tex");
    if (texture_location >= 0)
    {
      glUniform1iv(texture_location, 2, texture_units);
    }
  }
  glUseProgram(0);

  glUseProgram(renderer->terrain_program);
  if (renderer->terrain_shadow_map_location >= 0)
  {
    glUniform1i(renderer->terrain_shadow_map_location, 2);
  }
  glUseProgram(0);

  glUseProgram(renderer->palm_program);
  if (renderer->palm_shadow_map_location >= 0)
  {
    glUniform1i(renderer->palm_shadow_map_location, 2);
  }
  if (renderer->palm_diffuse_map_location >= 0)
  {
    glUniform1i(renderer->palm_diffuse_map_location, 3);
  }
  glUseProgram(0);

  if (!renderer_create_terrain(renderer) || !renderer_create_shadow_map(renderer))
  {
    renderer_destroy(renderer);
    return 0;
  }

  if (!palm_render_create(&renderer->palm_mesh) ||
    !mountain_render_create(&renderer->mountain_mesh) ||
    !tree_render_create(&renderer->tree_mesh) ||
    !grass_render_create(&renderer->grass_mesh))
  {
    renderer_destroy(renderer);
    return 0;
  }

  if (!console_overlay_create(&renderer->console_overlay))
  {
    renderer_destroy(renderer);
    return 0;
  }

  if (!stats_overlay_create(&renderer->stats_overlay))
  {
    renderer_destroy(renderer);
    return 0;
  }

  if (!renderer_resize(renderer, width, height))
  {
    renderer_destroy(renderer);
    return 0;
  }

  diagnostics_logf(
    "renderer_create: estimated_gpu_resources=%.1fMB framebuffer=%dx%d",
    renderer_estimate_gpu_memory_mb(renderer, renderer->framebuffer_width, renderer->framebuffer_height),
    renderer->framebuffer_width,
    renderer->framebuffer_height);

  return 1;
}

void renderer_destroy(Renderer* renderer)
{
  renderer_destroy_framebuffer(renderer);
  renderer_destroy_shadow_map(renderer);
  renderer_destroy_terrain(renderer);
  palm_render_destroy(&renderer->palm_mesh);
  mountain_render_destroy(&renderer->mountain_mesh);
  tree_render_destroy(&renderer->tree_mesh);
  grass_render_destroy(&renderer->grass_mesh);
  stats_overlay_destroy(&renderer->stats_overlay);
  console_overlay_destroy(&renderer->console_overlay);

  if (renderer->sky_program != 0U)
  {
    glDeleteProgram(renderer->sky_program);
    renderer->sky_program = 0U;
  }

  if (renderer->post_program != 0U)
  {
    glDeleteProgram(renderer->post_program);
    renderer->post_program = 0U;
  }

  if (renderer->terrain_program != 0U)
  {
    glDeleteProgram(renderer->terrain_program);
    renderer->terrain_program = 0U;
  }

  if (renderer->palm_program != 0U)
  {
    glDeleteProgram(renderer->palm_program);
    renderer->palm_program = 0U;
  }

  if (renderer->shadow_program != 0U)
  {
    glDeleteProgram(renderer->shadow_program);
    renderer->shadow_program = 0U;
  }

  if (renderer->palm_shadow_program != 0U)
  {
    glDeleteProgram(renderer->palm_shadow_program);
    renderer->palm_shadow_program = 0U;
  }

  if (renderer->sky_vao != 0U)
  {
    glDeleteVertexArrays(1, &renderer->sky_vao);
    renderer->sky_vao = 0U;
  }

  if (renderer->post_vao != 0U)
  {
    glDeleteVertexArrays(1, &renderer->post_vao);
    renderer->post_vao = 0U;
  }
}

int renderer_resize(Renderer* renderer, int width, int height)
{
  int framebuffer_width = 0;
  int framebuffer_height = 0;

  if (width <= 0 || height <= 0)
  {
    renderer->width = width;
    renderer->height = height;
    renderer->framebuffer_width = width;
    renderer->framebuffer_height = height;
    return 1;
  }

  framebuffer_width = (int)((float)width * renderer->quality.render_scale + 0.5f);
  framebuffer_height = (int)((float)height * renderer->quality.render_scale + 0.5f);
  if (framebuffer_width < 1)
  {
    framebuffer_width = 1;
  }
  if (framebuffer_height < 1)
  {
    framebuffer_height = 1;
  }

  if (renderer->framebuffer != 0U &&
    renderer->width == width &&
    renderer->height == height &&
    renderer->framebuffer_width == framebuffer_width &&
    renderer->framebuffer_height == framebuffer_height)
  {
    return 1;
  }

  renderer_destroy_framebuffer(renderer);
  if (!renderer_create_framebuffer(renderer, framebuffer_width, framebuffer_height))
  {
    return 0;
  }

  renderer->width = width;
  renderer->height = height;
  renderer->framebuffer_width = framebuffer_width;
  renderer->framebuffer_height = framebuffer_height;
  diagnostics_logf(
    "renderer_resize: estimated_gpu_resources=%.1fMB framebuffer=%dx%d window=%dx%d",
    renderer_estimate_gpu_memory_mb(renderer, framebuffer_width, framebuffer_height),
    framebuffer_width,
    framebuffer_height,
    width,
    height);
  return 1;
}

int renderer_set_quality_preset(Renderer* renderer, RendererQualityPreset preset)
{
  RendererQualityProfile next_profile;

  if (renderer == NULL)
  {
    return 0;
  }

  next_profile = render_quality_get_profile(preset, renderer->renderer_name, renderer->vendor_name);
  if (renderer->quality.preset == next_profile.preset)
  {
    return 1;
  }

  renderer_destroy_framebuffer(renderer);
  renderer_destroy_shadow_map(renderer);
  renderer_destroy_terrain(renderer);
  renderer->quality = next_profile;
  renderer->shadow_ready = 0;
  renderer->frame_index = 0U;

  if (!renderer_create_terrain(renderer) ||
    !renderer_create_shadow_map(renderer) ||
    !renderer_resize(renderer, renderer->width, renderer->height))
  {
    return 0;
  }

  renderer_log_quality_profile("renderer_set_quality_preset", renderer);
  return 1;
}

RendererQualityPreset renderer_get_quality_preset(const Renderer* renderer)
{
  if (renderer == NULL)
  {
    return RENDER_QUALITY_PRESET_HIGH;
  }

  return renderer->quality.preset;
}

void renderer_render(
  Renderer* renderer,
  const CameraState* camera,
  const AtmosphereState* atmosphere,
  const SceneSettings* settings,
  const OverlayState* overlay,
  const BlockWorld* block_world
)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  const Matrix projection = math_get_projection_matrix(renderer->width, renderer->height, active_settings->camera_fov_degrees);
  const Matrix view = math_get_view_matrix(camera->x, camera->y, camera->z, camera->yaw, camera->pitch);
  const ViewFrustum world_frustum = view_frustum_build(
    camera,
    renderer->width,
    renderer->height,
    active_settings->camera_fov_degrees);
  float terrain_origin_x = 0.0f;
  float terrain_origin_z = 0.0f;
  const GLfloat terrain_shape[4] = {
    active_settings->terrain_base_height,
    active_settings->terrain_height_scale,
    active_settings->terrain_roughness,
    active_settings->terrain_ridge_strength
  };
  const GLfloat environment_settings[4] = {
    active_settings->fog_density,
    0.0f,
    0.0f,
    0.0f
  };
  const GLfloat camera_position[3] = { camera->x, camera->y, camera->z };
  const Matrix light_view_projection = renderer_get_light_view_projection_matrix(renderer, camera, atmosphere, active_settings);
  const int shadow_update_interval = (renderer->quality.shadow_update_interval > 1) ? renderer->quality.shadow_update_interval : 1;
  const int refresh_shadows = (renderer->shadow_ready == 0) || ((renderer->frame_index % (unsigned int)shadow_update_interval) == 0U);
  TerrainContactPatch terrain_contact_patches[TERRAIN_CONTACT_PATCH_CAPACITY] = { 0 };
  float terrain_contact_distances[TERRAIN_CONTACT_PATCH_CAPACITY] = { 0.0f };
  int terrain_contact_count = 0;
  int terrain_contact_capacity = TERRAIN_CONTACT_PATCH_CAPACITY;

  if (renderer->quality.render_scale <= 0.62f)
  {
    terrain_contact_capacity = 6;
  }
  else if (renderer->quality.render_scale < 0.86f || renderer->quality.shadow_update_interval > 1)
  {
    terrain_contact_capacity = 10;
  }

  (void)palm_render_update(&renderer->palm_mesh, camera, active_settings, &renderer->quality);
  (void)mountain_render_update(&renderer->mountain_mesh, camera, active_settings, &renderer->quality, &world_frustum);
  (void)tree_render_update(&renderer->tree_mesh, camera, active_settings, &renderer->quality, &world_frustum);
  (void)grass_render_update(&renderer->grass_mesh, camera, active_settings, &renderer->quality, &world_frustum);
  terrain_contact_count = palm_render_collect_contact_patches(
    &renderer->palm_mesh,
    camera->x,
    camera->z,
    terrain_contact_patches,
    terrain_contact_distances,
    terrain_contact_count,
    terrain_contact_capacity);
  terrain_contact_count = palm_render_collect_contact_patches(
    &renderer->tree_mesh,
    camera->x,
    camera->z,
    terrain_contact_patches,
    terrain_contact_distances,
    terrain_contact_count,
    terrain_contact_capacity);
  terrain_set_contact_patches(terrain_contact_patches, terrain_contact_count);
  renderer_get_terrain_origin(renderer, camera, &terrain_origin_x, &terrain_origin_z);

  if (refresh_shadows)
  {
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->shadow_framebuffer);
    glViewport(0, 0, renderer->quality.shadow_map_size, renderer->quality.shadow_map_size);
    glClear(GL_DEPTH_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    glUseProgram(renderer->shadow_program);
    if (renderer->shadow_light_view_projection_location >= 0)
    {
      glUniformMatrix4fv(renderer->shadow_light_view_projection_location, 1, GL_FALSE, light_view_projection.m);
    }
    if (renderer->shadow_origin_location >= 0)
    {
      glUniform2f(renderer->shadow_origin_location, terrain_origin_x, terrain_origin_z);
    }
    if (renderer->shadow_shape_location >= 0)
    {
      glUniform4fv(renderer->shadow_shape_location, 1, terrain_shape);
    }
    renderer_upload_terrain_contact_uniforms(
      renderer->shadow_contact_count_location,
      renderer->shadow_contact_data_location,
      renderer->shadow_contact_params_location,
      terrain_contact_patches,
      terrain_contact_count);
    glBindVertexArray(renderer->shadow_terrain_vao != 0U ? renderer->shadow_terrain_vao : renderer->terrain_vao);
    glDrawElements(
      GL_TRIANGLES,
      (renderer->shadow_terrain_vao != 0U) ? renderer->shadow_terrain_index_count : renderer->terrain_index_count,
      GL_UNSIGNED_INT,
      NULL);
    glUseProgram(renderer->palm_shadow_program);
    if (renderer->palm_shadow_light_view_projection_location >= 0)
    {
      glUniformMatrix4fv(renderer->palm_shadow_light_view_projection_location, 1, GL_FALSE, light_view_projection.m);
    }
    glDisable(GL_CULL_FACE);
    palm_render_draw(&renderer->palm_mesh);
    tree_render_draw(&renderer->tree_mesh);
    if (renderer->quality.enable_grass_shadows != 0)
    {
      grass_render_draw(&renderer->grass_mesh);
    }
    glEnable(GL_CULL_FACE);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    renderer->shadow_ready = 1;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffer);
  glViewport(0, 0, renderer->framebuffer_width, renderer->framebuffer_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glDisable(GL_DEPTH_TEST);
  glUseProgram(renderer->sky_program);
  glUniformMatrix4fv(renderer->sky_projection_location, 1, GL_FALSE, projection.m);
  glUniformMatrix4fv(renderer->sky_view_location, 1, GL_FALSE, view.m);
  if (renderer->sky_cloud_time_location >= 0)
  {
    glUniform1f(renderer->sky_cloud_time_location, atmosphere->cloud_time_seconds);
  }
  if (renderer->sky_sun_direction_location >= 0)
  {
    glUniform3fv(renderer->sky_sun_direction_location, 1, atmosphere->sun_direction);
  }
  if (renderer->sky_sun_distance_location >= 0)
  {
    glUniform1f(renderer->sky_sun_distance_location, active_settings->sun_distance_mkm);
  }
  if (renderer->sky_cirrus_location >= 0)
  {
    glUniform1f(renderer->sky_cirrus_location, 0.18f + active_settings->cloud_amount * 0.26f);
  }
  if (renderer->sky_cumulus_location >= 0)
  {
    glUniform1f(
      renderer->sky_cumulus_location,
      (renderer->quality.enable_full_clouds ? 0.48f : 0.40f) + active_settings->cloud_amount * (renderer->quality.enable_full_clouds ? 0.34f : 0.22f)
    );
  }
  if (renderer->sky_cloud_settings_location >= 0)
  {
    const GLfloat cloud_settings[4] = {
      (active_settings->clouds_enabled != 0) ? 1.0f : 0.0f,
      active_settings->cloud_amount,
      active_settings->cloud_spacing,
      renderer->quality.enable_full_clouds ? 1.0f : 0.0f
    };
    glUniform4fv(renderer->sky_cloud_settings_location, 1, cloud_settings);
  }
  if (renderer->sky_quality_location >= 0)
  {
    glUniform1f(renderer->sky_quality_location, renderer->quality.enable_full_clouds ? 1.0f : 0.0f);
  }
  glBindVertexArray(renderer->sky_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glEnable(GL_DEPTH_TEST);
  glUseProgram(renderer->terrain_program);
  if (renderer->terrain_projection_location >= 0)
  {
    glUniformMatrix4fv(renderer->terrain_projection_location, 1, GL_FALSE, projection.m);
  }
  if (renderer->terrain_view_location >= 0)
  {
    glUniformMatrix4fv(renderer->terrain_view_location, 1, GL_FALSE, view.m);
  }
  if (renderer->terrain_light_view_projection_location >= 0)
  {
    glUniformMatrix4fv(renderer->terrain_light_view_projection_location, 1, GL_FALSE, light_view_projection.m);
  }
  if (renderer->terrain_origin_location >= 0)
  {
    glUniform2f(renderer->terrain_origin_location, terrain_origin_x, terrain_origin_z);
  }
  if (renderer->terrain_shape_location >= 0)
  {
    glUniform4fv(renderer->terrain_shape_location, 1, terrain_shape);
  }
  if (renderer->terrain_sun_direction_location >= 0)
  {
    glUniform3fv(renderer->terrain_sun_direction_location, 1, atmosphere->sun_direction);
  }
  if (renderer->terrain_sun_distance_location >= 0)
  {
    glUniform1f(renderer->terrain_sun_distance_location, active_settings->sun_distance_mkm);
  }
  if (renderer->terrain_camera_position_location >= 0)
  {
    glUniform3fv(renderer->terrain_camera_position_location, 1, camera_position);
  }
  if (renderer->terrain_lighting_quality_location >= 0)
  {
    const GLfloat lighting_quality[4] = {
      renderer->quality.enable_raytrace ? 1.0f : 0.0f,
      renderer->quality.enable_pathtrace ? 1.0f : 0.0f,
      renderer->quality.trace_distance_scale,
      0.0f
    };
    glUniform4fv(renderer->terrain_lighting_quality_location, 1, lighting_quality);
  }
  if (renderer->terrain_environment_location >= 0)
  {
    glUniform4fv(renderer->terrain_environment_location, 1, environment_settings);
  }
  renderer_upload_terrain_contact_uniforms(
    renderer->terrain_contact_count_location,
    renderer->terrain_contact_data_location,
    renderer->terrain_contact_params_location,
    terrain_contact_patches,
    terrain_contact_count);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, renderer->shadow_texture);
  glBindVertexArray(renderer->shadow_terrain_vao != 0U ? renderer->shadow_terrain_vao : renderer->terrain_vao);
  glDrawElements(
    GL_TRIANGLES,
    (renderer->shadow_terrain_vao != 0U) ? renderer->shadow_terrain_index_count : renderer->terrain_index_count,
    GL_UNSIGNED_INT,
    NULL);
  glUseProgram(renderer->palm_program);
  if (renderer->palm_projection_location >= 0)
  {
    glUniformMatrix4fv(renderer->palm_projection_location, 1, GL_FALSE, projection.m);
  }
  if (renderer->palm_view_location >= 0)
  {
    glUniformMatrix4fv(renderer->palm_view_location, 1, GL_FALSE, view.m);
  }
  if (renderer->palm_light_view_projection_location >= 0)
  {
    glUniformMatrix4fv(renderer->palm_light_view_projection_location, 1, GL_FALSE, light_view_projection.m);
  }
  if (renderer->palm_sun_direction_location >= 0)
  {
    glUniform3fv(renderer->palm_sun_direction_location, 1, atmosphere->sun_direction);
  }
  if (renderer->palm_sun_distance_location >= 0)
  {
    glUniform1f(renderer->palm_sun_distance_location, active_settings->sun_distance_mkm);
  }
  if (renderer->palm_camera_position_location >= 0)
  {
    glUniform3fv(renderer->palm_camera_position_location, 1, camera_position);
  }
  if (renderer->palm_terrain_shape_location >= 0)
  {
    glUniform4fv(renderer->palm_terrain_shape_location, 1, terrain_shape);
  }
  if (renderer->palm_environment_location >= 0)
  {
    glUniform4fv(renderer->palm_environment_location, 1, environment_settings);
  }
  if (renderer->palm_lighting_quality_location >= 0)
  {
    const GLfloat lighting_quality[4] = {
      renderer->quality.enable_raytrace ? 1.0f : 0.0f,
      renderer->quality.enable_pathtrace ? 1.0f : 0.0f,
      renderer->quality.trace_distance_scale,
      0.0f
    };
    glUniform4fv(renderer->palm_lighting_quality_location, 1, lighting_quality);
  }
  glDisable(GL_CULL_FACE);
  mountain_render_draw(&renderer->mountain_mesh);
  palm_render_draw(&renderer->palm_mesh);
  tree_render_draw(&renderer->tree_mesh);
  grass_render_draw(&renderer->grass_mesh);
  glEnable(GL_CULL_FACE);
  block_render_draw_world(renderer->framebuffer_width, renderer->framebuffer_height, camera, atmosphere, active_settings, block_world);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, renderer->width, renderer->height);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(renderer->post_program);
  if (renderer->post_quality_location >= 0)
  {
    const GLfloat post_quality[4] = {
      renderer->quality.enable_post_ao ? 1.0f : 0.0f,
      renderer->quality.render_scale,
      0.0f,
      0.0f
    };
    glUniform4fv(renderer->post_quality_location, 1, post_quality);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, renderer->color_texture);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, renderer->depth_texture);
  glBindVertexArray(renderer->post_vao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  console_overlay_render(&renderer->console_overlay, renderer->width, renderer->height, overlay);
  stats_overlay_render(&renderer->stats_overlay, renderer->width, renderer->height, overlay);

  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glUseProgram(0);
  renderer->frame_index += 1U;
}

float renderer_get_terrain_height(float x, float z, const SceneSettings* settings)
{
  return terrain_get_render_height(x, z, settings);
}

void renderer_sync_terrain_render_sampling(const Renderer* renderer, const CameraState* camera)
{
  TerrainRenderSamplingConfig config = { 0 };

  if (renderer == NULL || camera == NULL)
  {
    terrain_set_render_sampling(NULL);
    return;
  }

  renderer_get_terrain_origin(renderer, camera, &config.origin_x, &config.origin_z);
  config.mesh_step = renderer_get_terrain_step_for_resolution(
    (renderer->quality.terrain_resolution > 2) ? renderer->quality.terrain_resolution : 257);
  config.half_extent = k_renderer_terrain_half_extent;
  config.valid = 1;
  terrain_set_render_sampling(&config);
}

static void renderer_show_error(const char* title, const char* message)
{
  diagnostics_logf("%s: %s", title, message);
  platform_support_show_error_dialog(title, message);
}

static const char* renderer_find_last_path_separator(const char* path)
{
  const char* last_backslash = NULL;
  const char* last_slash = NULL;

  if (path == NULL)
  {
    return NULL;
  }

  last_backslash = strrchr(path, '\\');
  last_slash = strrchr(path, '/');
  if (last_backslash == NULL)
  {
    return last_slash;
  }
  if (last_slash == NULL)
  {
    return last_backslash;
  }

  return (last_backslash > last_slash) ? last_backslash : last_slash;
}

static int renderer_file_exists(const char* path)
{
  return platform_support_file_exists(path);
}

static int renderer_build_relative_path(const char* base_path, const char* relative_path, char* out_path, size_t out_path_size)
{
  const char* last_separator = NULL;
  size_t directory_length = 0U;

  if (base_path == NULL || relative_path == NULL || out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

  last_separator = renderer_find_last_path_separator(base_path);
  if (last_separator == NULL)
  {
    return 0;
  }

  directory_length = (size_t)(last_separator - base_path + 1);
  if (directory_length + strlen(relative_path) + 1U > out_path_size)
  {
    return 0;
  }

  memcpy(out_path, base_path, directory_length);
  (void)snprintf(out_path + directory_length, out_path_size - directory_length, "%s", relative_path);
  return 1;
}

static int renderer_build_shader_path(const char* relative_path, char* out_path, size_t out_path_size)
{
  char module_path[PLATFORM_PATH_MAX] = { 0 };
  char candidate_path[PLATFORM_PATH_MAX] = { 0 };
  char current_directory[PLATFORM_PATH_MAX] = { 0 };
  char* last_separator = NULL;
  size_t base_length = 0U;
  static const char* k_shader_prefix = "shaders/";
  static const char* k_shader_fallbacks[] = {
    "shaders/",
      "../shaders/",
      "../../shaders/",
      "../../../shaders/"
    };
  size_t i = 0U;

  if (!platform_support_get_executable_path(module_path, sizeof(module_path)))
  {
    renderer_show_error("Path Error", "Failed to resolve the executable directory for shader loading.");
    return 0;
  }

  last_separator = (char*)renderer_find_last_path_separator(module_path);
  if (last_separator == NULL)
  {
    renderer_show_error("Path Error", "Failed to resolve the executable directory separator for shader loading.");
    return 0;
  }

  last_separator[1] = '\0';
  if (strlen(module_path) + strlen(relative_path) + 1U <= sizeof(candidate_path))
  {
    (void)snprintf(candidate_path, sizeof(candidate_path), "%s%s", module_path, relative_path);
    if (renderer_file_exists(candidate_path))
    {
      (void)snprintf(out_path, out_path_size, "%s", candidate_path);
      return 1;
    }
  }

  if (platform_support_get_current_directory(current_directory, sizeof(current_directory)))
  {
    base_length = strlen(current_directory);
    if (base_length + 1U + strlen(relative_path) + 1U <= sizeof(candidate_path))
    {
      (void)snprintf(candidate_path, sizeof(candidate_path), "%s/%s", current_directory, relative_path);
      if (renderer_file_exists(candidate_path))
      {
        (void)snprintf(out_path, out_path_size, "%s", candidate_path);
        return 1;
      }
    }
  }

  if (strncmp(relative_path, k_shader_prefix, strlen(k_shader_prefix)) == 0)
  {
    const char* suffix = relative_path + strlen(k_shader_prefix);
      for (i = 0U; i < sizeof(k_shader_fallbacks) / sizeof(k_shader_fallbacks[0]); ++i)
      {
        char fallback_relative[PLATFORM_PATH_MAX] = { 0 };

      if (strlen(k_shader_fallbacks[i]) + strlen(suffix) + 1U > sizeof(fallback_relative))
      {
        continue;
      }

      (void)snprintf(fallback_relative, sizeof(fallback_relative), "%s%s", k_shader_fallbacks[i], suffix);
      if (renderer_build_relative_path(module_path, fallback_relative, candidate_path, sizeof(candidate_path)) &&
        renderer_file_exists(candidate_path))
      {
        (void)snprintf(out_path, out_path_size, "%s", candidate_path);
        return 1;
      }
    }
  }

  {
    char message[512] = { 0 };
    (void)snprintf(
      message,
      sizeof(message),
      "Failed to resolve shader path for '%s'. Check the shaders folder next to the executable or in the project root.",
      relative_path
    );
    renderer_show_error("Path Error", message);
  }
  return 0;
}

static int renderer_text_buffer_append(RendererTextBuffer* buffer, const char* text, size_t text_length)
{
  size_t required_capacity = 0U;
  char* resized = NULL;

  if (buffer == NULL || text == NULL)
  {
    return 0;
  }

  required_capacity = buffer->length + text_length + 1U;
  if (required_capacity > buffer->capacity)
  {
    size_t new_capacity = (buffer->capacity > 0U) ? buffer->capacity : 512U;
    while (new_capacity < required_capacity)
    {
      new_capacity *= 2U;
    }

    resized = (char*)realloc(buffer->data, new_capacity);
    if (resized == NULL)
    {
      renderer_show_error("Memory Error", "Failed to grow the shader source buffer.");
      return 0;
    }

    buffer->data = resized;
    buffer->capacity = new_capacity;
  }

  memcpy(buffer->data + buffer->length, text, text_length);
  buffer->length += text_length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int renderer_build_include_path(const char* source_path, const char* include_path, char* out_path, size_t out_path_size)
{
  const char* last_separator = NULL;
  size_t directory_length = 0U;

  if (source_path == NULL || include_path == NULL || out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

  if (strchr(include_path, ':') != NULL || include_path[0] == '/' || include_path[0] == '\\')
  {
    if (strlen(include_path) + 1U > out_path_size)
    {
      return 0;
    }
    (void)snprintf(out_path, out_path_size, "%s", include_path);
    return 1;
  }

  last_separator = renderer_find_last_path_separator(source_path);
  if (last_separator == NULL)
  {
    return 0;
  }

  directory_length = (size_t)(last_separator - source_path + 1);
  if (directory_length + strlen(include_path) + 1U > out_path_size)
  {
    return 0;
  }

  memcpy(out_path, source_path, directory_length);
  (void)snprintf(out_path + directory_length, out_path_size - directory_length, "%s", include_path);
  return 1;
}

static int renderer_append_text_file_recursive(const char* path, const char* label, int depth, RendererTextBuffer* out_buffer)
{
  char* raw_source = NULL;
  const char* cursor = NULL;

  if (depth > 8)
  {
    renderer_show_error("Shader Error", "Shader include nesting exceeded the supported recursion depth.");
    return 0;
  }

  if (!renderer_load_raw_text_file(path, label, &raw_source))
  {
    return 0;
  }

  cursor = raw_source;
  while (*cursor != '\0')
  {
    const char* line_start = cursor;
    const char* line_end = strchr(cursor, '\n');
    const char* trimmed = NULL;
    size_t line_length = 0U;

    if (line_end == NULL)
    {
      line_end = cursor + strlen(cursor);
    }
    else
    {
      line_end += 1;
    }

    line_length = (size_t)(line_end - line_start);
    trimmed = line_start;
    while (trimmed < line_end && (*trimmed == ' ' || *trimmed == '\t'))
    {
      ++trimmed;
    }

    if ((size_t)(line_end - trimmed) >= 11U && strncmp(trimmed, "#include \"", 10U) == 0)
    {
      const char* include_begin = trimmed + 10;
      const char* include_end = strchr(include_begin, '"');
      if (include_end != NULL && include_end < line_end)
      {
        char include_name[PLATFORM_PATH_MAX] = { 0 };
        char include_path[PLATFORM_PATH_MAX] = { 0 };
        size_t include_length = (size_t)(include_end - include_begin);

        if (include_length == 0U || include_length >= sizeof(include_name))
        {
          free(raw_source);
          renderer_show_error("Shader Error", "Shader include path is invalid or too long.");
          return 0;
        }

        memcpy(include_name, include_begin, include_length);
        include_name[include_length] = '\0';

        if (!renderer_build_include_path(path, include_name, include_path, sizeof(include_path)) ||
          !renderer_append_text_file_recursive(include_path, label, depth + 1, out_buffer) ||
          !renderer_text_buffer_append(out_buffer, "\n", 1U))
        {
          free(raw_source);
          return 0;
        }

        cursor = line_end;
        continue;
      }
    }

    if (!renderer_text_buffer_append(out_buffer, line_start, line_length))
    {
      free(raw_source);
      return 0;
    }

    cursor = line_end;
  }

  free(raw_source);
  return 1;
}

static int renderer_load_raw_text_file(const char* path, const char* label, char** out_source)
{
  char message[256] = { 0 };
  FILE* file = NULL;
  long file_size = 0L;
  size_t bytes_read = 0U;
  char* source = NULL;

  #if defined(_MSC_VER)
  if (fopen_s(&file, path, "rb") != 0)
  {
    file = NULL;
  }
  #else
  file = fopen(path, "rb");
  #endif

  if (file == NULL)
  {
    (void)snprintf(message, sizeof(message), "Failed to open shader file: %s\n%s", label, path);
    renderer_show_error("File Error", message);
    return 0;
  }

  if (fseek(file, 0L, SEEK_END) != 0)
  {
    (void)snprintf(message, sizeof(message), "Failed to seek shader file: %s.", label);
    renderer_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  file_size = ftell(file);
  if (file_size <= 0L || fseek(file, 0L, SEEK_SET) != 0)
  {
    (void)snprintf(message, sizeof(message), "Shader file is empty or unreadable: %s.", label);
    renderer_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  source = (char*)malloc((size_t)file_size + 1U);
  if (source == NULL)
  {
    renderer_show_error("Memory Error", "Failed to allocate memory for the shader source.");
    fclose(file);
    return 0;
  }

  bytes_read = fread(source, 1U, (size_t)file_size, file);
  fclose(file);
  if (bytes_read != (size_t)file_size)
  {
    free(source);
    (void)snprintf(message, sizeof(message), "Failed to read shader file: %s.", label);
    renderer_show_error("File Error", message);
    return 0;
  }

  source[file_size] = '\0';
  *out_source = source;
  return 1;
}

static int renderer_load_text_file(const char* path, const char* label, char** out_source)
{
  RendererTextBuffer buffer = { 0 };

  if (!renderer_append_text_file_recursive(path, label, 0, &buffer))
  {
    free(buffer.data);
    return 0;
  }

  if (buffer.data == NULL)
  {
    buffer.data = (char*)malloc(1U);
    if (buffer.data == NULL)
    {
      renderer_show_error("Memory Error", "Failed to allocate memory for the empty shader source.");
      return 0;
    }
    buffer.data[0] = '\0';
  }

  *out_source = buffer.data;
  return 1;
}

static int renderer_check_shader(GLuint shader, const char* label)
{
  GLint status_code = GL_FALSE;
  GLint log_length = 0;
  GLsizei written_length = 0;
  char* log = NULL;
  char title[128] = { 0 };

  glGetShaderiv(shader, GL_COMPILE_STATUS, &status_code);
  if (status_code == GL_TRUE)
  {
    return 1;
  }

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
  if (log_length < 1)
  {
    log_length = 1;
  }

  log = (char*)malloc((size_t)log_length + 1U);
  if (log == NULL)
  {
    renderer_show_error("Shader Error", "Shader compilation failed and the error log could not be allocated.");
    return 0;
  }

  glGetShaderInfoLog(shader, log_length, &written_length, log);
  log[written_length] = '\0';
  (void)snprintf(title, sizeof(title), "Shader Compile Failed: %s", label);
  renderer_show_error(title, log);
  free(log);
  return 0;
}

static int renderer_check_program(GLuint program, const char* label)
{
  GLint status_code = GL_FALSE;
  GLint log_length = 0;
  GLsizei written_length = 0;
  char* log = NULL;
  char title[128] = { 0 };

  glGetProgramiv(program, GL_LINK_STATUS, &status_code);
  if (status_code == GL_TRUE)
  {
    return 1;
  }

  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
  if (log_length < 1)
  {
    log_length = 1;
  }

  log = (char*)malloc((size_t)log_length + 1U);
  if (log == NULL)
  {
    renderer_show_error("Program Error", "Program linking failed and the error log could not be allocated.");
    return 0;
  }

  glGetProgramInfoLog(program, log_length, &written_length, log);
  log[written_length] = '\0';
  (void)snprintf(title, sizeof(title), "Program Link Failed: %s", label);
  renderer_show_error(title, log);
  free(log);
  return 0;
}

static GLuint renderer_compile_shader(GLenum shader_type, const char* source, const char* label)
{
  GLuint shader = glCreateShader(shader_type);
  if (shader == 0U)
  {
    renderer_show_error("OpenGL Error", "glCreateShader returned 0.");
    return 0U;
  }

  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  if (!renderer_check_shader(shader, label))
  {
    glDeleteShader(shader);
    return 0U;
  }

  return shader;
}

static GLuint renderer_create_program_from_files(const char* vertex_path, const char* fragment_path, const char* label)
{
  char resolved_vertex_path[PLATFORM_PATH_MAX] = { 0 };
  char resolved_fragment_path[PLATFORM_PATH_MAX] = { 0 };
  char* vertex_source = NULL;
  char* fragment_source = NULL;
  GLuint vertex_shader = 0U;
  GLuint fragment_shader = 0U;
  GLuint program = 0U;
  char vertex_label[128] = { 0 };
  char fragment_label[128] = { 0 };

  (void)snprintf(vertex_label, sizeof(vertex_label), "%s Vertex", label);
  (void)snprintf(fragment_label, sizeof(fragment_label), "%s Fragment", label);

  if (!renderer_build_shader_path(vertex_path, resolved_vertex_path, sizeof(resolved_vertex_path)) ||
    !renderer_build_shader_path(fragment_path, resolved_fragment_path, sizeof(resolved_fragment_path)) ||
    !renderer_load_text_file(resolved_vertex_path, vertex_label, &vertex_source) ||
    !renderer_load_text_file(resolved_fragment_path, fragment_label, &fragment_source))
  {
    free(vertex_source);
    free(fragment_source);
    return 0U;
  }

  vertex_shader = renderer_compile_shader(GL_VERTEX_SHADER, vertex_source, vertex_label);
  fragment_shader = renderer_compile_shader(GL_FRAGMENT_SHADER, fragment_source, fragment_label);
  free(vertex_source);
  free(fragment_source);

  if (vertex_shader == 0U || fragment_shader == 0U)
  {
    if (vertex_shader != 0U)
    {
      glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0U)
    {
      glDeleteShader(fragment_shader);
    }
    return 0U;
  }

  program = glCreateProgram();
  if (program == 0U)
  {
    renderer_show_error("OpenGL Error", "glCreateProgram returned 0.");
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return 0U;
  }

  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDetachShader(program, vertex_shader);
  glDetachShader(program, fragment_shader);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  if (!renderer_check_program(program, label))
  {
    glDeleteProgram(program);
    return 0U;
  }

  return program;
}

static void renderer_destroy_framebuffer(Renderer* renderer)
{
  if (renderer->framebuffer != 0U)
  {
    glDeleteFramebuffers(1, &renderer->framebuffer);
    renderer->framebuffer = 0U;
  }

  if (renderer->color_texture != 0U)
  {
    glDeleteTextures(1, &renderer->color_texture);
    renderer->color_texture = 0U;
  }

  if (renderer->depth_texture != 0U)
  {
    glDeleteTextures(1, &renderer->depth_texture);
    renderer->depth_texture = 0U;
  }
}

static int renderer_create_framebuffer(Renderer* renderer, int width, int height)
{
  const GLenum draw_buffers[] = { GL_COLOR_ATTACHMENT0 };

  glGenTextures(1, &renderer->color_texture);
  glBindTexture(GL_TEXTURE_2D, renderer->color_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &renderer->depth_texture);
  glBindTexture(GL_TEXTURE_2D, renderer->depth_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &renderer->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, renderer->framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer->color_texture, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderer->depth_texture, 0);
  glDrawBuffers(1, draw_buffers);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
  {
    renderer_show_error("OpenGL Error", "Failed to create the framebuffer used for the post-processing pass.");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer_destroy_framebuffer(renderer);
    return 0;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return 1;
}

static void renderer_destroy_shadow_map(Renderer* renderer)
{
  if (renderer->shadow_framebuffer != 0U)
  {
    glDeleteFramebuffers(1, &renderer->shadow_framebuffer);
    renderer->shadow_framebuffer = 0U;
  }

  if (renderer->shadow_texture != 0U)
  {
    glDeleteTextures(1, &renderer->shadow_texture);
    renderer->shadow_texture = 0U;
  }
}

static int renderer_create_shadow_map(Renderer* renderer)
{
  const int shadow_map_size = (renderer->quality.shadow_map_size > 0) ? renderer->quality.shadow_map_size : 1024;

  glGenTextures(1, &renderer->shadow_texture);
  glBindTexture(GL_TEXTURE_2D, renderer->shadow_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, shadow_map_size, shadow_map_size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &renderer->shadow_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, renderer->shadow_framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, renderer->shadow_texture, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
  {
    renderer_show_error("OpenGL Error", "Failed to create the framebuffer used for the terrain shadow map.");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    renderer_destroy_shadow_map(renderer);
    return 0;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return 1;
}

static void renderer_destroy_terrain(Renderer* renderer)
{
  if (renderer->terrain_index_buffer != 0U)
  {
    glDeleteBuffers(1, &renderer->terrain_index_buffer);
    renderer->terrain_index_buffer = 0U;
  }

  if (renderer->terrain_vertex_buffer != 0U)
  {
    glDeleteBuffers(1, &renderer->terrain_vertex_buffer);
    renderer->terrain_vertex_buffer = 0U;
  }

  if (renderer->terrain_vao != 0U)
  {
    glDeleteVertexArrays(1, &renderer->terrain_vao);
    renderer->terrain_vao = 0U;
  }

  if (renderer->shadow_terrain_index_buffer != 0U)
  {
    glDeleteBuffers(1, &renderer->shadow_terrain_index_buffer);
    renderer->shadow_terrain_index_buffer = 0U;
  }

  if (renderer->shadow_terrain_vertex_buffer != 0U)
  {
    glDeleteBuffers(1, &renderer->shadow_terrain_vertex_buffer);
    renderer->shadow_terrain_vertex_buffer = 0U;
  }

  if (renderer->shadow_terrain_vao != 0U)
  {
    glDeleteVertexArrays(1, &renderer->shadow_terrain_vao);
    renderer->shadow_terrain_vao = 0U;
  }

  renderer->terrain_index_count = 0;
  renderer->shadow_terrain_index_count = 0;
}

static int renderer_create_heightfield_mesh(
  GLuint* out_vao,
  GLuint* out_vertex_buffer,
  GLuint* out_index_buffer,
  GLsizei* out_index_count,
  int resolution)
{
  const int mesh_resolution = (resolution > 2) ? resolution : 257;
  const int vertex_count = mesh_resolution * mesh_resolution;
  const int quad_count = (mesh_resolution - 1) * (mesh_resolution - 1);
  const float terrain_step = renderer_get_terrain_step_for_resolution(mesh_resolution);
  TerrainVertex* vertices = NULL;
  unsigned int* indices = NULL;
  int z = 0;
  int x = 0;
  int index_cursor = 0;

  if (out_vao == NULL || out_vertex_buffer == NULL || out_index_buffer == NULL || out_index_count == NULL)
  {
    return 0;
  }

  vertices = (TerrainVertex*)malloc(sizeof(TerrainVertex) * (size_t)vertex_count);
  indices = (unsigned int*)malloc(sizeof(unsigned int) * (size_t)quad_count * 6U);
  if (vertices == NULL || indices == NULL)
  {
    free(vertices);
    free(indices);
    renderer_show_error("Memory Error", "Failed to allocate the terrain mesh.");
    return 0;
  }

  for (z = 0; z < mesh_resolution; ++z)
  {
    for (x = 0; x < mesh_resolution; ++x)
    {
      const int vertex_index = z * mesh_resolution + x;
      const float local_x = -k_renderer_terrain_half_extent + terrain_step * (float)x;
      const float local_z = -k_renderer_terrain_half_extent + terrain_step * (float)z;

      vertices[vertex_index].position[0] = local_x;
      vertices[vertex_index].position[1] = local_z;
    }
  }

  for (z = 0; z < mesh_resolution - 1; ++z)
  {
    for (x = 0; x < mesh_resolution - 1; ++x)
    {
      const unsigned int top_left = (unsigned int)(z * mesh_resolution + x);
      const unsigned int top_right = top_left + 1U;
      const unsigned int bottom_left = top_left + (unsigned int)mesh_resolution;
      const unsigned int bottom_right = bottom_left + 1U;

      indices[index_cursor++] = top_left;
      indices[index_cursor++] = bottom_left;
      indices[index_cursor++] = top_right;
      indices[index_cursor++] = top_right;
      indices[index_cursor++] = bottom_left;
      indices[index_cursor++] = bottom_right;
    }
  }

  glGenVertexArrays(1, out_vao);
  glGenBuffers(1, out_vertex_buffer);
  glGenBuffers(1, out_index_buffer);

  glBindVertexArray(*out_vao);
  glBindBuffer(GL_ARRAY_BUFFER, *out_vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(TerrainVertex) * (size_t)vertex_count, vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *out_index_buffer);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * (size_t)index_cursor, indices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TerrainVertex), (const void*)offsetof(TerrainVertex, position));
  glBindVertexArray(0);

  *out_index_count = (GLsizei)index_cursor;

  free(vertices);
  free(indices);
  return 1;
}

static int renderer_create_terrain(Renderer* renderer)
{
  const int terrain_resolution = (renderer->quality.terrain_resolution > 2) ? renderer->quality.terrain_resolution : 257;
  const int shadow_terrain_resolution =
    (renderer->quality.shadow_terrain_resolution > terrain_resolution)
      ? renderer->quality.shadow_terrain_resolution
      : terrain_resolution;

  if (!renderer_create_heightfield_mesh(
    &renderer->terrain_vao,
    &renderer->terrain_vertex_buffer,
    &renderer->terrain_index_buffer,
    &renderer->terrain_index_count,
    terrain_resolution))
  {
    return 0;
  }

  if (shadow_terrain_resolution > terrain_resolution)
  {
    return renderer_create_heightfield_mesh(
      &renderer->shadow_terrain_vao,
      &renderer->shadow_terrain_vertex_buffer,
      &renderer->shadow_terrain_index_buffer,
      &renderer->shadow_terrain_index_count,
      shadow_terrain_resolution);
  }

  renderer->shadow_terrain_vao = 0U;
  renderer->shadow_terrain_vertex_buffer = 0U;
  renderer->shadow_terrain_index_buffer = 0U;
  renderer->shadow_terrain_index_count = 0;
  return 1;
}

static float renderer_get_terrain_step_for_resolution(int resolution)
{
  const int safe_resolution = (resolution > 2) ? resolution : 257;
  return (k_renderer_terrain_half_extent * 2.0f) / (float)(safe_resolution - 1);
}

static double renderer_estimate_gpu_memory_mb(const Renderer* renderer, int framebuffer_width, int framebuffer_height)
{
  const int terrain_resolution = (renderer != NULL && renderer->quality.terrain_resolution > 2) ? renderer->quality.terrain_resolution : 257;
  const int shadow_terrain_resolution =
    (renderer != NULL && renderer->quality.shadow_terrain_resolution > terrain_resolution)
      ? renderer->quality.shadow_terrain_resolution
      : terrain_resolution;
  const int shadow_map_size = (renderer != NULL && renderer->quality.shadow_map_size > 0) ? renderer->quality.shadow_map_size : 1024;
  const double color_bytes = (double)framebuffer_width * (double)framebuffer_height * 4.0;
  const double depth_bytes = (double)framebuffer_width * (double)framebuffer_height * 4.0;
  const double shadow_bytes = (double)shadow_map_size * (double)shadow_map_size * 2.0;
  const double terrain_vertex_bytes = (double)terrain_resolution * (double)terrain_resolution * (double)sizeof(TerrainVertex);
  const double terrain_index_bytes =
    (double)(terrain_resolution - 1) * (double)(terrain_resolution - 1) * 6.0 * (double)sizeof(unsigned int);
  double total_bytes = color_bytes + depth_bytes + shadow_bytes + terrain_vertex_bytes + terrain_index_bytes;

  if (shadow_terrain_resolution > terrain_resolution)
  {
    total_bytes +=
      (double)shadow_terrain_resolution * (double)shadow_terrain_resolution * (double)sizeof(TerrainVertex) +
      (double)(shadow_terrain_resolution - 1) * (double)(shadow_terrain_resolution - 1) * 6.0 * (double)sizeof(unsigned int);
  }

  return total_bytes / (1024.0 * 1024.0);
}

static float renderer_get_terrain_step(const Renderer* renderer)
{
  const int terrain_resolution = (renderer != NULL && renderer->quality.terrain_resolution > 2) ? renderer->quality.terrain_resolution : 257;
  const int shadow_terrain_resolution =
    (renderer != NULL && renderer->quality.shadow_terrain_resolution > 2)
      ? renderer->quality.shadow_terrain_resolution
      : terrain_resolution;
  const int active_resolution = (shadow_terrain_resolution > terrain_resolution) ? shadow_terrain_resolution : terrain_resolution;

  return renderer_get_terrain_step_for_resolution(active_resolution);
}

static void renderer_get_terrain_origin(const Renderer* renderer, const CameraState* camera, float* out_x, float* out_z)
{
  const float terrain_step = renderer_get_terrain_step(renderer);
  *out_x = floorf(camera->x / terrain_step) * terrain_step;
  *out_z = floorf(camera->z / terrain_step) * terrain_step;
}

static void renderer_transform_point(const Matrix* matrix, float x, float y, float z, float w, float* out_x, float* out_y, float* out_z, float* out_w)
{
  if (out_x != NULL)
  {
    *out_x = matrix->m[0] * x + matrix->m[4] * y + matrix->m[8] * z + matrix->m[12] * w;
  }
  if (out_y != NULL)
  {
    *out_y = matrix->m[1] * x + matrix->m[5] * y + matrix->m[9] * z + matrix->m[13] * w;
  }
  if (out_z != NULL)
  {
    *out_z = matrix->m[2] * x + matrix->m[6] * y + matrix->m[10] * z + matrix->m[14] * w;
  }
  if (out_w != NULL)
  {
    *out_w = matrix->m[3] * x + matrix->m[7] * y + matrix->m[11] * z + matrix->m[15] * w;
  }
}

static Matrix renderer_get_stabilized_shadow_matrix(const Renderer* renderer, const Matrix* light_view, const Matrix* light_projection)
{
  Matrix stabilized_projection = *light_projection;
  Matrix shadow_matrix = math_matrix_multiply(&stabilized_projection, light_view);
  float origin_x = 0.0f;
  float origin_y = 0.0f;
  float origin_w = 1.0f;
  const float texel_scale =
    (float)(((renderer != NULL && renderer->quality.shadow_map_size > 0) ? renderer->quality.shadow_map_size : 1024)) * 0.5f;

  renderer_transform_point(&shadow_matrix, 0.0f, 0.0f, 0.0f, 1.0f, &origin_x, &origin_y, NULL, &origin_w);
  if (fabsf(origin_w) > 0.0001f)
  {
    origin_x /= origin_w;
    origin_y /= origin_w;
  }

  stabilized_projection.m[12] += (roundf(origin_x * texel_scale) - origin_x * texel_scale) / texel_scale;
  stabilized_projection.m[13] += (roundf(origin_y * texel_scale) - origin_y * texel_scale) / texel_scale;
  return math_matrix_multiply(&stabilized_projection, light_view);
}

static Matrix renderer_get_light_view_projection_matrix(const Renderer* renderer, const CameraState* camera, const AtmosphereState* atmosphere, const SceneSettings* settings)
{
  float scene_center_x = 0.0f;
  float scene_center_z = 0.0f;
  float scene_center_y = 0.0f;
  const float shadow_extent =
    (renderer != NULL && renderer->quality.shadow_extent > 40.0f) ? renderer->quality.shadow_extent : 180.0f;
  const float light_distance = shadow_extent + 64.0f;
  const float sun_x = atmosphere->sun_direction[0];
  const float sun_y = atmosphere->sun_direction[1];
  const float sun_z = atmosphere->sun_direction[2];
  const float up_x = (fabsf(sun_y) > 0.92f) ? 0.0f : 0.0f;
  const float up_y = (fabsf(sun_y) > 0.92f) ? 0.0f : 1.0f;
  const float up_z = (fabsf(sun_y) > 0.92f) ? 1.0f : 0.0f;

  renderer_get_terrain_origin(renderer, camera, &scene_center_x, &scene_center_z);
  scene_center_y = renderer_get_terrain_height(scene_center_x, scene_center_z, settings) + 10.0f;

  const Matrix light_view = math_get_look_at_matrix(
    scene_center_x + sun_x * light_distance,
    scene_center_y + sun_y * light_distance,
    scene_center_z + sun_z * light_distance,
    scene_center_x,
    scene_center_y,
    scene_center_z,
    up_x,
    up_y,
    up_z
  );
  const Matrix light_projection = math_get_orthographic_matrix(
    -shadow_extent,
    shadow_extent,
    -shadow_extent,
    shadow_extent,
    1.0f,
    light_distance * 2.4f
  );

  return renderer_get_stabilized_shadow_matrix(renderer, &light_view, &light_projection);
}

static void renderer_upload_terrain_contact_uniforms(GLint count_location, GLint data_location, GLint params_location, const TerrainContactPatch* patches, int patch_count)
{
  GLfloat contact_data[TERRAIN_CONTACT_PATCH_CAPACITY * 4] = { 0.0f };
  GLfloat contact_params[TERRAIN_CONTACT_PATCH_CAPACITY * 4] = { 0.0f };
  int patch_index = 0;

  if (patch_count < 0)
  {
    patch_count = 0;
  }
  if (patch_count > TERRAIN_CONTACT_PATCH_CAPACITY)
  {
    patch_count = TERRAIN_CONTACT_PATCH_CAPACITY;
  }

  if (count_location >= 0)
  {
    glUniform1i(count_location, patch_count);
  }

  if (patches == NULL || patch_count <= 0)
  {
    return;
  }

  for (patch_index = 0; patch_index < patch_count; ++patch_index)
  {
    contact_data[patch_index * 4 + 0] = patches[patch_index].x;
    contact_data[patch_index * 4 + 1] = patches[patch_index].z;
    contact_data[patch_index * 4 + 2] = patches[patch_index].target_y;
    contact_data[patch_index * 4 + 3] = patches[patch_index].inner_radius;
    contact_params[patch_index * 4 + 0] = patches[patch_index].outer_radius;
    contact_params[patch_index * 4 + 1] = patches[patch_index].strength;
  }

  if (data_location >= 0)
  {
    glUniform4fv(data_location, patch_count, contact_data);
  }
  if (params_location >= 0)
  {
    glUniform4fv(params_location, patch_count, contact_params);
  }
}

static void renderer_log_quality_profile(const char* context, const Renderer* renderer)
{
  if (renderer == NULL)
  {
    return;
  }

  diagnostics_logf(
    "%s: preset=%s quality=%s render_scale=%.2f shadow=%d shadow_extent=%.1f terrain=%d shadow_terrain=%d raytrace=%d pathtrace=%d post_ao=%d clouds=%d shadow_interval=%d grass_shadow=%d tree_density=%.2f grass_density=%.2f gpu=%s vendor=%s",
    (context != NULL) ? context : "renderer",
    render_quality_preset_get_label(renderer->quality.preset),
    (renderer->quality.name != NULL) ? renderer->quality.name : "unknown",
    renderer->quality.render_scale,
    renderer->quality.shadow_map_size,
    renderer->quality.shadow_extent,
    renderer->quality.terrain_resolution,
    renderer->quality.shadow_terrain_resolution,
    renderer->quality.enable_raytrace,
    renderer->quality.enable_pathtrace,
    renderer->quality.enable_post_ao,
    renderer->quality.enable_full_clouds,
    renderer->quality.shadow_update_interval,
    renderer->quality.enable_grass_shadows,
    renderer->quality.tree_density_scale,
    renderer->quality.grass_density_scale,
    (renderer->renderer_name[0] != '\0') ? renderer->renderer_name : "unknown",
    (renderer->vendor_name[0] != '\0') ? renderer->vendor_name : "unknown");
}
