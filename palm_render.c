#include "palm_render.h"

#include "diagnostics.h"
#include "platform_support.h"
#include "procedural_lod.h"
#include "terrain.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define palm_render_sscanf sscanf_s
#else
#define palm_render_sscanf sscanf
#endif

typedef struct PalmVertex
{
  float position[3];
  float normal[3];
  float color[3];
  float texcoord[2];
} PalmVertex;

typedef struct PalmColor
{
  float r;
  float g;
  float b;
} PalmColor;

typedef struct PalmVec2
{
  float x;
  float y;
} PalmVec2;

typedef struct PalmVec3
{
  float x;
  float y;
  float z;
} PalmVec3;

typedef struct PalmInstanceData
{
  float transform[16];
  float tint[4];
} PalmInstanceData;

typedef struct PalmObjIndex
{
  int position_index;
  int normal_index;
  int texcoord_index;
} PalmObjIndex;

typedef struct PalmMaterial
{
  char name[64];
  PalmColor diffuse;
  int has_diffuse;
  char texture_path[PLATFORM_PATH_MAX];
  int has_texture;
} PalmMaterial;

typedef struct PalmRenderAssetSpec
{
  const char* relative_obj_path;
  int category;
  float desired_height_min;
  float desired_height_max;
  float scale_jitter_min;
  float scale_jitter_max;
  float embed_depth_min;
  float embed_depth_max;
  float slope_limit;
} PalmRenderAssetSpec;

typedef struct PalmVec3Array
{
  PalmVec3* data;
  size_t count;
  size_t capacity;
} PalmVec3Array;

typedef struct PalmVec2Array
{
  PalmVec2* data;
  size_t count;
  size_t capacity;
} PalmVec2Array;

typedef struct PalmVertexArray
{
  PalmVertex* data;
  size_t count;
  size_t capacity;
} PalmVertexArray;

typedef struct PalmMaterialArray
{
  PalmMaterial* data;
  size_t count;
  size_t capacity;
} PalmMaterialArray;

enum
{
  PALM_RENDER_MAX_FACE_VERTICES = 32
};

static const float k_palm_render_pi = 3.14159265f;
static const PalmRenderAssetSpec k_palm_render_asset_specs[PALM_RENDER_MAX_VARIANTS] = {
  { "res/obj/kelapasawit.obj", PALM_RENDER_CATEGORY_PALM, 8.2f, 12.4f, 0.82f, 1.22f, 0.22f, 0.58f, 1.45f },
  { "res/obj/Date Palm.obj", PALM_RENDER_CATEGORY_PALM, 8.6f, 12.8f, 0.82f, 1.18f, 0.20f, 0.54f, 1.40f },
  { "res/obj/Tree.obj", PALM_RENDER_CATEGORY_TREE, 9.2f, 14.6f, 0.80f, 1.16f, 0.24f, 0.60f, 1.32f },
  { "res/obj/Trava Kolosok.obj", PALM_RENDER_CATEGORY_GRASS, 0.62f, 1.28f, 0.82f, 1.24f, 0.02f, 0.08f, 1.08f }
};

static void palm_render_show_error(const char* title, const char* message);
static int palm_render_is_space(char value);
static const char* palm_render_find_last_path_separator(const char* path);
static const char* palm_render_get_basename(const char* path);
static int palm_render_file_exists(const char* path);
static int palm_render_build_relative_path(const char* base_path, const char* relative_path, char* out_path, size_t out_path_size);
static int palm_render_resolve_asset_path(const char* relative_path, char* out_path, size_t out_path_size);
static int palm_render_resolve_material_asset_path(const char* mtl_path, const char* relative_path, char* out_path, size_t out_path_size);
static int palm_render_load_text_file(const char* path, const char* label, char** out_text);
static char* palm_render_trim_left(char* text);
static void palm_render_trim_right_in_place(char* text);
static const char* palm_render_skip_spaces_const(const char* text);
static const char* palm_render_match_keyword(const char* text, const char* keyword);
static int palm_render_reserve_memory(void** buffer, size_t* capacity, size_t required, size_t element_size);
static int palm_render_push_vec3(PalmVec3Array* array, PalmVec3 value);
static int palm_render_push_vec2(PalmVec2Array* array, PalmVec2 value);
static int palm_render_push_vertex(PalmVertexArray* array, PalmVertex value);
static int palm_render_path_uses_black_key(const char* path);
static int palm_render_estimate_texture_color(const char* texture_path, PalmColor* out_color);
static int palm_render_create_texture_from_file(const char* texture_path, GLuint* out_texture);
static int palm_render_create_solid_texture(PalmColor color, GLuint* out_texture);
static int palm_render_material_needs_texture_color(const PalmMaterial* material);
static PalmMaterial* palm_render_find_material_mutable(PalmMaterialArray* materials, const char* name);
static PalmMaterial* palm_render_push_material(PalmMaterialArray* array, const char* name);
static PalmMaterial* palm_render_get_or_create_material(PalmMaterialArray* materials, const char* name);
static int palm_render_parse_mtl(const char* mtl_path, PalmMaterialArray* materials);
static const PalmMaterial* palm_render_find_material(const PalmMaterialArray* materials, const char* name);
static int palm_render_parse_face_vertex(const char* token, PalmObjIndex* out_index);
static int palm_render_resolve_obj_index(int obj_index, size_t count);
static PalmVec3 palm_render_vec3_subtract(PalmVec3 a, PalmVec3 b);
static PalmVec3 palm_render_vec3_cross(PalmVec3 a, PalmVec3 b);
static float palm_render_vec3_dot(PalmVec3 a, PalmVec3 b);
static PalmVec3 palm_render_vec3_normalize(PalmVec3 value);
static int palm_render_load_model_vertices(
  const char* relative_obj_path,
  PalmVertex** out_vertices,
  GLsizei* out_vertex_count,
  float* out_model_height,
  char* out_diffuse_texture_path,
  size_t out_diffuse_texture_path_size);
static int palm_render_create_variant(PalmRenderVariant* variant, const PalmRenderAssetSpec* asset_spec);
static void palm_render_destroy_variant(PalmRenderVariant* variant);
static int palm_render_reserve_instances(PalmRenderVariant* variant, size_t required_instance_capacity);
static float palm_render_clamp(float value, float min_value, float max_value);
static float palm_render_mix(float a, float b, float t);
static float palm_render_hash_unit(int x, int z, unsigned int seed);
static float palm_render_estimate_slope(float x, float z, const SceneSettings* settings);
static int palm_render_has_category(const PalmRenderMesh* mesh, int category);
static GLsizei palm_render_get_max_vertex_count_for_category(const PalmRenderMesh* mesh, int category);
static PalmRenderVariant* palm_render_pick_variant(PalmRenderMesh* mesh, int category, int grid_x, int grid_z, unsigned int seed);
static void palm_render_reset_instances(PalmRenderMesh* mesh);
static int palm_render_upload_instances(PalmRenderMesh* mesh);
static int palm_render_float_nearly_equal(float a, float b, float epsilon);
static int palm_render_cache_matches(
  const PalmRenderMesh* mesh,
  int grid_min_x,
  int grid_max_x,
  int grid_min_z,
  int grid_max_z,
  float radius,
  float cell_size,
  const SceneSettings* settings);
static int palm_render_populate_palm_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
static int palm_render_populate_tree_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
static int palm_render_populate_grass_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality);
static void palm_render_build_instance_transform(PalmInstanceData* instance, float x, float y, float z, float scale, float yaw_radians, PalmColor tint);

int palm_render_create(PalmRenderMesh* mesh)
{
  return palm_render_create_category(mesh, PALM_RENDER_CATEGORY_PALM);
}

int palm_render_create_category(PalmRenderMesh* mesh, PalmRenderCategory category)
{
  int asset_index = 0;
  const char* category_name = "foliage";

  if (mesh == NULL)
  {
    return 0;
  }

  memset(mesh, 0, sizeof(*mesh));

  for (asset_index = 0; asset_index < PALM_RENDER_MAX_VARIANTS; ++asset_index)
  {
    if (k_palm_render_asset_specs[asset_index].category != category)
    {
      continue;
    }
    if (palm_render_create_variant(&mesh->variants[mesh->variant_count], &k_palm_render_asset_specs[asset_index]))
    {
      mesh->variant_count += 1;
    }
  }

  switch (category)
  {
    case PALM_RENDER_CATEGORY_PALM:
      category_name = "palm";
      break;
    case PALM_RENDER_CATEGORY_TREE:
      category_name = "tree";
      break;
    case PALM_RENDER_CATEGORY_GRASS:
      category_name = "grass";
      break;
    default:
      break;
  }

  if (mesh->variant_count <= 0)
  {
    palm_render_destroy(mesh);
    palm_render_show_error("Palm Error", "Failed to load any foliage OBJ variants for the requested category.");
    return 0;
  }

  diagnostics_logf("palm_render: category=%s variant_count=%d", category_name, mesh->variant_count);
  return 1;
}

static int palm_render_create_variant(PalmRenderVariant* variant, const PalmRenderAssetSpec* asset_spec)
{
  PalmVertex* vertices = NULL;
  GLsizei vertex_count = 0;
  float model_height = 1.0f;
  char diffuse_texture_path[PLATFORM_PATH_MAX] = { 0 };
  int column = 0;

  if (variant == NULL || asset_spec == NULL || asset_spec->relative_obj_path == NULL)
  {
    return 0;
  }

  memset(variant, 0, sizeof(*variant));
  if (!palm_render_load_model_vertices(
    asset_spec->relative_obj_path,
    &vertices,
    &vertex_count,
    &model_height,
    diffuse_texture_path,
    sizeof(diffuse_texture_path)))
  {
    return 0;
  }

  glGenVertexArrays(1, &variant->vao);
  glGenBuffers(1, &variant->vertex_buffer);
  glGenBuffers(1, &variant->instance_buffer);
  if (variant->vao == 0U || variant->vertex_buffer == 0U || variant->instance_buffer == 0U)
  {
    free(vertices);
    palm_render_destroy_variant(variant);
    palm_render_show_error("Palm Error", "Failed to allocate palm render buffers.");
    return 0;
  }

  variant->vertex_count = vertex_count;
  variant->model_height = model_height;
  variant->category = asset_spec->category;
  variant->desired_height_min = asset_spec->desired_height_min;
  variant->desired_height_max = asset_spec->desired_height_max;
  variant->scale_jitter_min = asset_spec->scale_jitter_min;
  variant->scale_jitter_max = asset_spec->scale_jitter_max;
  variant->embed_depth_min = asset_spec->embed_depth_min;
  variant->embed_depth_max = asset_spec->embed_depth_max;
  variant->slope_limit = asset_spec->slope_limit;
  if (diffuse_texture_path[0] != '\0')
  {
    if (!palm_render_create_texture_from_file(diffuse_texture_path, &variant->diffuse_texture))
    {
      diagnostics_logf(
        "palm_render: failed to load diffuse texture '%s' for %s, using solid fallback",
        diffuse_texture_path,
        asset_spec->relative_obj_path);
    }
    else
    {
      diagnostics_logf(
        "palm_render: using diffuse texture '%s' for %s",
        diffuse_texture_path,
        asset_spec->relative_obj_path);
    }
  }
  if (variant->diffuse_texture == 0U &&
    !palm_render_create_solid_texture((PalmColor){ 1.0f, 1.0f, 1.0f }, &variant->diffuse_texture))
  {
    free(vertices);
    palm_render_destroy_variant(variant);
    palm_render_show_error("Palm Error", "Failed to allocate palm fallback texture.");
    return 0;
  }

  glBindVertexArray(variant->vao);

  glBindBuffer(GL_ARRAY_BUFFER, variant->vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, sizeof(PalmVertex) * (size_t)variant->vertex_count, vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex), (const void*)offsetof(PalmVertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex), (const void*)offsetof(PalmVertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex), (const void*)offsetof(PalmVertex, color));
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(PalmVertex), (const void*)offsetof(PalmVertex, texcoord));

  glBindBuffer(GL_ARRAY_BUFFER, variant->instance_buffer);
  glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
  for (column = 0; column < 4; ++column)
  {
    const GLuint attribute = (GLuint)(3 + column);
    const size_t offset = offsetof(PalmInstanceData, transform) + sizeof(float) * 4U * (size_t)column;
    glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, sizeof(PalmInstanceData), (const void*)offset);
    glVertexAttribDivisor(attribute, 1);
  }
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(PalmInstanceData), (const void*)offsetof(PalmInstanceData, tint));
  glVertexAttribDivisor(7, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  free(vertices);
  return 1;
}

static void palm_render_destroy_variant(PalmRenderVariant* variant)
{
  if (variant == NULL)
  {
    return;
  }

  if (variant->instance_buffer != 0U)
  {
    glDeleteBuffers(1, &variant->instance_buffer);
    variant->instance_buffer = 0U;
  }
  if (variant->vertex_buffer != 0U)
  {
    glDeleteBuffers(1, &variant->vertex_buffer);
    variant->vertex_buffer = 0U;
  }
  if (variant->diffuse_texture != 0U)
  {
    glDeleteTextures(1, &variant->diffuse_texture);
    variant->diffuse_texture = 0U;
  }
  if (variant->vao != 0U)
  {
    glDeleteVertexArrays(1, &variant->vao);
    variant->vao = 0U;
  }
  if (variant->cpu_instances != NULL)
  {
    free(variant->cpu_instances);
    variant->cpu_instances = NULL;
  }
  variant->cpu_instance_capacity = 0U;
  variant->vertex_count = 0;
  variant->instance_count = 0;
  variant->model_height = 0.0f;
  variant->category = PALM_RENDER_CATEGORY_PALM;
  variant->desired_height_min = 0.0f;
  variant->desired_height_max = 0.0f;
  variant->scale_jitter_min = 0.0f;
  variant->scale_jitter_max = 0.0f;
  variant->embed_depth_min = 0.0f;
  variant->embed_depth_max = 0.0f;
  variant->slope_limit = 0.0f;
}

void palm_render_destroy(PalmRenderMesh* mesh)
{
  int variant_index = 0;

  if (mesh == NULL)
  {
    return;
  }

  for (variant_index = 0; variant_index < PALM_RENDER_MAX_VARIANTS; ++variant_index)
  {
    palm_render_destroy_variant(&mesh->variants[variant_index]);
  }
  mesh->variant_count = 0;
  mesh->cache_valid = 0;
}

int palm_render_update_category(
  PalmRenderMesh* mesh,
  PalmRenderCategory category,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  int populate_result = 1;
  int variant_index = 0;
  int had_instances = 0;

  if (mesh == NULL)
  {
    return 0;
  }

  if (camera == NULL || mesh->variant_count <= 0 || active_settings->palm_size <= 0.01f || !palm_render_has_category(mesh, category))
  {
    for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
    {
      if (mesh->variants[variant_index].instance_count > 0)
      {
        had_instances = 1;
      }
      mesh->variants[variant_index].instance_count = 0;
    }
    mesh->cache_valid = 0;
    return had_instances ? palm_render_upload_instances(mesh) : 1;
  }

  switch (category)
  {
    case PALM_RENDER_CATEGORY_PALM:
      populate_result = palm_render_populate_palm_instances(mesh, camera, active_settings, quality);
      break;
    case PALM_RENDER_CATEGORY_TREE:
      populate_result = palm_render_populate_tree_instances(mesh, camera, active_settings, quality);
      break;
    case PALM_RENDER_CATEGORY_GRASS:
      populate_result = palm_render_populate_grass_instances(mesh, camera, active_settings, quality);
      break;
    default:
      return 0;
  }

  if (populate_result == 0)
  {
    return 0;
  }
  if (populate_result == 2)
  {
    return 1;
  }

  return palm_render_upload_instances(mesh);
}

int palm_render_update(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  return palm_render_update_category(mesh, PALM_RENDER_CATEGORY_PALM, camera, settings, quality);
}

void palm_render_draw(const PalmRenderMesh* mesh)
{
  int variant_index = 0;

  if (mesh == NULL || mesh->variant_count <= 0)
  {
    return;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    const PalmRenderVariant* variant = &mesh->variants[variant_index];

    if (variant->vao == 0U || variant->vertex_count <= 0 || variant->instance_count <= 0)
    {
      continue;
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, variant->diffuse_texture);
    glBindVertexArray(variant->vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, variant->vertex_count, variant->instance_count);
  }
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
}

static void palm_render_show_error(const char* title, const char* message)
{
  diagnostics_logf("%s: %s", title, message);
  platform_support_show_error_dialog(title, message);
}

static int palm_render_is_space(char value)
{
  return value == ' ' || value == '\t';
}

static const char* palm_render_find_last_path_separator(const char* path)
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

static const char* palm_render_get_basename(const char* path)
{
  const char* separator = palm_render_find_last_path_separator(path);
  return (separator != NULL) ? (separator + 1) : path;
}

static int palm_render_file_exists(const char* path)
{
  return platform_support_file_exists(path);
}

static int palm_render_build_relative_path(const char* base_path, const char* relative_path, char* out_path, size_t out_path_size)
{
  const char* last_separator = NULL;
  size_t directory_length = 0U;

  if (base_path == NULL || relative_path == NULL || out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

  last_separator = palm_render_find_last_path_separator(base_path);
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

static int palm_render_resolve_material_asset_path(const char* mtl_path, const char* relative_path, char* out_path, size_t out_path_size)
{
  char candidate_path[PLATFORM_PATH_MAX] = { 0 };
  char fallback_relative[PLATFORM_PATH_MAX] = { 0 };
  const char* basename = NULL;

  if (mtl_path == NULL || relative_path == NULL || out_path == NULL || out_path_size == 0U)
  {
    return 0;
  }

  if (palm_render_build_relative_path(mtl_path, relative_path, candidate_path, sizeof(candidate_path)) &&
    palm_render_file_exists(candidate_path))
  {
    (void)snprintf(out_path, out_path_size, "%s", candidate_path);
    return 1;
  }

  basename = palm_render_get_basename(relative_path);
  if (basename == NULL || basename[0] == '\0')
  {
    return 0;
  }

  (void)snprintf(fallback_relative, sizeof(fallback_relative), "material/%s", basename);
  if (palm_render_build_relative_path(mtl_path, fallback_relative, candidate_path, sizeof(candidate_path)) &&
    palm_render_file_exists(candidate_path))
  {
    (void)snprintf(out_path, out_path_size, "%s", candidate_path);
    return 1;
  }

  return 0;
}

static int palm_render_resolve_asset_path(const char* relative_path, char* out_path, size_t out_path_size)
{
  char module_path[PLATFORM_PATH_MAX] = { 0 };
  char candidate_path[PLATFORM_PATH_MAX] = { 0 };
  char current_directory[PLATFORM_PATH_MAX] = { 0 };
  char* last_separator = NULL;
  size_t base_length = 0U;
  static const char* k_res_prefix = "res/";
  static const char* k_res_fallbacks[] = {
    "res/",
    "../res/",
    "../../res/",
    "../../../res/"
  };
  size_t i = 0U;

  if (!platform_support_get_executable_path(module_path, sizeof(module_path)))
  {
    palm_render_show_error("Path Error", "Failed to resolve the executable directory for foliage assets.");
    return 0;
  }

  last_separator = (char*)palm_render_find_last_path_separator(module_path);
  if (last_separator == NULL)
  {
    palm_render_show_error("Path Error", "Failed to resolve the executable directory separator for foliage assets.");
    return 0;
  }

  last_separator[1] = '\0';
  if (strlen(module_path) + strlen(relative_path) + 1U <= sizeof(candidate_path))
  {
    (void)snprintf(candidate_path, sizeof(candidate_path), "%s%s", module_path, relative_path);
    if (palm_render_file_exists(candidate_path))
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
      if (palm_render_file_exists(candidate_path))
      {
        (void)snprintf(out_path, out_path_size, "%s", candidate_path);
        return 1;
      }
    }
  }

  if (strncmp(relative_path, k_res_prefix, strlen(k_res_prefix)) == 0)
  {
    const char* suffix = relative_path + strlen(k_res_prefix);
    for (i = 0U; i < sizeof(k_res_fallbacks) / sizeof(k_res_fallbacks[0]); ++i)
    {
      char fallback_relative[PLATFORM_PATH_MAX] = { 0 };

      if (strlen(k_res_fallbacks[i]) + strlen(suffix) + 1U > sizeof(fallback_relative))
      {
        continue;
      }

      (void)snprintf(fallback_relative, sizeof(fallback_relative), "%s%s", k_res_fallbacks[i], suffix);
      if (palm_render_build_relative_path(module_path, fallback_relative, candidate_path, sizeof(candidate_path)) &&
        palm_render_file_exists(candidate_path))
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
      "Failed to resolve foliage asset path for '%s'. Check the res folder next to the executable or in the project root.",
      relative_path
    );
    palm_render_show_error("Path Error", message);
  }
  return 0;
}

static int palm_render_load_text_file(const char* path, const char* label, char** out_text)
{
  char message[256] = { 0 };
  FILE* file = NULL;
  long file_size = 0L;
  size_t bytes_read = 0U;
  char* text = NULL;

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
    (void)snprintf(message, sizeof(message), "Failed to open %s file:\n%s", label, path);
    palm_render_show_error("File Error", message);
    return 0;
  }

  if (fseek(file, 0L, SEEK_END) != 0)
  {
    (void)snprintf(message, sizeof(message), "Failed to seek %s file.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  file_size = ftell(file);
  if (file_size < 0L || fseek(file, 0L, SEEK_SET) != 0)
  {
    (void)snprintf(message, sizeof(message), "%s file is unreadable.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  text = (char*)malloc((size_t)file_size + 1U);
  if (text == NULL)
  {
    palm_render_show_error("Memory Error", "Failed to allocate memory for palm asset loading.");
    fclose(file);
    return 0;
  }

  bytes_read = fread(text, 1U, (size_t)file_size, file);
  fclose(file);
  if (bytes_read != (size_t)file_size)
  {
    free(text);
    (void)snprintf(message, sizeof(message), "Failed to read %s file.", label);
    palm_render_show_error("File Error", message);
    return 0;
  }

  text[file_size] = '\0';
  *out_text = text;
  return 1;
}

static char* palm_render_trim_left(char* text)
{
  while (text != NULL && palm_render_is_space(*text))
  {
    ++text;
  }
  return text;
}

static void palm_render_trim_right_in_place(char* text)
{
  size_t length = 0U;

  if (text == NULL)
  {
    return;
  }

  length = strlen(text);
  while (length > 0U && palm_render_is_space(text[length - 1U]))
  {
    text[length - 1U] = '\0';
    length -= 1U;
  }
}

static const char* palm_render_skip_spaces_const(const char* text)
{
  while (text != NULL && palm_render_is_space(*text))
  {
    ++text;
  }
  return text;
}

static const char* palm_render_match_keyword(const char* text, const char* keyword)
{
  const size_t keyword_length = strlen(keyword);

  if (text == NULL || keyword == NULL)
  {
    return NULL;
  }
  if (strncmp(text, keyword, keyword_length) != 0)
  {
    return NULL;
  }
  if (text[keyword_length] != '\0' && !palm_render_is_space(text[keyword_length]))
  {
    return NULL;
  }

  return palm_render_skip_spaces_const(text + keyword_length);
}

static int palm_render_path_uses_black_key(const char* path)
{
  char lowered[PLATFORM_PATH_MAX] = { 0 };
  size_t index = 0U;
  size_t path_length = 0U;

  if (path == NULL)
  {
    return 0;
  }

  path_length = strlen(path);
  if (path_length >= sizeof(lowered))
  {
    path_length = sizeof(lowered) - 1U;
  }

  for (index = 0U; index < path_length; ++index)
  {
    lowered[index] = (char)tolower((unsigned char)path[index]);
  }
  lowered[path_length] = '\0';

  return strstr(lowered, "cut") != NULL;
}

static int palm_render_estimate_texture_color(const char* texture_path, PalmColor* out_color)
{
  unsigned char* pixels = NULL;
  int width = 0;
  int height = 0;
  int source_channels = 0;
  unsigned long long sum_r = 0ULL;
  unsigned long long sum_g = 0ULL;
  unsigned long long sum_b = 0ULL;
  unsigned long long sample_count = 0ULL;
  int texel_index = 0;

  if (texture_path == NULL || out_color == NULL)
  {
    return 0;
  }

  pixels = stbi_load(texture_path, &width, &height, &source_channels, 4);
  if (pixels == NULL || width <= 0 || height <= 0)
  {
    if (pixels != NULL)
    {
      stbi_image_free(pixels);
    }
    return 0;
  }

  for (texel_index = 0; texel_index < width * height; ++texel_index)
  {
    const unsigned char* rgba = &pixels[texel_index * 4];
    const unsigned char max_channel = (rgba[0] > rgba[1])
      ? ((rgba[0] > rgba[2]) ? rgba[0] : rgba[2])
      : ((rgba[1] > rgba[2]) ? rgba[1] : rgba[2]);
    const int has_alpha = source_channels >= 4;

    if ((has_alpha && rgba[3] <= 16U) || (palm_render_path_uses_black_key(texture_path) && max_channel <= 12U))
    {
      continue;
    }

    sum_r += rgba[0];
    sum_g += rgba[1];
    sum_b += rgba[2];
    sample_count += 1ULL;
  }

  stbi_image_free(pixels);
  if (sample_count == 0ULL)
  {
    return 0;
  }

  out_color->r = (float)sum_r / (255.0f * (float)sample_count);
  out_color->g = (float)sum_g / (255.0f * (float)sample_count);
  out_color->b = (float)sum_b / (255.0f * (float)sample_count);
  return 1;
}

static int palm_render_create_texture_from_file(const char* texture_path, GLuint* out_texture)
{
  unsigned char* pixels = NULL;
  int width = 0;
  int height = 0;
  int source_channels = 0;
  GLuint texture = 0U;

  if (texture_path == NULL || out_texture == NULL)
  {
    return 0;
  }

  *out_texture = 0U;
  pixels = stbi_load(texture_path, &width, &height, &source_channels, 4);
  if (pixels == NULL || width <= 0 || height <= 0)
  {
    if (pixels != NULL)
    {
      stbi_image_free(pixels);
    }
    return 0;
  }

  glGenTextures(1, &texture);
  if (texture == 0U)
  {
    stbi_image_free(pixels);
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(pixels);
  *out_texture = texture;
  return 1;
}

static int palm_render_create_solid_texture(PalmColor color, GLuint* out_texture)
{
  const unsigned char rgba[4] = {
    (unsigned char)(palm_render_clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f),
    (unsigned char)(palm_render_clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f),
    (unsigned char)(palm_render_clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f),
    255U
  };
  GLuint texture = 0U;

  if (out_texture == NULL)
  {
    return 0;
  }

  *out_texture = 0U;
  glGenTextures(1, &texture);
  if (texture == 0U)
  {
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindTexture(GL_TEXTURE_2D, 0);

  *out_texture = texture;
  return 1;
}

static int palm_render_material_needs_texture_color(const PalmMaterial* material)
{
  float min_channel = 0.0f;
  float max_channel = 0.0f;

  if (material == NULL || material->has_diffuse == 0)
  {
    return 1;
  }

  min_channel = material->diffuse.r;
  if (material->diffuse.g < min_channel)
  {
    min_channel = material->diffuse.g;
  }
  if (material->diffuse.b < min_channel)
  {
    min_channel = material->diffuse.b;
  }

  max_channel = material->diffuse.r;
  if (material->diffuse.g > max_channel)
  {
    max_channel = material->diffuse.g;
  }
  if (material->diffuse.b > max_channel)
  {
    max_channel = material->diffuse.b;
  }

  return (min_channel >= 0.94f && max_channel <= 1.01f) || (max_channel - min_channel <= 0.03f && max_channel >= 0.90f);
}

static int palm_render_reserve_memory(void** buffer, size_t* capacity, size_t required, size_t element_size)
{
  void* new_buffer = NULL;
  size_t new_capacity = 0U;

  if (buffer == NULL || capacity == NULL || element_size == 0U)
  {
    return 0;
  }
  if (required <= *capacity)
  {
    return 1;
  }

  new_capacity = (*capacity > 0U) ? *capacity : 256U;
  while (new_capacity < required)
  {
    if (new_capacity > (SIZE_MAX / 2U))
    {
      return 0;
    }
    new_capacity *= 2U;
  }
  if (new_capacity > (SIZE_MAX / element_size))
  {
    return 0;
  }

  new_buffer = realloc(*buffer, new_capacity * element_size);
  if (new_buffer == NULL)
  {
    return 0;
  }

  *buffer = new_buffer;
  *capacity = new_capacity;
  return 1;
}

static int palm_render_push_vec3(PalmVec3Array* array, PalmVec3 value)
{
  if (array == NULL || !palm_render_reserve_memory((void**)&array->data, &array->capacity, array->count + 1U, sizeof(PalmVec3)))
  {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static int palm_render_push_vec2(PalmVec2Array* array, PalmVec2 value)
{
  if (array == NULL || !palm_render_reserve_memory((void**)&array->data, &array->capacity, array->count + 1U, sizeof(PalmVec2)))
  {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static int palm_render_push_vertex(PalmVertexArray* array, PalmVertex value)
{
  if (array == NULL || !palm_render_reserve_memory((void**)&array->data, &array->capacity, array->count + 1U, sizeof(PalmVertex)))
  {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static PalmMaterial* palm_render_push_material(PalmMaterialArray* array, const char* name)
{
  PalmMaterial* material = NULL;

  if (array == NULL || name == NULL ||
    !palm_render_reserve_memory((void**)&array->data, &array->capacity, array->count + 1U, sizeof(PalmMaterial)))
  {
    return NULL;
  }

  material = &array->data[array->count];
  memset(material, 0, sizeof(*material));
  (void)snprintf(material->name, sizeof(material->name), "%s", name);
  material->diffuse = (PalmColor){ 0.70f, 0.70f, 0.70f };
  material->has_diffuse = 0;
  material->texture_path[0] = '\0';
  material->has_texture = 0;
  array->count += 1U;
  return material;
}

static PalmMaterial* palm_render_find_material_mutable(PalmMaterialArray* materials, const char* name)
{
  size_t material_index = 0U;

  if (materials == NULL || name == NULL)
  {
    return NULL;
  }

  for (material_index = 0U; material_index < materials->count; ++material_index)
  {
    if (strcmp(materials->data[material_index].name, name) == 0)
    {
      return &materials->data[material_index];
    }
  }

  return NULL;
}

static PalmMaterial* palm_render_get_or_create_material(PalmMaterialArray* materials, const char* name)
{
  PalmMaterial* material = NULL;

  if (materials == NULL || name == NULL || name[0] == '\0')
  {
    return NULL;
  }

  material = palm_render_find_material_mutable(materials, name);
  if (material != NULL)
  {
    return material;
  }

  return palm_render_push_material(materials, name);
}

static int palm_render_parse_mtl(const char* mtl_path, PalmMaterialArray* materials)
{
  char* source = NULL;
  char* cursor = NULL;
  PalmMaterial* current_material = NULL;

  if (materials == NULL)
  {
    return 0;
  }
  if (!palm_render_load_text_file(mtl_path, "MTL", &source))
  {
    return 0;
  }

  cursor = source;
  while (*cursor != '\0')
  {
    char* line_start = cursor;
    char* line_end = cursor;
    char* trimmed = NULL;
    const char* argument = NULL;

    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
    {
      ++line_end;
    }
    if (*line_end == '\r')
    {
      *line_end = '\0';
      cursor = line_end + 1;
      if (*cursor == '\n')
      {
        *cursor = '\0';
        cursor += 1;
      }
    }
    else if (*line_end == '\n')
    {
      *line_end = '\0';
      cursor = line_end + 1;
    }
    else
    {
      cursor = line_end;
    }

    trimmed = palm_render_trim_left(line_start);
    if (trimmed[0] == '\0' || trimmed[0] == '#')
    {
      continue;
    }

    argument = palm_render_match_keyword(trimmed, "newmtl");
    if (argument != NULL)
    {
      char material_name[64] = { 0 };

      (void)snprintf(material_name, sizeof(material_name), "%s", argument);
      palm_render_trim_right_in_place(material_name);
      current_material = palm_render_get_or_create_material(materials, material_name);
      if (current_material == NULL)
      {
        free(source);
        palm_render_show_error("Memory Error", "Failed to store palm material data.");
        return 0;
      }
    }
    else if ((argument = palm_render_match_keyword(trimmed, "Kd")) != NULL && current_material != NULL)
    {
      float r = 0.70f;
      float g = 0.70f;
      float b = 0.70f;

      if (palm_render_sscanf(argument, "%f %f %f", &r, &g, &b) == 3)
      {
        current_material->diffuse = (PalmColor){ r, g, b };
        current_material->has_diffuse = 1;
      }
    }
    else if ((argument = palm_render_match_keyword(trimmed, "map_Kd")) != NULL && current_material != NULL)
    {
      char relative_name[PLATFORM_PATH_MAX] = { 0 };
      char texture_path[PLATFORM_PATH_MAX] = { 0 };
      PalmColor texture_color = { 0.0f, 0.0f, 0.0f };

      (void)snprintf(relative_name, sizeof(relative_name), "%s", argument);
      palm_render_trim_right_in_place(relative_name);
      if (palm_render_resolve_material_asset_path(mtl_path, relative_name, texture_path, sizeof(texture_path)))
      {
        (void)snprintf(current_material->texture_path, sizeof(current_material->texture_path), "%s", texture_path);
        current_material->has_texture = 1;
        if (palm_render_material_needs_texture_color(current_material) &&
          palm_render_estimate_texture_color(texture_path, &texture_color))
        {
          current_material->diffuse = texture_color;
          current_material->has_diffuse = 1;
        }
      }
    }
  }

  free(source);
  return 1;
}

static const PalmMaterial* palm_render_find_material(const PalmMaterialArray* materials, const char* name)
{
  size_t i = 0U;

  if (materials == NULL || name == NULL)
  {
    return NULL;
  }

  for (i = 0U; i < materials->count; ++i)
  {
    if (strcmp(materials->data[i].name, name) == 0)
    {
      return &materials->data[i];
    }
  }

  return NULL;
}

static int palm_render_parse_face_vertex(const char* token, PalmObjIndex* out_index)
{
  const char* cursor = token;
  char* end = NULL;
  long value = 0L;

  if (token == NULL || out_index == NULL)
  {
    return 0;
  }

  memset(out_index, 0, sizeof(*out_index));
  value = strtol(cursor, &end, 10);
  if (end == cursor)
  {
    return 0;
  }
  out_index->position_index = (int)value;

  if (*end == '/')
  {
    cursor = end + 1;
    if (*cursor != '/')
    {
      value = strtol(cursor, &end, 10);
      if (end != cursor)
      {
        out_index->texcoord_index = (int)value;
      }
    }
    else
    {
      end = (char*)cursor;
    }

    if (*end == '/')
    {
      cursor = end + 1;
      value = strtol(cursor, &end, 10);
      if (end != cursor)
      {
        out_index->normal_index = (int)value;
      }
    }
  }

  return 1;
}

static int palm_render_resolve_obj_index(int obj_index, size_t count)
{
  int resolved = -1;

  if (obj_index > 0)
  {
    resolved = obj_index - 1;
  }
  else if (obj_index < 0)
  {
    resolved = (int)count + obj_index;
  }

  if (resolved < 0 || resolved >= (int)count)
  {
    return -1;
  }

  return resolved;
}

static PalmVec3 palm_render_vec3_subtract(PalmVec3 a, PalmVec3 b)
{
  PalmVec3 result = { a.x - b.x, a.y - b.y, a.z - b.z };
  return result;
}

static PalmVec3 palm_render_vec3_cross(PalmVec3 a, PalmVec3 b)
{
  PalmVec3 result = {
    a.y * b.z - a.z * b.y,
    a.z * b.x - a.x * b.z,
    a.x * b.y - a.y * b.x
  };
  return result;
}

static float palm_render_vec3_dot(PalmVec3 a, PalmVec3 b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static PalmVec3 palm_render_vec3_normalize(PalmVec3 value)
{
  const float length = sqrtf(palm_render_vec3_dot(value, value));
  if (length <= 0.00001f)
  {
    PalmVec3 fallback = { 0.0f, 1.0f, 0.0f };
    return fallback;
  }

  {
    const float inverse_length = 1.0f / length;
    PalmVec3 result = { value.x * inverse_length, value.y * inverse_length, value.z * inverse_length };
    return result;
  }
}

static int palm_render_load_model_vertices(
  const char* relative_obj_path,
  PalmVertex** out_vertices,
  GLsizei* out_vertex_count,
  float* out_model_height,
  char* out_diffuse_texture_path,
  size_t out_diffuse_texture_path_size)
{
  char obj_path[PLATFORM_PATH_MAX] = { 0 };
  char selected_texture_path[PLATFORM_PATH_MAX] = { 0 };
  char* source = NULL;
  char* cursor = NULL;
  PalmVec3Array positions = { 0 };
  PalmVec3Array normals = { 0 };
  PalmVec2Array texcoords = { 0 };
  PalmVertexArray vertices = { 0 };
  PalmMaterialArray materials = { 0 };
  PalmColor current_color = { 0.45f, 0.35f, 0.20f };
  const PalmMaterial* current_material = NULL;
  PalmVec3 bounds_min = { 1.0e9f, 1.0e9f, 1.0e9f };
  PalmVec3 bounds_max = { -1.0e9f, -1.0e9f, -1.0e9f };
  PalmVec3 base_center = { 0.0f, 0.0f, 0.0f };
  int texture_selection_valid = 1;
  size_t base_count = 0U;
  size_t i = 0U;

  if (relative_obj_path == NULL || out_vertices == NULL || out_vertex_count == NULL || out_model_height == NULL)
  {
    return 0;
  }
  *out_vertices = NULL;
  *out_vertex_count = 0;
  *out_model_height = 1.0f;
  if (out_diffuse_texture_path != NULL && out_diffuse_texture_path_size > 0U)
  {
    out_diffuse_texture_path[0] = '\0';
  }

  if (!palm_render_resolve_asset_path(relative_obj_path, obj_path, sizeof(obj_path)) ||
    !palm_render_load_text_file(obj_path, "OBJ", &source))
  {
    return 0;
  }

  cursor = source;
  while (*cursor != '\0')
  {
    char* line_start = cursor;
    char* line_end = cursor;
    char* trimmed = NULL;
    const char* argument = NULL;

    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
    {
      ++line_end;
    }
    if (*line_end == '\r')
    {
      *line_end = '\0';
      cursor = line_end + 1;
      if (*cursor == '\n')
      {
        *cursor = '\0';
        cursor += 1;
      }
    }
    else if (*line_end == '\n')
    {
      *line_end = '\0';
      cursor = line_end + 1;
    }
    else
    {
      cursor = line_end;
    }

    trimmed = palm_render_trim_left(line_start);
    if (trimmed[0] == '\0' || trimmed[0] == '#')
    {
      continue;
    }

    argument = palm_render_match_keyword(trimmed, "mtllib");
    if (argument != NULL)
    {
      char mtl_path[PLATFORM_PATH_MAX] = { 0 };
      char relative_name[PLATFORM_PATH_MAX] = { 0 };

      (void)snprintf(relative_name, sizeof(relative_name), "%s", argument);
      palm_render_trim_right_in_place(relative_name);
      if (!palm_render_build_relative_path(obj_path, relative_name, mtl_path, sizeof(mtl_path)))
      {
        diagnostics_logf("palm_render: skipped MTL reference '%s' for %s", relative_name, obj_path);
      }
      else if (!palm_render_file_exists(mtl_path))
      {
        diagnostics_logf("palm_render: missing MTL '%s' referenced by %s", mtl_path, obj_path);
      }
      else if (!palm_render_parse_mtl(mtl_path, &materials))
      {
        diagnostics_logf("palm_render: failed to parse MTL '%s', using fallback colors", mtl_path);
      }
    }
    else if ((argument = palm_render_match_keyword(trimmed, "usemtl")) != NULL)
    {
      char material_name[64] = { 0 };

      (void)snprintf(material_name, sizeof(material_name), "%s", argument);
      palm_render_trim_right_in_place(material_name);
      current_material = palm_render_find_material(&materials, material_name);
      current_color = (current_material != NULL) ? current_material->diffuse : (PalmColor){ 0.70f, 0.70f, 0.70f };
    }
    else if (strncmp(trimmed, "v ", 2U) == 0)
    {
      PalmVec3 value = { 0 };
      if (palm_render_sscanf(trimmed + 2, "%f %f %f", &value.x, &value.y, &value.z) == 3)
      {
        if (!palm_render_push_vec3(&positions, value))
        {
          palm_render_show_error("Memory Error", "Failed to store palm OBJ positions.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    }
    else if (strncmp(trimmed, "vt ", 3U) == 0)
    {
      PalmVec2 value = { 0.0f, 0.0f };
      if (palm_render_sscanf(trimmed + 3, "%f %f", &value.x, &value.y) >= 2)
      {
        if (!palm_render_push_vec2(&texcoords, value))
        {
          palm_render_show_error("Memory Error", "Failed to store palm OBJ texcoords.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    }
    else if (strncmp(trimmed, "vn ", 3U) == 0)
    {
      PalmVec3 value = { 0 };
      if (palm_render_sscanf(trimmed + 3, "%f %f %f", &value.x, &value.y, &value.z) == 3)
      {
        if (!palm_render_push_vec3(&normals, value))
        {
          palm_render_show_error("Memory Error", "Failed to store palm OBJ normals.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    }
    else if (strncmp(trimmed, "f ", 2U) == 0)
    {
      PalmObjIndex corners[PALM_RENDER_MAX_FACE_VERTICES] = { 0 };
      int corner_count = 0;
      char* token = palm_render_trim_left(trimmed + 2);

      while (token[0] != '\0' && corner_count < PALM_RENDER_MAX_FACE_VERTICES)
      {
        PalmObjIndex index = { 0 };
        char* separator = token;
        char separator_char = '\0';

        while (*separator != '\0' && *separator != ' ' && *separator != '\t')
        {
          ++separator;
        }
        separator_char = *separator;
        if (separator_char != '\0')
        {
          *separator = '\0';
        }

        if (token[0] != '\0' && palm_render_parse_face_vertex(token, &index))
        {
          corners[corner_count] = index;
          corner_count += 1;
        }

        if (separator_char == '\0')
        {
          token = separator;
        }
        else
        {
          token = palm_render_trim_left(separator + 1);
        }
      }

      if (corner_count >= 3)
      {
        int triangle_index = 0;

        for (triangle_index = 1; triangle_index + 1 < corner_count; ++triangle_index)
        {
          const PalmObjIndex triangle[3] = {
            corners[0],
            corners[triangle_index],
            corners[triangle_index + 1]
          };
          PalmVec3 triangle_positions[3];
          PalmVec3 triangle_normal = { 0.0f, 1.0f, 0.0f };
          PalmVec2 triangle_texcoords[3] = {
            { 0.0f, 0.0f },
            { 0.0f, 0.0f },
            { 0.0f, 0.0f }
          };
          const int material_has_texture = (
            current_material != NULL &&
            current_material->has_texture != 0 &&
            current_material->texture_path[0] != '\0' &&
            !palm_render_path_uses_black_key(current_material->texture_path));
          int vertex_index = 0;

          if (texture_selection_valid)
          {
            if (material_has_texture)
            {
              if (selected_texture_path[0] == '\0')
              {
                (void)snprintf(selected_texture_path, sizeof(selected_texture_path), "%s", current_material->texture_path);
              }
              else if (strcmp(selected_texture_path, current_material->texture_path) != 0)
              {
                texture_selection_valid = 0;
              }
            }
            else
            {
              texture_selection_valid = 0;
            }
          }

          for (vertex_index = 0; vertex_index < 3; ++vertex_index)
          {
            const int resolved_position = palm_render_resolve_obj_index(triangle[vertex_index].position_index, positions.count);
            if (resolved_position < 0)
            {
              palm_render_show_error("OBJ Error", "Palm OBJ contains an invalid position index.");
              free(source);
              free(vertices.data);
              free(positions.data);
              free(normals.data);
              free(texcoords.data);
              free(materials.data);
              return 0;
            }

            triangle_positions[vertex_index] = positions.data[resolved_position];
            if (material_has_texture && texture_selection_valid)
            {
              const int resolved_texcoord = palm_render_resolve_obj_index(triangle[vertex_index].texcoord_index, texcoords.count);
              if (resolved_texcoord < 0)
              {
                texture_selection_valid = 0;
              }
              else
              {
                triangle_texcoords[vertex_index] = texcoords.data[resolved_texcoord];
              }
            }
          }

          triangle_normal = palm_render_vec3_normalize(
            palm_render_vec3_cross(
              palm_render_vec3_subtract(triangle_positions[1], triangle_positions[0]),
              palm_render_vec3_subtract(triangle_positions[2], triangle_positions[0])));

          for (vertex_index = 0; vertex_index < 3; ++vertex_index)
          {
            const int resolved_normal = palm_render_resolve_obj_index(triangle[vertex_index].normal_index, normals.count);
            const PalmVec3 normal = (resolved_normal >= 0) ? normals.data[resolved_normal] : triangle_normal;
            PalmVertex vertex = {
              { triangle_positions[vertex_index].x, triangle_positions[vertex_index].y, triangle_positions[vertex_index].z },
              { normal.x, normal.y, normal.z },
              { current_color.r, current_color.g, current_color.b },
              { triangle_texcoords[vertex_index].x, 1.0f - triangle_texcoords[vertex_index].y }
            };

            if (!palm_render_push_vertex(&vertices, vertex))
            {
              palm_render_show_error("Memory Error", "Failed to store palm OBJ triangles.");
              free(source);
              free(vertices.data);
              free(positions.data);
              free(normals.data);
              free(texcoords.data);
              free(materials.data);
              return 0;
            }
          }
        }
      }
    }
  }

  if (positions.count == 0U || vertices.count == 0U)
  {
    palm_render_show_error("OBJ Error", "Palm OBJ did not contain any renderable geometry.");
    free(source);
    free(vertices.data);
    free(positions.data);
    free(normals.data);
    free(texcoords.data);
    free(materials.data);
    return 0;
  }

  for (i = 0U; i < positions.count; ++i)
  {
    const PalmVec3 value = positions.data[i];
    if (value.x < bounds_min.x)
    {
      bounds_min.x = value.x;
    }
    if (value.y < bounds_min.y)
    {
      bounds_min.y = value.y;
    }
    if (value.z < bounds_min.z)
    {
      bounds_min.z = value.z;
    }
    if (value.x > bounds_max.x)
    {
      bounds_max.x = value.x;
    }
    if (value.y > bounds_max.y)
    {
      bounds_max.y = value.y;
    }
    if (value.z > bounds_max.z)
    {
      bounds_max.z = value.z;
    }
  }

  for (i = 0U; i < positions.count; ++i)
  {
    const PalmVec3 value = positions.data[i];
    if (value.y <= bounds_min.y + 5.0f)
    {
      base_center.x += value.x;
      base_center.z += value.z;
      base_count += 1U;
    }
  }
  if (base_count > 0U)
  {
    base_center.x /= (float)base_count;
    base_center.z /= (float)base_count;
  }

  for (i = 0U; i < vertices.count; ++i)
  {
    vertices.data[i].position[0] -= base_center.x;
    vertices.data[i].position[1] -= bounds_min.y;
    vertices.data[i].position[2] -= base_center.z;
  }

  *out_vertices = vertices.data;
  *out_vertex_count = (GLsizei)vertices.count;
  *out_model_height = palm_render_clamp(bounds_max.y - bounds_min.y, 1.0f, 10000.0f);
  if (out_diffuse_texture_path != NULL &&
    out_diffuse_texture_path_size > 0U &&
    texture_selection_valid &&
    selected_texture_path[0] != '\0')
  {
    (void)snprintf(out_diffuse_texture_path, out_diffuse_texture_path_size, "%s", selected_texture_path);
  }

  diagnostics_logf(
    "palm_render: loaded OBJ vertices=%d height=%.2f textured=%s source=%s",
    (int)*out_vertex_count,
    *out_model_height,
    (texture_selection_valid && selected_texture_path[0] != '\0') ? "yes" : "no",
    obj_path);

  free(source);
  free(positions.data);
  free(normals.data);
  free(texcoords.data);
  free(materials.data);
  return 1;
}

static int palm_render_reserve_instances(PalmRenderVariant* variant, size_t required_instance_capacity)
{
  if (variant == NULL)
  {
    return 0;
  }
  if (required_instance_capacity <= variant->cpu_instance_capacity)
  {
    return 1;
  }

  if (!palm_render_reserve_memory(
    &variant->cpu_instances,
    &variant->cpu_instance_capacity,
    required_instance_capacity,
    sizeof(PalmInstanceData)))
  {
    palm_render_show_error("Memory Error", "Failed to allocate palm instance data.");
    return 0;
  }

  return 1;
}

static void palm_render_reset_instances(PalmRenderMesh* mesh)
{
  int variant_index = 0;

  if (mesh == NULL)
  {
    return;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    mesh->variants[variant_index].instance_count = 0;
  }
}

static int palm_render_upload_instances(PalmRenderMesh* mesh)
{
  int variant_index = 0;

  if (mesh == NULL)
  {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    PalmRenderVariant* variant = &mesh->variants[variant_index];

    glBindBuffer(GL_ARRAY_BUFFER, variant->instance_buffer);
    glBufferData(
      GL_ARRAY_BUFFER,
      sizeof(PalmInstanceData) * (size_t)variant->instance_count,
      (variant->instance_count > 0) ? variant->cpu_instances : NULL,
      GL_DYNAMIC_DRAW);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return 1;
}

static int palm_render_float_nearly_equal(float a, float b, float epsilon)
{
  return fabsf(a - b) <= epsilon;
}

static int palm_render_cache_matches(
  const PalmRenderMesh* mesh,
  int grid_min_x,
  int grid_max_x,
  int grid_min_z,
  int grid_max_z,
  float radius,
  float cell_size,
  const SceneSettings* settings)
{
  if (mesh == NULL || settings == NULL || mesh->cache_valid == 0)
  {
    return 0;
  }

  return
    mesh->cache_grid_min_x == grid_min_x &&
    mesh->cache_grid_max_x == grid_max_x &&
    mesh->cache_grid_min_z == grid_min_z &&
    mesh->cache_grid_max_z == grid_max_z &&
    palm_render_float_nearly_equal(mesh->cache_radius, radius, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_cell_size, cell_size, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_palm_size, settings->palm_size, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_palm_count, settings->palm_count, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_palm_fruit_density, settings->palm_fruit_density, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_palm_render_radius, settings->palm_render_radius, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_terrain_base_height, settings->terrain_base_height, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_terrain_height_scale, settings->terrain_height_scale, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_terrain_roughness, settings->terrain_roughness, 0.001f) &&
    palm_render_float_nearly_equal(mesh->cache_terrain_ridge_strength, settings->terrain_ridge_strength, 0.001f);
}

static int palm_render_has_category(const PalmRenderMesh* mesh, int category)
{
  int variant_index = 0;

  if (mesh == NULL)
  {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    const PalmRenderVariant* variant = &mesh->variants[variant_index];
    if (variant->category == category && variant->vao != 0U && variant->vertex_count > 0)
    {
      return 1;
    }
  }

  return 0;
}

static GLsizei palm_render_get_max_vertex_count_for_category(const PalmRenderMesh* mesh, int category)
{
  GLsizei max_vertex_count = 0;
  int variant_index = 0;

  if (mesh == NULL)
  {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    const PalmRenderVariant* variant = &mesh->variants[variant_index];
    if (variant->category == category && variant->vertex_count > max_vertex_count)
    {
      max_vertex_count = variant->vertex_count;
    }
  }

  return max_vertex_count;
}

static PalmRenderVariant* palm_render_pick_variant(PalmRenderMesh* mesh, int category, int grid_x, int grid_z, unsigned int seed)
{
  PalmRenderVariant* matches[PALM_RENDER_MAX_VARIANTS] = { 0 };
  int match_count = 0;
  int variant_index = 0;
  int selected_index = 0;

  if (mesh == NULL)
  {
    return NULL;
  }

  for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
  {
    PalmRenderVariant* variant = &mesh->variants[variant_index];
    if (variant->category == category && variant->vao != 0U && variant->vertex_count > 0)
    {
      matches[match_count] = variant;
      match_count += 1;
    }
  }

  if (match_count <= 0)
  {
    return NULL;
  }

  selected_index = (int)(palm_render_hash_unit(grid_x, grid_z, seed) * (float)match_count);
  if (selected_index >= match_count)
  {
    selected_index = match_count - 1;
  }

  return matches[selected_index];
}

static int palm_render_populate_palm_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count = palm_render_get_max_vertex_count_for_category(mesh, PALM_RENDER_CATEGORY_PALM);
  float radius = 0.0f;
  int effective_palm_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL || !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_PALM) || max_vertex_count <= 0)
  {
    return 1;
  }

  lod_config.requested_radius = settings->palm_render_radius;
  lod_config.requested_radius_min = 80.0f;
  lod_config.requested_radius_max = 900.0f;
  lod_config.radius_scale_low = 1.10f;
  lod_config.radius_scale_high = 1.28f;
  lod_config.effective_radius_min = 96.0f;
  lod_config.effective_radius_max = 1150.0f;
  lod_config.requested_instance_count = settings->palm_count * 0.72f;
  lod_config.requested_instance_count_min = 0.0f;
  lod_config.requested_instance_count_max = 4000.0f;
  lod_config.instance_budget_min = 36;
  lod_config.instance_budget_max = 96;
  lod_config.source_vertex_count = (float)max_vertex_count;
  lod_config.fallback_vertex_count = 92502.0f;
  lod_config.vertex_budget_low = 3600000.0f;
  lod_config.vertex_budget_high = 8200000.0f;
  lod_config.cell_size_min = 16.0f;
  lod_config.cell_size_max = 120.0f;

  lod_state = procedural_lod_resolve(quality, &lod_config);
  radius = lod_state.effective_radius;
  effective_palm_target = lod_state.effective_instance_count;
  cell_size = lod_state.cell_size;

  if (effective_palm_target <= 0 || cell_size <= 0.0f)
  {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) * (size_t)(grid_max_z - grid_min_z + 1);
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z, grid_max_z, radius, cell_size, settings))
    {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
    {
      PalmRenderVariant* variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_PALM && !palm_render_reserve_instances(variant, estimated_capacity))
      {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z)
    {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x)
      {
        const float offset_x = palm_render_hash_unit(grid_x, grid_z, 0U);
        const float offset_z = palm_render_hash_unit(grid_x, grid_z, 1U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 2U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 3U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant* variant = palm_render_pick_variant(mesh, PALM_RENDER_CATEGORY_PALM, grid_x, grid_z, 5U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 4U) * (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_height(x, z, settings);
        const float fruit_density = palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        PalmColor tint = {
          palm_render_mix(0.94f, 1.04f, variation) + fruit_density * 0.05f,
          palm_render_mix(0.92f, 1.10f, scale_jitter),
          palm_render_mix(0.92f, 1.03f, variation) - fruit_density * 0.03f
        };
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f)
        {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit)
        {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity)
        {
          continue;
        }

        desired_height = palm_render_mix(variant->desired_height_min, variant->desired_height_max, variation) *
          settings->palm_size * palm_render_mix(variant->scale_jitter_min, variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min, variant->embed_depth_max, variation) * settings->palm_size + slope * 0.18f;

        palm_render_build_instance_transform(
          &((PalmInstanceData*)variant->cpu_instances)[variant->instance_count],
          x,
          ground_y - embed_depth,
          z,
          desired_height / variant->model_height,
          yaw,
          tint);
        variant->instance_count += 1;
      }
    }

    mesh->cache_valid = 1;
    mesh->cache_grid_min_x = grid_min_x;
    mesh->cache_grid_max_x = grid_max_x;
    mesh->cache_grid_min_z = grid_min_z;
    mesh->cache_grid_max_z = grid_max_z;
    mesh->cache_radius = radius;
    mesh->cache_cell_size = cell_size;
    mesh->cache_palm_size = settings->palm_size;
    mesh->cache_palm_count = settings->palm_count;
    mesh->cache_palm_fruit_density = settings->palm_fruit_density;
    mesh->cache_palm_render_radius = settings->palm_render_radius;
    mesh->cache_terrain_base_height = settings->terrain_base_height;
    mesh->cache_terrain_height_scale = settings->terrain_height_scale;
    mesh->cache_terrain_roughness = settings->terrain_roughness;
    mesh->cache_terrain_ridge_strength = settings->terrain_ridge_strength;
  }

  return 1;
}

static int palm_render_populate_tree_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count = palm_render_get_max_vertex_count_for_category(mesh, PALM_RENDER_CATEGORY_TREE);
  float radius = 0.0f;
  int effective_tree_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL || !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_TREE) || max_vertex_count <= 0)
  {
    return 1;
  }

  lod_config.requested_radius = settings->palm_render_radius;
  lod_config.requested_radius_min = 80.0f;
  lod_config.requested_radius_max = 900.0f;
  lod_config.radius_scale_low = 1.08f;
  lod_config.radius_scale_high = 1.22f;
  lod_config.effective_radius_min = 94.0f;
  lod_config.effective_radius_max = 1120.0f;
  lod_config.requested_instance_count = settings->palm_count * 0.30f;
  lod_config.requested_instance_count_min = 0.0f;
  lod_config.requested_instance_count_max = 2200.0f;
  lod_config.instance_budget_min = 12;
  lod_config.instance_budget_max = 42;
  lod_config.source_vertex_count = (float)max_vertex_count;
  lod_config.fallback_vertex_count = 92502.0f;
  lod_config.vertex_budget_low = 1800000.0f;
  lod_config.vertex_budget_high = 3600000.0f;
  lod_config.cell_size_min = 24.0f;
  lod_config.cell_size_max = 160.0f;

  lod_state = procedural_lod_resolve(quality, &lod_config);
  radius = lod_state.effective_radius;
  effective_tree_target = lod_state.effective_instance_count;
  cell_size = lod_state.cell_size;

  if (effective_tree_target <= 0 || cell_size <= 0.0f)
  {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) * (size_t)(grid_max_z - grid_min_z + 1);
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z, grid_max_z, radius, cell_size, settings))
    {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
    {
      PalmRenderVariant* variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_TREE && !palm_render_reserve_instances(variant, estimated_capacity))
      {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z)
    {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x)
      {
        const float offset_x = palm_render_hash_unit(grid_x, grid_z, 40U);
        const float offset_z = palm_render_hash_unit(grid_x, grid_z, 41U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 42U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 43U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant* variant = palm_render_pick_variant(mesh, PALM_RENDER_CATEGORY_TREE, grid_x, grid_z, 44U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 45U) * (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_height(x, z, settings);
        const float fruit_density = palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        PalmColor tint = {
          palm_render_mix(0.90f, 1.02f, variation),
          palm_render_mix(0.92f, 1.06f, scale_jitter) - fruit_density * 0.01f,
          palm_render_mix(0.88f, 0.99f, variation) - fruit_density * 0.02f
        };
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f)
        {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit)
        {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity)
        {
          continue;
        }

        desired_height = palm_render_mix(variant->desired_height_min, variant->desired_height_max, variation) *
          settings->palm_size * palm_render_mix(variant->scale_jitter_min, variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min, variant->embed_depth_max, variation) * settings->palm_size + slope * 0.14f;

        palm_render_build_instance_transform(
          &((PalmInstanceData*)variant->cpu_instances)[variant->instance_count],
          x,
          ground_y - embed_depth,
          z,
          desired_height / variant->model_height,
          yaw,
          tint);
        variant->instance_count += 1;
      }
    }

    mesh->cache_valid = 1;
    mesh->cache_grid_min_x = grid_min_x;
    mesh->cache_grid_max_x = grid_max_x;
    mesh->cache_grid_min_z = grid_min_z;
    mesh->cache_grid_max_z = grid_max_z;
    mesh->cache_radius = radius;
    mesh->cache_cell_size = cell_size;
    mesh->cache_palm_size = settings->palm_size;
    mesh->cache_palm_count = settings->palm_count;
    mesh->cache_palm_fruit_density = settings->palm_fruit_density;
    mesh->cache_palm_render_radius = settings->palm_render_radius;
    mesh->cache_terrain_base_height = settings->terrain_base_height;
    mesh->cache_terrain_height_scale = settings->terrain_height_scale;
    mesh->cache_terrain_roughness = settings->terrain_roughness;
    mesh->cache_terrain_ridge_strength = settings->terrain_ridge_strength;
  }

  return 1;
}

static int palm_render_populate_grass_instances(
  PalmRenderMesh* mesh,
  const CameraState* camera,
  const SceneSettings* settings,
  const RendererQualityProfile* quality)
{
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count = palm_render_get_max_vertex_count_for_category(mesh, PALM_RENDER_CATEGORY_GRASS);
  float radius = 0.0f;
  int effective_grass_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL || !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_GRASS) || max_vertex_count <= 0)
  {
    return 1;
  }

  lod_config.requested_radius = settings->palm_render_radius;
  lod_config.requested_radius_min = 80.0f;
  lod_config.requested_radius_max = 900.0f;
  lod_config.radius_scale_low = 1.00f;
  lod_config.radius_scale_high = 1.08f;
  lod_config.effective_radius_min = 72.0f;
  lod_config.effective_radius_max = 760.0f;
  lod_config.requested_instance_count = settings->palm_count * 20.0f;
  lod_config.requested_instance_count_min = 0.0f;
  lod_config.requested_instance_count_max = 18000.0f;
  lod_config.instance_budget_min = 72;
  lod_config.instance_budget_max = 900;
  lod_config.source_vertex_count = (float)max_vertex_count;
  lod_config.fallback_vertex_count = 900.0f;
  lod_config.vertex_budget_low = 1200000.0f;
  lod_config.vertex_budget_high = 3200000.0f;
  lod_config.cell_size_min = 6.0f;
  lod_config.cell_size_max = 24.0f;

  lod_state = procedural_lod_resolve(quality, &lod_config);
  radius = lod_state.effective_radius;
  effective_grass_target = lod_state.effective_instance_count;
  cell_size = lod_state.cell_size;

  if (effective_grass_target <= 0 || cell_size <= 0.0f)
  {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) * (size_t)(grid_max_z - grid_min_z + 1);
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z, grid_max_z, radius, cell_size, settings))
    {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count; ++variant_index)
    {
      PalmRenderVariant* variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_GRASS && !palm_render_reserve_instances(variant, estimated_capacity))
      {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z)
    {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x)
      {
        const float offset_x = palm_render_mix(0.18f, 0.82f, palm_render_hash_unit(grid_x, grid_z, 20U));
        const float offset_z = palm_render_mix(0.18f, 0.82f, palm_render_hash_unit(grid_x, grid_z, 21U));
        const float coverage = palm_render_hash_unit(grid_x, grid_z, 22U);
        const int patch_x = (int)floorf((float)grid_x * 0.35f);
        const int patch_z = (int)floorf((float)grid_z * 0.35f);
        const float patch = palm_render_hash_unit(patch_x, patch_z, 23U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 24U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 25U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant* variant = palm_render_pick_variant(mesh, PALM_RENDER_CATEGORY_GRASS, grid_x, grid_z, 27U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 26U) * (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_height(x, z, settings);
        const float fruit_density = palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        const float hole_threshold = palm_render_mix(0.05f, 0.24f, patch);
        PalmColor tint = {
          palm_render_mix(0.88f, 1.08f, variation) - fruit_density * 0.02f,
          palm_render_mix(0.92f, 1.16f, scale_jitter) + fruit_density * 0.03f,
          palm_render_mix(0.82f, 1.00f, variation) - fruit_density * 0.04f
        };
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f)
        {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit || coverage < hole_threshold)
        {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity)
        {
          continue;
        }

        desired_height = palm_render_mix(variant->desired_height_min, variant->desired_height_max, variation) *
          settings->palm_size * palm_render_mix(variant->scale_jitter_min, variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min, variant->embed_depth_max, variation) * settings->palm_size + slope * 0.04f;

        palm_render_build_instance_transform(
          &((PalmInstanceData*)variant->cpu_instances)[variant->instance_count],
          x,
          ground_y - embed_depth,
          z,
          desired_height / variant->model_height,
          yaw,
          tint);
        variant->instance_count += 1;
      }
    }

    mesh->cache_valid = 1;
    mesh->cache_grid_min_x = grid_min_x;
    mesh->cache_grid_max_x = grid_max_x;
    mesh->cache_grid_min_z = grid_min_z;
    mesh->cache_grid_max_z = grid_max_z;
    mesh->cache_radius = radius;
    mesh->cache_cell_size = cell_size;
    mesh->cache_palm_size = settings->palm_size;
    mesh->cache_palm_count = settings->palm_count;
    mesh->cache_palm_fruit_density = settings->palm_fruit_density;
    mesh->cache_palm_render_radius = settings->palm_render_radius;
    mesh->cache_terrain_base_height = settings->terrain_base_height;
    mesh->cache_terrain_height_scale = settings->terrain_height_scale;
    mesh->cache_terrain_roughness = settings->terrain_roughness;
    mesh->cache_terrain_ridge_strength = settings->terrain_ridge_strength;
  }

  return 1;
}

static float palm_render_clamp(float value, float min_value, float max_value)
{
  if (value < min_value)
  {
    return min_value;
  }
  if (value > max_value)
  {
    return max_value;
  }
  return value;
}

static float palm_render_mix(float a, float b, float t)
{
  return a + (b - a) * t;
}

static float palm_render_hash_unit(int x, int z, unsigned int seed)
{
  unsigned int state = (unsigned int)(x * 374761393) ^ (unsigned int)(z * 668265263) ^ (seed * 2246822519U);
  state = (state ^ (state >> 13)) * 1274126177U;
  state ^= state >> 16;
  return (float)(state & 0x00FFFFFFU) / (float)0x01000000U;
}

static float palm_render_estimate_slope(float x, float z, const SceneSettings* settings)
{
  const float sample_offset = 3.0f;
  const float x0 = terrain_get_height(x - sample_offset, z, settings);
  const float x1 = terrain_get_height(x + sample_offset, z, settings);
  const float z0 = terrain_get_height(x, z - sample_offset, settings);
  const float z1 = terrain_get_height(x, z + sample_offset, settings);
  const float dx = fabsf(x1 - x0) / (sample_offset * 2.0f);
  const float dz = fabsf(z1 - z0) / (sample_offset * 2.0f);

  return dx + dz;
}

static void palm_render_build_instance_transform(PalmInstanceData* instance, float x, float y, float z, float scale, float yaw_radians, PalmColor tint)
{
  const float c = cosf(yaw_radians) * scale;
  const float s = sinf(yaw_radians) * scale;

  if (instance == NULL)
  {
    return;
  }

  memset(instance, 0, sizeof(*instance));
  instance->transform[0] = c;
  instance->transform[2] = s;
  instance->transform[5] = scale;
  instance->transform[8] = -s;
  instance->transform[10] = c;
  instance->transform[12] = x;
  instance->transform[13] = y;
  instance->transform[14] = z;
  instance->transform[15] = 1.0f;
  instance->tint[0] = palm_render_clamp(tint.r, 0.75f, 1.25f);
  instance->tint[1] = palm_render_clamp(tint.g, 0.75f, 1.25f);
  instance->tint[2] = palm_render_clamp(tint.b, 0.75f, 1.25f);
  instance->tint[3] = 1.0f;
}
