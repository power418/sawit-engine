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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define palm_render_sscanf sscanf_s
#else
#define palm_render_sscanf sscanf
#endif

typedef struct PalmVertex {
  float position[3];
  float normal[3];
  float color[3];
  float texcoord[2];
} PalmVertex;

typedef struct PalmColor {
  float r;
  float g;
  float b;
} PalmColor;

typedef struct PalmVec2 {
  float x;
  float y;
} PalmVec2;

typedef struct PalmVec3 {
  float x;
  float y;
  float z;
} PalmVec3;

typedef struct PalmInstanceData {
  float transform[16];
  float tint[4];
} PalmInstanceData;

typedef struct PalmObjIndex {
  int position_index;
  int normal_index;
  int texcoord_index;
} PalmObjIndex;

typedef struct PalmMaterial {
  char name[64];
  PalmColor diffuse;
  int has_diffuse;
  char texture_path[PLATFORM_PATH_MAX];
  int has_texture;
} PalmMaterial;

typedef struct PalmRenderAssetSpec {
  const char *relative_asset_path;
  int category;
  float desired_height_min;
  float desired_height_max;
  float scale_jitter_min;
  float scale_jitter_max;
  float embed_depth_min;
  float embed_depth_max;
  float slope_limit;
} PalmRenderAssetSpec;

typedef struct PalmVec3Array {
  PalmVec3 *data;
  size_t count;
  size_t capacity;
} PalmVec3Array;

typedef struct PalmVec2Array {
  PalmVec2 *data;
  size_t count;
  size_t capacity;
} PalmVec2Array;

typedef struct PalmVertexArray {
  PalmVertex *data;
  size_t count;
  size_t capacity;
} PalmVertexArray;

typedef struct PalmMaterialArray {
  PalmMaterial *data;
  size_t count;
  size_t capacity;
} PalmMaterialArray;

typedef struct PalmGltfBufferView {
  size_t byte_offset;
  size_t byte_length;
  size_t byte_stride;
} PalmGltfBufferView;

typedef struct PalmGltfAccessor {
  int buffer_view_index;
  size_t byte_offset;
  size_t count;
  int component_type;
  int component_count;
  int normalized;
} PalmGltfAccessor;

typedef struct PalmGltfTexture {
  int image_index;
} PalmGltfTexture;

typedef struct PalmGltfMaterial {
  PalmColor base_color;
  int has_base_color;
  int base_color_texture_index;
} PalmGltfMaterial;

typedef struct PalmGltfImage {
  const unsigned char *bytes;
  size_t byte_length;
} PalmGltfImage;

typedef struct PalmGltfDecodedImage {
  unsigned char *pixels;
  int width;
  int height;
  int channels;
} PalmGltfDecodedImage;

typedef struct PalmGltfPrimitive {
  int indices_accessor_index;
  int position_accessor_index;
  int normal_accessor_index;
  int texcoord_accessor_index;
  int material_index;
  int mode;
} PalmGltfPrimitive;

typedef struct PalmGltfMesh {
  PalmGltfPrimitive *primitives;
  size_t primitive_count;
} PalmGltfMesh;

typedef struct PalmGltfNode {
  int mesh_index;
  int *children;
  size_t child_count;
  float transform[16];
} PalmGltfNode;

typedef struct PalmGltfDocument {
  PalmGltfBufferView *buffer_views;
  PalmGltfAccessor *accessors;
  PalmGltfTexture *textures;
  PalmGltfMaterial *materials;
  PalmGltfImage *images;
  PalmGltfDecodedImage *decoded_images;
  PalmGltfMesh *meshes;
  PalmGltfNode *nodes;
  int *scene_roots;
  size_t buffer_view_count;
  size_t accessor_count;
  size_t texture_count;
  size_t material_count;
  size_t image_count;
  size_t mesh_count;
  size_t node_count;
  size_t scene_root_count;
  unsigned char *owned_data;
  const unsigned char *binary_chunk;
  size_t binary_chunk_size;
} PalmGltfDocument;

enum { PALM_RENDER_MAX_FACE_VERTICES = 32 };

static const float k_palm_render_pi = 3.14159265f;
static const float k_palm_render_terrain_half_extent = 3200.0f;
static const PalmRenderAssetSpec k_palm_render_asset_specs[] = {
    {"res/obj/kelapasawit.obj", PALM_RENDER_CATEGORY_PALM, 8.2f, 12.4f, 0.82f,
     1.22f, 0.22f, 0.58f, 1.45f},
    {"res/obj/Date Palm.obj", PALM_RENDER_CATEGORY_PALM, 8.6f, 12.8f, 0.82f,
     1.18f, 0.20f, 0.54f, 1.40f},
    {"res/low_house_wis_doors_all_in_one.glb", PALM_RENDER_CATEGORY_HOUSE,
     11.5f, 18.5f, 0.92f, 1.12f, 0.06f, 0.20f, 0.60f},
    {"res/chinese_house2.glb", PALM_RENDER_CATEGORY_HOUSE, 15.0f, 24.0f, 0.88f,
     1.08f, 0.10f, 0.24f, 0.46f},
    {"res/obj/Tree.obj", PALM_RENDER_CATEGORY_TREE, 9.2f, 14.6f, 0.80f, 1.16f,
     0.24f, 0.60f, 1.32f},
    {"res/obj/Trava Kolosok.obj", PALM_RENDER_CATEGORY_GRASS, 0.62f, 1.28f,
     0.82f, 1.24f, 0.02f, 0.08f, 1.08f},
    {"res/obj/mountain.obj", PALM_RENDER_CATEGORY_MOUNTAIN, 980.0f, 1720.0f,
     0.90f, 1.20f, 140.0f, 380.0f, 12.0f}};
static const size_t k_palm_render_asset_spec_count =
    sizeof(k_palm_render_asset_specs) / sizeof(k_palm_render_asset_specs[0]);

static void palm_render_show_error(const char *title, const char *message);
static int palm_render_is_space(char value);
static const char *palm_render_find_last_path_separator(const char *path);
static const char *palm_render_get_basename(const char *path);
static int palm_render_file_exists(const char *path);
static int palm_render_build_relative_path(const char *base_path,
                                           const char *relative_path,
                                           char *out_path,
                                           size_t out_path_size);
static int palm_render_resolve_asset_path(const char *relative_path,
                                          char *out_path, size_t out_path_size);
static int palm_render_resolve_material_asset_path(const char *mtl_path,
                                                   const char *relative_path,
                                                   char *out_path,
                                                   size_t out_path_size);
static int palm_render_load_text_file(const char *path, const char *label,
                                      char **out_text);
static int palm_render_load_binary_file(const char *path, const char *label,
                                        unsigned char **out_data,
                                        size_t *out_size);
static char *palm_render_trim_left(char *text);
static void palm_render_trim_right_in_place(char *text);
static const char *palm_render_skip_spaces_const(const char *text);
static const char *palm_render_match_keyword(const char *text,
                                             const char *keyword);
static int palm_render_reserve_memory(void **buffer, size_t *capacity,
                                      size_t required, size_t element_size);
static int palm_render_push_vec3(PalmVec3Array *array, PalmVec3 value);
static int palm_render_push_vec2(PalmVec2Array *array, PalmVec2 value);
static int palm_render_push_vertex(PalmVertexArray *array, PalmVertex value);
static int palm_render_path_uses_black_key(const char *path);
static int palm_render_estimate_texture_color(const char *texture_path,
                                              PalmColor *out_color);
static int palm_render_create_texture_from_file(const char *texture_path,
                                                GLuint *out_texture);
static int palm_render_create_solid_texture(PalmColor color,
                                            GLuint *out_texture);
static int
palm_render_material_needs_texture_color(const PalmMaterial *material);
static PalmMaterial *
palm_render_find_material_mutable(PalmMaterialArray *materials,
                                  const char *name);
static PalmMaterial *palm_render_push_material(PalmMaterialArray *array,
                                               const char *name);
static PalmMaterial *
palm_render_get_or_create_material(PalmMaterialArray *materials,
                                   const char *name);
static int palm_render_parse_mtl(const char *mtl_path,
                                 PalmMaterialArray *materials);
static const PalmMaterial *
palm_render_find_material(const PalmMaterialArray *materials, const char *name);
static int palm_render_parse_face_vertex(const char *token,
                                         PalmObjIndex *out_index);
static int palm_render_resolve_obj_index(int obj_index, size_t count);
static PalmVec3 palm_render_vec3_subtract(PalmVec3 a, PalmVec3 b);
static PalmVec3 palm_render_vec3_cross(PalmVec3 a, PalmVec3 b);
static float palm_render_vec3_dot(PalmVec3 a, PalmVec3 b);
static PalmVec3 palm_render_vec3_normalize(PalmVec3 value);
static void palm_render_matrix_identity(float *out_matrix);
static void palm_render_matrix_multiply(float *out_matrix, const float *left,
                                        const float *right);
static void palm_render_matrix_from_trs(float *out_matrix,
                                        const PalmVec3 *translation,
                                        const float *rotation,
                                        const PalmVec3 *scale);
static PalmVec3 palm_render_transform_point(const float *matrix,
                                            PalmVec3 value);
static PalmVec3 palm_render_transform_direction(const float *matrix,
                                                PalmVec3 value);
#define JSON_IMPLEMENTATION
#define JSON_STATIC
#include "json.h"

#define PalmJsonToken struct json_token
#define PALM_JSON_OBJECT JSON_OBJECT
#define PALM_JSON_ARRAY JSON_ARRAY
#define PALM_JSON_STRING JSON_STRING

static int palm_render_json_parse(const char *json, size_t length,
                                  PalmJsonToken **out_tokens,
                                  size_t *out_token_count);
static int palm_render_json_root_object_get(const PalmJsonToken *tokens,
                                            size_t token_count,
                                            const char *key);
static int palm_render_json_token_equals(const char *json,
                                         const PalmJsonToken *token,
                                         const char *expected);
static int palm_render_json_token_to_int(const char *json,
                                         const PalmJsonToken *token,
                                         int *out_value);
static int palm_render_json_token_to_size(const char *json,
                                          const PalmJsonToken *token,
                                          size_t *out_value);
static int palm_render_json_token_to_float(const char *json,
                                           const PalmJsonToken *token,
                                           float *out_value);
static int palm_render_json_object_get(const char *json,
                                       const PalmJsonToken *tokens,
                                       size_t token_count, size_t object_index,
                                       const char *key);
static int palm_render_json_array_get(const PalmJsonToken *tokens,
                                      size_t token_count, size_t array_index,
                                      size_t element_index);
static int palm_render_gltf_accessor_component_count(const char *type_name);
static size_t palm_render_gltf_component_size(int component_type);
static float
palm_render_gltf_read_component_as_float(const unsigned char *source,
                                         int component_type, int normalized);
static unsigned int palm_render_gltf_read_index(const unsigned char *source,
                                                int component_type);
static int palm_render_parse_glb_document(const char *relative_asset_path,
                                          PalmGltfDocument *out_document);
static void palm_render_destroy_gltf_document(PalmGltfDocument *document);
static int palm_render_decode_gltf_image(PalmGltfDocument *document,
                                         int image_index);
static PalmColor
palm_render_sample_gltf_material_color(const PalmGltfDocument *document,
                                       int material_index, PalmVec2 texcoord);
static int palm_render_append_gltf_node_vertices(
    const PalmGltfDocument *document, int node_index,
    const float *parent_matrix, PalmVertexArray *vertices);
static int palm_render_load_obj_vertices(
    const char *relative_asset_path, PalmVertex **out_vertices,
    GLsizei *out_vertex_count, float *out_model_height, float *out_model_radius,
    char *out_diffuse_texture_path, size_t out_diffuse_texture_path_size);
static int palm_render_load_glb_vertices(const char *relative_asset_path,
                                         PalmVertex **out_vertices,
                                         GLsizei *out_vertex_count,
                                         float *out_model_height,
                                         float *out_model_radius);
static int palm_render_load_model_vertices(
    const char *relative_asset_path, PalmVertex **out_vertices,
    GLsizei *out_vertex_count, float *out_model_height, float *out_model_radius,
    char *out_diffuse_texture_path, size_t out_diffuse_texture_path_size);
static int palm_render_append_category_assets(PalmRenderMesh *mesh,
                                              PalmRenderCategory category);
static int palm_render_create_variant(PalmRenderVariant *variant,
                                      const PalmRenderAssetSpec *asset_spec);
static void palm_render_destroy_variant(PalmRenderVariant *variant);
static int palm_render_reserve_instances(PalmRenderVariant *variant,
                                         size_t required_instance_capacity);
static int
palm_render_reserve_visible_instances(PalmRenderVariant *variant,
                                      size_t required_instance_capacity);
static float palm_render_clamp(float value, float min_value, float max_value);
static float palm_render_mix(float a, float b, float t);
static float
palm_render_get_terrain_step_for_quality(const RendererQualityProfile *quality);
static void palm_render_get_terrain_origin_from_camera(
    const CameraState *camera, const RendererQualityProfile *quality,
    float *out_x, float *out_z);
static float palm_render_hash_unit(int x, int z, unsigned int seed);
static float palm_render_estimate_slope(float x, float z,
                                        const SceneSettings *settings);
static int palm_render_has_category(const PalmRenderMesh *mesh, int category);
static GLsizei
palm_render_get_max_vertex_count_for_category(const PalmRenderMesh *mesh,
                                              int category);
static PalmRenderVariant *palm_render_pick_variant(PalmRenderMesh *mesh,
                                                   int category, int grid_x,
                                                   int grid_z,
                                                   unsigned int seed);
static void palm_render_reset_instances(PalmRenderMesh *mesh);
static int palm_render_upload_instances(PalmRenderMesh *mesh,
                                        const ViewFrustum *frustum);
static int palm_render_float_nearly_equal(float a, float b, float epsilon);
static int
palm_render_instance_intersects_frustum(const PalmRenderVariant *variant,
                                        const PalmInstanceData *instance,
                                        const ViewFrustum *frustum);
static int palm_render_cache_matches(const PalmRenderMesh *mesh, int grid_min_x,
                                     int grid_max_x, int grid_min_z,
                                     int grid_max_z, float radius,
                                     float cell_size,
                                     const SceneSettings *settings);
static int palm_render_populate_palm_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality);
static int palm_render_populate_house_instances(PalmRenderMesh *mesh,
                                                const CameraState *camera,
                                                const SceneSettings *settings,
                                                float house_radius,
                                                float house_cell_size);
static int palm_render_populate_tree_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality);
static int palm_render_populate_grass_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality);
static int palm_render_populate_mountain_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality);
static int palm_render_add_house_instance(PalmRenderMesh *mesh, int grid_x,
                                          int grid_z, float x, float z,
                                          float distance,
                                          const SceneSettings *settings,
                                          int *in_out_near_count,
                                          int *in_out_far_count);
static float
palm_render_sample_lowest_terrain_ring(float x, float z, float radius,
                                       const SceneSettings *settings);
static void palm_render_build_instance_transform(PalmInstanceData *instance,
                                                 float x, float y, float z,
                                                 float scale, float yaw_radians,
                                                 PalmColor tint);

int palm_render_create(PalmRenderMesh *mesh) {
  if (mesh == NULL) {
    return 0;
  }

  memset(mesh, 0, sizeof(*mesh));
  if (!palm_render_append_category_assets(mesh, PALM_RENDER_CATEGORY_PALM) ||
      !palm_render_append_category_assets(mesh, PALM_RENDER_CATEGORY_HOUSE)) {
    palm_render_destroy(mesh);
    palm_render_show_error("Palm Error",
                           "Failed to load palm and house variants.");
    return 0;
  }

  diagnostics_logf("palm_render: category=palm variant_count=%d",
                   mesh->variant_count);
  return 1;
}

int palm_render_create_category(PalmRenderMesh *mesh,
                                PalmRenderCategory category) {
  const char *category_name = "foliage";

  if (mesh == NULL) {
    return 0;
  }

  memset(mesh, 0, sizeof(*mesh));
  (void)palm_render_append_category_assets(mesh, category);

  switch (category) {
  case PALM_RENDER_CATEGORY_PALM:
    category_name = "palm";
    break;
  case PALM_RENDER_CATEGORY_TREE:
    category_name = "tree";
    break;
  case PALM_RENDER_CATEGORY_GRASS:
    category_name = "grass";
    break;
  case PALM_RENDER_CATEGORY_MOUNTAIN:
    category_name = "mountain";
    break;
  case PALM_RENDER_CATEGORY_HOUSE:
    category_name = "house";
    break;
  default:
    break;
  }

  if (mesh->variant_count <= 0) {
    palm_render_destroy(mesh);
    palm_render_show_error(
        "Palm Error",
        "Failed to load any foliage OBJ variants for the requested category.");
    return 0;
  }

  diagnostics_logf("palm_render: category=%s variant_count=%d", category_name,
                   mesh->variant_count);
  return 1;
}

static int palm_render_append_category_assets(PalmRenderMesh *mesh,
                                              PalmRenderCategory category) {
  size_t asset_index = 0U;

  if (mesh == NULL) {
    return 0;
  }

  for (asset_index = 0U; asset_index < k_palm_render_asset_spec_count;
       ++asset_index) {
    const PalmRenderAssetSpec *asset_spec =
        &k_palm_render_asset_specs[asset_index];

    if (asset_spec->category != category) {
      continue;
    }
    if (mesh->variant_count >= PALM_RENDER_MAX_VARIANTS) {
      palm_render_show_error(
          "Palm Error",
          "Configured foliage variants exceed PALM_RENDER_MAX_VARIANTS.");
      return 0;
    }
    if (palm_render_create_variant(&mesh->variants[mesh->variant_count],
                                   asset_spec)) {
      mesh->variant_count += 1;
    } else {
      diagnostics_logf("palm_render: failed to create variant source=%s category=%d",
                       asset_spec->relative_asset_path, asset_spec->category);
    }
  }

  return palm_render_has_category(mesh, category);
}

static int palm_render_create_variant(PalmRenderVariant *variant,
                                      const PalmRenderAssetSpec *asset_spec) {
  PalmVertex *vertices = NULL;
  GLsizei vertex_count = 0;
  float model_height = 1.0f;
  float model_radius = 1.0f;
  char diffuse_texture_path[PLATFORM_PATH_MAX] = {0};
  int column = 0;

  if (variant == NULL || asset_spec == NULL ||
      asset_spec->relative_asset_path == NULL) {
    return 0;
  }

  memset(variant, 0, sizeof(*variant));
  diagnostics_logf("palm_render: loading variant source=%s category=%d",
                   asset_spec->relative_asset_path, asset_spec->category);
  if (!palm_render_load_model_vertices(asset_spec->relative_asset_path,
                                       &vertices, &vertex_count, &model_height,
                                       &model_radius, diffuse_texture_path,
                                       sizeof(diffuse_texture_path))) {
    diagnostics_logf("palm_render: model vertex load failed source=%s",
                     asset_spec->relative_asset_path);
    return 0;
  }

  glGenVertexArrays(1, &variant->vao);
  glGenBuffers(1, &variant->vertex_buffer);
  glGenBuffers(1, &variant->instance_buffer);
  if (variant->vao == 0U || variant->vertex_buffer == 0U ||
      variant->instance_buffer == 0U) {
    free(vertices);
    palm_render_destroy_variant(variant);
    palm_render_show_error("Palm Error",
                           "Failed to allocate palm render buffers.");
    return 0;
  }

  variant->vertex_count = vertex_count;
  variant->model_height = model_height;
  variant->model_radius = model_radius;
  variant->category = asset_spec->category;
  variant->desired_height_min = asset_spec->desired_height_min;
  variant->desired_height_max = asset_spec->desired_height_max;
  variant->scale_jitter_min = asset_spec->scale_jitter_min;
  variant->scale_jitter_max = asset_spec->scale_jitter_max;
  variant->embed_depth_min = asset_spec->embed_depth_min;
  variant->embed_depth_max = asset_spec->embed_depth_max;
  variant->slope_limit = asset_spec->slope_limit;
  if (diffuse_texture_path[0] != '\0') {
    if (!palm_render_create_texture_from_file(diffuse_texture_path,
                                              &variant->diffuse_texture)) {
      diagnostics_logf("palm_render: failed to load diffuse texture '%s' for "
                       "%s, using solid fallback",
                       diffuse_texture_path, asset_spec->relative_asset_path);
    } else {
      diagnostics_logf("palm_render: using diffuse texture '%s' for %s",
                       diffuse_texture_path, asset_spec->relative_asset_path);
    }
  }
  if (variant->diffuse_texture == 0U &&
      !palm_render_create_solid_texture((PalmColor){1.0f, 1.0f, 1.0f},
                                        &variant->diffuse_texture)) {
    free(vertices);
    palm_render_destroy_variant(variant);
    palm_render_show_error("Palm Error",
                           "Failed to allocate palm fallback texture.");
    return 0;
  }

  glBindVertexArray(variant->vao);

  glBindBuffer(GL_ARRAY_BUFFER, variant->vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(PalmVertex) * (size_t)variant->vertex_count, vertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex),
                        (const void *)offsetof(PalmVertex, position));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex),
                        (const void *)offsetof(PalmVertex, normal));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PalmVertex),
                        (const void *)offsetof(PalmVertex, color));
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, sizeof(PalmVertex),
                        (const void *)offsetof(PalmVertex, texcoord));

  glBindBuffer(GL_ARRAY_BUFFER, variant->instance_buffer);
  glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_DYNAMIC_DRAW);
  for (column = 0; column < 4; ++column) {
    const GLuint attribute = (GLuint)(3 + column);
    const size_t offset = offsetof(PalmInstanceData, transform) +
                          sizeof(float) * 4U * (size_t)column;
    glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE,
                          sizeof(PalmInstanceData), (const void *)offset);
    glVertexAttribDivisor(attribute, 1);
  }
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(PalmInstanceData),
                        (const void *)offsetof(PalmInstanceData, tint));
  glVertexAttribDivisor(7, 1);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);

  free(vertices);
  return 1;
}

static void palm_render_destroy_variant(PalmRenderVariant *variant) {
  if (variant == NULL) {
    return;
  }

  if (variant->instance_buffer != 0U) {
    glDeleteBuffers(1, &variant->instance_buffer);
    variant->instance_buffer = 0U;
  }
  if (variant->vertex_buffer != 0U) {
    glDeleteBuffers(1, &variant->vertex_buffer);
    variant->vertex_buffer = 0U;
  }
  if (variant->diffuse_texture != 0U) {
    glDeleteTextures(1, &variant->diffuse_texture);
    variant->diffuse_texture = 0U;
  }
  if (variant->vao != 0U) {
    glDeleteVertexArrays(1, &variant->vao);
    variant->vao = 0U;
  }
  if (variant->cpu_instances != NULL) {
    free(variant->cpu_instances);
    variant->cpu_instances = NULL;
  }
  if (variant->cpu_visible_instances != NULL) {
    free(variant->cpu_visible_instances);
    variant->cpu_visible_instances = NULL;
  }
  variant->cpu_instance_capacity = 0U;
  variant->cpu_visible_instance_capacity = 0U;
  variant->vertex_count = 0;
  variant->instance_count = 0;
  variant->visible_instance_count = 0;
  variant->model_height = 0.0f;
  variant->model_radius = 0.0f;
  variant->category = PALM_RENDER_CATEGORY_PALM;
  variant->desired_height_min = 0.0f;
  variant->desired_height_max = 0.0f;
  variant->scale_jitter_min = 0.0f;
  variant->scale_jitter_max = 0.0f;
  variant->embed_depth_min = 0.0f;
  variant->embed_depth_max = 0.0f;
  variant->slope_limit = 0.0f;
}

void palm_render_destroy(PalmRenderMesh *mesh) {
  int variant_index = 0;

  if (mesh == NULL) {
    return;
  }

  for (variant_index = 0; variant_index < PALM_RENDER_MAX_VARIANTS;
       ++variant_index) {
    palm_render_destroy_variant(&mesh->variants[variant_index]);
  }
  mesh->variant_count = 0;
  mesh->cache_valid = 0;
}

int palm_render_update_category(PalmRenderMesh *mesh,
                                PalmRenderCategory category,
                                const CameraState *camera,
                                const SceneSettings *settings,
                                const RendererQualityProfile *quality) {
  return palm_render_update_category_with_frustum(mesh, category, camera,
                                                  settings, quality, NULL);
}

int palm_render_update_category_with_frustum(
    PalmRenderMesh *mesh, PalmRenderCategory category,
    const CameraState *camera, const SceneSettings *settings,
    const RendererQualityProfile *quality, const ViewFrustum *frustum) {
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings *active_settings =
      (settings != NULL) ? settings : &fallback_settings;
  int populate_result = 1;
  int variant_index = 0;
  int had_instances = 0;

  if (mesh == NULL) {
    return 0;
  }

  if (camera == NULL || mesh->variant_count <= 0 ||
      !palm_render_has_category(mesh, category) ||
      (category != PALM_RENDER_CATEGORY_MOUNTAIN &&
       active_settings->palm_size <= 0.01f)) {
    for (variant_index = 0; variant_index < mesh->variant_count;
         ++variant_index) {
      if (mesh->variants[variant_index].instance_count > 0 ||
          mesh->variants[variant_index].visible_instance_count > 0) {
        had_instances = 1;
      }
      mesh->variants[variant_index].instance_count = 0;
      mesh->variants[variant_index].visible_instance_count = 0;
    }
    mesh->cache_valid = 0;
    return had_instances ? palm_render_upload_instances(mesh, frustum) : 1;
  }

  switch (category) {
  case PALM_RENDER_CATEGORY_PALM:
    populate_result = palm_render_populate_palm_instances(
        mesh, camera, active_settings, quality);
    break;
  case PALM_RENDER_CATEGORY_TREE:
    populate_result = palm_render_populate_tree_instances(
        mesh, camera, active_settings, quality);
    break;
  case PALM_RENDER_CATEGORY_GRASS:
    populate_result = palm_render_populate_grass_instances(
        mesh, camera, active_settings, quality);
    break;
  case PALM_RENDER_CATEGORY_MOUNTAIN:
    populate_result = palm_render_populate_mountain_instances(
        mesh, camera, active_settings, quality);
    break;
  default:
    return 0;
  }

  if (populate_result == 0) {
    return 0;
  }
  if (populate_result == 2) {
    return (frustum != NULL) ? palm_render_upload_instances(mesh, frustum) : 1;
  }

  return palm_render_upload_instances(mesh, frustum);
}

int palm_render_update(PalmRenderMesh *mesh, const CameraState *camera,
                       const SceneSettings *settings,
                       const RendererQualityProfile *quality) {
  return palm_render_update_category(mesh, PALM_RENDER_CATEGORY_PALM, camera,
                                     settings, quality);
}

void palm_render_draw(const PalmRenderMesh *mesh) {
  int variant_index = 0;

  if (mesh == NULL || mesh->variant_count <= 0) {
    return;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    const PalmRenderVariant *variant = &mesh->variants[variant_index];

    if (variant->vao == 0U || variant->vertex_count <= 0 ||
        variant->visible_instance_count <= 0) {
      continue;
    }

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, variant->diffuse_texture);
    glBindVertexArray(variant->vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, variant->vertex_count,
                          variant->visible_instance_count);
  }
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
}

static void palm_render_show_error(const char *title, const char *message) {
  diagnostics_logf("%s: %s", title, message);
  platform_support_show_error_dialog(title, message);
}

static int palm_render_is_space(char value) {
  return value == ' ' || value == '\t';
}

static const char *palm_render_find_last_path_separator(const char *path) {
  const char *last_backslash = NULL;
  const char *last_slash = NULL;

  if (path == NULL) {
    return NULL;
  }

  last_backslash = strrchr(path, '\\');
  last_slash = strrchr(path, '/');
  if (last_backslash == NULL) {
    return last_slash;
  }
  if (last_slash == NULL) {
    return last_backslash;
  }

  return (last_backslash > last_slash) ? last_backslash : last_slash;
}

static const char *palm_render_get_basename(const char *path) {
  const char *separator = palm_render_find_last_path_separator(path);
  return (separator != NULL) ? (separator + 1) : path;
}

static int palm_render_file_exists(const char *path) {
  return platform_support_file_exists(path);
}

static int palm_render_build_relative_path(const char *base_path,
                                           const char *relative_path,
                                           char *out_path,
                                           size_t out_path_size) {
  const char *last_separator = NULL;
  size_t directory_length = 0U;

  if (base_path == NULL || relative_path == NULL || out_path == NULL ||
      out_path_size == 0U) {
    return 0;
  }

  last_separator = palm_render_find_last_path_separator(base_path);
  if (last_separator == NULL) {
    return 0;
  }

  directory_length = (size_t)(last_separator - base_path + 1);
  if (directory_length + strlen(relative_path) + 1U > out_path_size) {
    return 0;
  }

  memcpy(out_path, base_path, directory_length);
  (void)snprintf(out_path + directory_length, out_path_size - directory_length,
                 "%s", relative_path);
  return 1;
}

static int palm_render_resolve_material_asset_path(const char *mtl_path,
                                                   const char *relative_path,
                                                   char *out_path,
                                                   size_t out_path_size) {
  char candidate_path[PLATFORM_PATH_MAX] = {0};
  char fallback_relative[PLATFORM_PATH_MAX] = {0};
  const char *basename = NULL;

  if (mtl_path == NULL || relative_path == NULL || out_path == NULL ||
      out_path_size == 0U) {
    return 0;
  }

  if (palm_render_build_relative_path(mtl_path, relative_path, candidate_path,
                                      sizeof(candidate_path)) &&
      palm_render_file_exists(candidate_path)) {
    (void)snprintf(out_path, out_path_size, "%s", candidate_path);
    return 1;
  }

  basename = palm_render_get_basename(relative_path);
  if (basename == NULL || basename[0] == '\0') {
    return 0;
  }

  (void)snprintf(fallback_relative, sizeof(fallback_relative), "material/%s",
                 basename);
  if (palm_render_build_relative_path(mtl_path, fallback_relative,
                                      candidate_path, sizeof(candidate_path)) &&
      palm_render_file_exists(candidate_path)) {
    (void)snprintf(out_path, out_path_size, "%s", candidate_path);
    return 1;
  }

  return 0;
}

static int palm_render_resolve_asset_path(const char *relative_path,
                                          char *out_path,
                                          size_t out_path_size) {
  char module_path[PLATFORM_PATH_MAX] = {0};
  char candidate_path[PLATFORM_PATH_MAX] = {0};
  char current_directory[PLATFORM_PATH_MAX] = {0};
  char *last_separator = NULL;
  size_t base_length = 0U;
  static const char *k_res_prefix = "res/";
  static const char *k_res_fallbacks[] = {"res/", "../res/", "../../res/",
                                          "../../../res/"};
  size_t i = 0U;

  if (!platform_support_get_executable_path(module_path, sizeof(module_path))) {
    palm_render_show_error(
        "Path Error",
        "Failed to resolve the executable directory for foliage assets.");
    return 0;
  }

  last_separator = (char *)palm_render_find_last_path_separator(module_path);
  if (last_separator == NULL) {
    palm_render_show_error("Path Error",
                           "Failed to resolve the executable directory "
                           "separator for foliage assets.");
    return 0;
  }

  last_separator[1] = '\0';
  if (strlen(module_path) + strlen(relative_path) + 1U <=
      sizeof(candidate_path)) {
    (void)snprintf(candidate_path, sizeof(candidate_path), "%s%s", module_path,
                   relative_path);
    if (palm_render_file_exists(candidate_path)) {
      (void)snprintf(out_path, out_path_size, "%s", candidate_path);
      return 1;
    }
  }

  if (platform_support_get_current_directory(current_directory,
                                             sizeof(current_directory))) {
    base_length = strlen(current_directory);
    if (base_length + 1U + strlen(relative_path) + 1U <=
        sizeof(candidate_path)) {
      (void)snprintf(candidate_path, sizeof(candidate_path), "%s/%s",
                     current_directory, relative_path);
      if (palm_render_file_exists(candidate_path)) {
        (void)snprintf(out_path, out_path_size, "%s", candidate_path);
        return 1;
      }
    }
  }

  if (strncmp(relative_path, k_res_prefix, strlen(k_res_prefix)) == 0) {
    const char *suffix = relative_path + strlen(k_res_prefix);
    for (i = 0U; i < sizeof(k_res_fallbacks) / sizeof(k_res_fallbacks[0]);
         ++i) {
      char fallback_relative[PLATFORM_PATH_MAX] = {0};

      if (strlen(k_res_fallbacks[i]) + strlen(suffix) + 1U >
          sizeof(fallback_relative)) {
        continue;
      }

      (void)snprintf(fallback_relative, sizeof(fallback_relative), "%s%s",
                     k_res_fallbacks[i], suffix);
      if (palm_render_build_relative_path(module_path, fallback_relative,
                                          candidate_path,
                                          sizeof(candidate_path)) &&
          palm_render_file_exists(candidate_path)) {
        (void)snprintf(out_path, out_path_size, "%s", candidate_path);
        return 1;
      }
    }
  }

  {
    char message[512] = {0};
    (void)snprintf(message, sizeof(message),
                   "Failed to resolve foliage asset path for '%s'. Check the "
                   "res folder next to the executable or in the project root.",
                   relative_path);
    palm_render_show_error("Path Error", message);
  }
  return 0;
}

static int palm_render_load_text_file(const char *path, const char *label,
                                      char **out_text) {
  char message[256] = {0};
  FILE *file = NULL;
  long file_size = 0L;
  size_t bytes_read = 0U;
  char *text = NULL;

#if defined(_MSC_VER)
  if (fopen_s(&file, path, "rb") != 0) {
    file = NULL;
  }
#else
  file = fopen(path, "rb");
#endif

  if (file == NULL) {
    (void)snprintf(message, sizeof(message), "Failed to open %s file:\n%s",
                   label, path);
    palm_render_show_error("File Error", message);
    return 0;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    (void)snprintf(message, sizeof(message), "Failed to seek %s file.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  file_size = ftell(file);
  if (file_size < 0L || fseek(file, 0L, SEEK_SET) != 0) {
    (void)snprintf(message, sizeof(message), "%s file is unreadable.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  text = (char *)malloc((size_t)file_size + 1U);
  if (text == NULL) {
    palm_render_show_error("Memory Error",
                           "Failed to allocate memory for palm asset loading.");
    fclose(file);
    return 0;
  }

  bytes_read = fread(text, 1U, (size_t)file_size, file);
  fclose(file);
  if (bytes_read != (size_t)file_size) {
    free(text);
    (void)snprintf(message, sizeof(message), "Failed to read %s file.", label);
    palm_render_show_error("File Error", message);
    return 0;
  }

  text[file_size] = '\0';
  *out_text = text;
  return 1;
}

static int palm_render_load_binary_file(const char *path, const char *label,
                                        unsigned char **out_data,
                                        size_t *out_size) {
  char message[256] = {0};
  FILE *file = NULL;
  long file_size = 0L;
  size_t bytes_read = 0U;
  unsigned char *data = NULL;

  if (out_data == NULL || out_size == NULL) {
    return 0;
  }

  *out_data = NULL;
  *out_size = 0U;

#if defined(_MSC_VER)
  if (fopen_s(&file, path, "rb") != 0) {
    file = NULL;
  }
#else
  file = fopen(path, "rb");
#endif

  if (file == NULL) {
    (void)snprintf(message, sizeof(message), "Failed to open %s file:\n%s",
                   label, path);
    palm_render_show_error("File Error", message);
    return 0;
  }

  if (fseek(file, 0L, SEEK_END) != 0) {
    (void)snprintf(message, sizeof(message), "Failed to seek %s file.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  file_size = ftell(file);
  if (file_size <= 0L || fseek(file, 0L, SEEK_SET) != 0) {
    (void)snprintf(message, sizeof(message), "%s file is unreadable.", label);
    palm_render_show_error("File Error", message);
    fclose(file);
    return 0;
  }

  data = (unsigned char *)malloc((size_t)file_size);
  if (data == NULL) {
    palm_render_show_error(
        "Memory Error",
        "Failed to allocate memory for binary palm asset loading.");
    fclose(file);
    return 0;
  }

  bytes_read = fread(data, 1U, (size_t)file_size, file);
  fclose(file);
  if (bytes_read != (size_t)file_size) {
    free(data);
    (void)snprintf(message, sizeof(message), "Failed to read %s file.", label);
    palm_render_show_error("File Error", message);
    return 0;
  }

  *out_data = data;
  *out_size = (size_t)file_size;
  return 1;
}

static char *palm_render_trim_left(char *text) {
  while (text != NULL && palm_render_is_space(*text)) {
    ++text;
  }
  return text;
}

static void palm_render_trim_right_in_place(char *text) {
  size_t length = 0U;

  if (text == NULL) {
    return;
  }

  length = strlen(text);
  while (length > 0U && palm_render_is_space(text[length - 1U])) {
    text[length - 1U] = '\0';
    length -= 1U;
  }
}

static const char *palm_render_skip_spaces_const(const char *text) {
  while (text != NULL && palm_render_is_space(*text)) {
    ++text;
  }
  return text;
}

static const char *palm_render_match_keyword(const char *text,
                                             const char *keyword) {
  const size_t keyword_length = strlen(keyword);

  if (text == NULL || keyword == NULL) {
    return NULL;
  }
  if (strncmp(text, keyword, keyword_length) != 0) {
    return NULL;
  }
  if (text[keyword_length] != '\0' &&
      !palm_render_is_space(text[keyword_length])) {
    return NULL;
  }

  return palm_render_skip_spaces_const(text + keyword_length);
}

static int palm_render_path_uses_black_key(const char *path) {
  char lowered[PLATFORM_PATH_MAX] = {0};
  size_t index = 0U;
  size_t path_length = 0U;

  if (path == NULL) {
    return 0;
  }

  path_length = strlen(path);
  if (path_length >= sizeof(lowered)) {
    path_length = sizeof(lowered) - 1U;
  }

  for (index = 0U; index < path_length; ++index) {
    lowered[index] = (char)tolower((unsigned char)path[index]);
  }
  lowered[path_length] = '\0';

  return strstr(lowered, "cut") != NULL;
}

static int palm_render_estimate_texture_color(const char *texture_path,
                                              PalmColor *out_color) {
  unsigned char *pixels = NULL;
  int width = 0;
  int height = 0;
  int source_channels = 0;
  unsigned long long sum_r = 0ULL;
  unsigned long long sum_g = 0ULL;
  unsigned long long sum_b = 0ULL;
  unsigned long long sample_count = 0ULL;
  int texel_index = 0;

  if (texture_path == NULL || out_color == NULL) {
    return 0;
  }

  pixels = stbi_load(texture_path, &width, &height, &source_channels, 4);
  if (pixels == NULL || width <= 0 || height <= 0) {
    if (pixels != NULL) {
      stbi_image_free(pixels);
    }
    return 0;
  }

  for (texel_index = 0; texel_index < width * height; ++texel_index) {
    const unsigned char *rgba = &pixels[texel_index * 4];
    const unsigned char max_channel =
        (rgba[0] > rgba[1]) ? ((rgba[0] > rgba[2]) ? rgba[0] : rgba[2])
                            : ((rgba[1] > rgba[2]) ? rgba[1] : rgba[2]);
    const int has_alpha = source_channels >= 4;

    if ((has_alpha && rgba[3] <= 16U) ||
        (palm_render_path_uses_black_key(texture_path) && max_channel <= 12U)) {
      continue;
    }

    sum_r += rgba[0];
    sum_g += rgba[1];
    sum_b += rgba[2];
    sample_count += 1ULL;
  }

  stbi_image_free(pixels);
  if (sample_count == 0ULL) {
    return 0;
  }

  out_color->r = (float)sum_r / (255.0f * (float)sample_count);
  out_color->g = (float)sum_g / (255.0f * (float)sample_count);
  out_color->b = (float)sum_b / (255.0f * (float)sample_count);
  return 1;
}

static int palm_render_create_texture_from_file(const char *texture_path,
                                                GLuint *out_texture) {
  unsigned char *pixels = NULL;
  int width = 0;
  int height = 0;
  int source_channels = 0;
  GLuint texture = 0U;

  if (texture_path == NULL || out_texture == NULL) {
    return 0;
  }

  *out_texture = 0U;
  pixels = stbi_load(texture_path, &width, &height, &source_channels, 4);
  if (pixels == NULL || width <= 0 || height <= 0) {
    if (pixels != NULL) {
      stbi_image_free(pixels);
    }
    return 0;
  }

  glGenTextures(1, &texture);
  if (texture == 0U) {
    stbi_image_free(pixels);
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(pixels);
  *out_texture = texture;
  return 1;
}

static int palm_render_create_solid_texture(PalmColor color,
                                            GLuint *out_texture) {
  const unsigned char rgba[4] = {
      (unsigned char)(palm_render_clamp(color.r, 0.0f, 1.0f) * 255.0f + 0.5f),
      (unsigned char)(palm_render_clamp(color.g, 0.0f, 1.0f) * 255.0f + 0.5f),
      (unsigned char)(palm_render_clamp(color.b, 0.0f, 1.0f) * 255.0f + 0.5f),
      255U};
  GLuint texture = 0U;

  if (out_texture == NULL) {
    return 0;
  }

  *out_texture = 0U;
  glGenTextures(1, &texture);
  if (texture == 0U) {
    return 0;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               rgba);
  glBindTexture(GL_TEXTURE_2D, 0);

  *out_texture = texture;
  return 1;
}

static int
palm_render_material_needs_texture_color(const PalmMaterial *material) {
  float min_channel = 0.0f;
  float max_channel = 0.0f;

  if (material == NULL || material->has_diffuse == 0) {
    return 1;
  }

  min_channel = material->diffuse.r;
  if (material->diffuse.g < min_channel) {
    min_channel = material->diffuse.g;
  }
  if (material->diffuse.b < min_channel) {
    min_channel = material->diffuse.b;
  }

  max_channel = material->diffuse.r;
  if (material->diffuse.g > max_channel) {
    max_channel = material->diffuse.g;
  }
  if (material->diffuse.b > max_channel) {
    max_channel = material->diffuse.b;
  }

  return (min_channel >= 0.94f && max_channel <= 1.01f) ||
         (max_channel - min_channel <= 0.03f && max_channel >= 0.90f);
}

static int palm_render_reserve_memory(void **buffer, size_t *capacity,
                                      size_t required, size_t element_size) {
  void *new_buffer = NULL;
  size_t new_capacity = 0U;

  if (buffer == NULL || capacity == NULL || element_size == 0U) {
    return 0;
  }
  if (required <= *capacity) {
    return 1;
  }

  new_capacity = (*capacity > 0U) ? *capacity : 256U;
  while (new_capacity < required) {
    if (new_capacity > (SIZE_MAX / 2U)) {
      return 0;
    }
    new_capacity *= 2U;
  }
  if (new_capacity > (SIZE_MAX / element_size)) {
    return 0;
  }

  new_buffer = realloc(*buffer, new_capacity * element_size);
  if (new_buffer == NULL) {
    return 0;
  }

  *buffer = new_buffer;
  *capacity = new_capacity;
  return 1;
}

static int palm_render_push_vec3(PalmVec3Array *array, PalmVec3 value) {
  if (array == NULL ||
      !palm_render_reserve_memory((void **)&array->data, &array->capacity,
                                  array->count + 1U, sizeof(PalmVec3))) {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static int palm_render_push_vec2(PalmVec2Array *array, PalmVec2 value) {
  if (array == NULL ||
      !palm_render_reserve_memory((void **)&array->data, &array->capacity,
                                  array->count + 1U, sizeof(PalmVec2))) {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static int palm_render_push_vertex(PalmVertexArray *array, PalmVertex value) {
  if (array == NULL ||
      !palm_render_reserve_memory((void **)&array->data, &array->capacity,
                                  array->count + 1U, sizeof(PalmVertex))) {
    return 0;
  }

  array->data[array->count] = value;
  array->count += 1U;
  return 1;
}

static PalmMaterial *palm_render_push_material(PalmMaterialArray *array,
                                               const char *name) {
  PalmMaterial *material = NULL;

  if (array == NULL || name == NULL ||
      !palm_render_reserve_memory((void **)&array->data, &array->capacity,
                                  array->count + 1U, sizeof(PalmMaterial))) {
    return NULL;
  }

  material = &array->data[array->count];
  memset(material, 0, sizeof(*material));
  (void)snprintf(material->name, sizeof(material->name), "%s", name);
  material->diffuse = (PalmColor){0.70f, 0.70f, 0.70f};
  material->has_diffuse = 0;
  material->texture_path[0] = '\0';
  material->has_texture = 0;
  array->count += 1U;
  return material;
}

static PalmMaterial *
palm_render_find_material_mutable(PalmMaterialArray *materials,
                                  const char *name) {
  size_t material_index = 0U;

  if (materials == NULL || name == NULL) {
    return NULL;
  }

  for (material_index = 0U; material_index < materials->count;
       ++material_index) {
    if (strcmp(materials->data[material_index].name, name) == 0) {
      return &materials->data[material_index];
    }
  }

  return NULL;
}

static PalmMaterial *
palm_render_get_or_create_material(PalmMaterialArray *materials,
                                   const char *name) {
  PalmMaterial *material = NULL;

  if (materials == NULL || name == NULL || name[0] == '\0') {
    return NULL;
  }

  material = palm_render_find_material_mutable(materials, name);
  if (material != NULL) {
    return material;
  }

  return palm_render_push_material(materials, name);
}

static int palm_render_parse_mtl(const char *mtl_path,
                                 PalmMaterialArray *materials) {
  char *source = NULL;
  char *cursor = NULL;
  PalmMaterial *current_material = NULL;

  if (materials == NULL) {
    return 0;
  }
  if (!palm_render_load_text_file(mtl_path, "MTL", &source)) {
    return 0;
  }

  cursor = source;
  while (*cursor != '\0') {
    char *line_start = cursor;
    char *line_end = cursor;
    char *trimmed = NULL;
    const char *argument = NULL;

    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
      ++line_end;
    }
    if (*line_end == '\r') {
      *line_end = '\0';
      cursor = line_end + 1;
      if (*cursor == '\n') {
        *cursor = '\0';
        cursor += 1;
      }
    } else if (*line_end == '\n') {
      *line_end = '\0';
      cursor = line_end + 1;
    } else {
      cursor = line_end;
    }

    trimmed = palm_render_trim_left(line_start);
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
      continue;
    }

    argument = palm_render_match_keyword(trimmed, "newmtl");
    if (argument != NULL) {
      char material_name[64] = {0};

      (void)snprintf(material_name, sizeof(material_name), "%s", argument);
      palm_render_trim_right_in_place(material_name);
      current_material =
          palm_render_get_or_create_material(materials, material_name);
      if (current_material == NULL) {
        free(source);
        palm_render_show_error("Memory Error",
                               "Failed to store palm material data.");
        return 0;
      }
    } else if ((argument = palm_render_match_keyword(trimmed, "Kd")) != NULL &&
               current_material != NULL) {
      float r = 0.70f;
      float g = 0.70f;
      float b = 0.70f;

      if (palm_render_sscanf(argument, "%f %f %f", &r, &g, &b) == 3) {
        current_material->diffuse = (PalmColor){r, g, b};
        current_material->has_diffuse = 1;
      }
    } else if ((argument = palm_render_match_keyword(trimmed, "map_Kd")) !=
                   NULL &&
               current_material != NULL) {
      char relative_name[PLATFORM_PATH_MAX] = {0};
      char texture_path[PLATFORM_PATH_MAX] = {0};
      PalmColor texture_color = {0.0f, 0.0f, 0.0f};

      (void)snprintf(relative_name, sizeof(relative_name), "%s", argument);
      palm_render_trim_right_in_place(relative_name);
      if (palm_render_resolve_material_asset_path(
              mtl_path, relative_name, texture_path, sizeof(texture_path))) {
        (void)snprintf(current_material->texture_path,
                       sizeof(current_material->texture_path), "%s",
                       texture_path);
        current_material->has_texture = 1;
        if (palm_render_material_needs_texture_color(current_material) &&
            palm_render_estimate_texture_color(texture_path, &texture_color)) {
          current_material->diffuse = texture_color;
          current_material->has_diffuse = 1;
        }
      }
    }
  }

  free(source);
  return 1;
}

static const PalmMaterial *
palm_render_find_material(const PalmMaterialArray *materials,
                          const char *name) {
  size_t i = 0U;

  if (materials == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0U; i < materials->count; ++i) {
    if (strcmp(materials->data[i].name, name) == 0) {
      return &materials->data[i];
    }
  }

  return NULL;
}

static int palm_render_parse_face_vertex(const char *token,
                                         PalmObjIndex *out_index) {
  const char *cursor = token;
  char *end = NULL;
  long value = 0L;

  if (token == NULL || out_index == NULL) {
    return 0;
  }

  memset(out_index, 0, sizeof(*out_index));
  value = strtol(cursor, &end, 10);
  if (end == cursor) {
    return 0;
  }
  out_index->position_index = (int)value;

  if (*end == '/') {
    cursor = end + 1;
    if (*cursor != '/') {
      value = strtol(cursor, &end, 10);
      if (end != cursor) {
        out_index->texcoord_index = (int)value;
      }
    } else {
      end = (char *)cursor;
    }

    if (*end == '/') {
      cursor = end + 1;
      value = strtol(cursor, &end, 10);
      if (end != cursor) {
        out_index->normal_index = (int)value;
      }
    }
  }

  return 1;
}

static int palm_render_resolve_obj_index(int obj_index, size_t count) {
  int resolved = -1;

  if (obj_index > 0) {
    resolved = obj_index - 1;
  } else if (obj_index < 0) {
    resolved = (int)count + obj_index;
  }

  if (resolved < 0 || resolved >= (int)count) {
    return -1;
  }

  return resolved;
}

static PalmVec3 palm_render_vec3_subtract(PalmVec3 a, PalmVec3 b) {
  PalmVec3 result = {a.x - b.x, a.y - b.y, a.z - b.z};
  return result;
}

static PalmVec3 palm_render_vec3_cross(PalmVec3 a, PalmVec3 b) {
  PalmVec3 result = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                     a.x * b.y - a.y * b.x};
  return result;
}

static float palm_render_vec3_dot(PalmVec3 a, PalmVec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static PalmVec3 palm_render_vec3_normalize(PalmVec3 value) {
  const float length = sqrtf(palm_render_vec3_dot(value, value));
  if (length <= 0.00001f) {
    PalmVec3 fallback = {0.0f, 1.0f, 0.0f};
    return fallback;
  }

  {
    const float inverse_length = 1.0f / length;
    PalmVec3 result = {value.x * inverse_length, value.y * inverse_length,
                       value.z * inverse_length};
    return result;
  }
}

static void palm_render_matrix_identity(float *out_matrix) {
  size_t i = 0U;

  if (out_matrix == NULL) {
    return;
  }

  for (i = 0U; i < 16U; ++i) {
    out_matrix[i] = 0.0f;
  }
  out_matrix[0] = 1.0f;
  out_matrix[5] = 1.0f;
  out_matrix[10] = 1.0f;
  out_matrix[15] = 1.0f;
}

static void palm_render_matrix_multiply(float *out_matrix, const float *left,
                                        const float *right) {
  float result[16] = {0};
  int column = 0;
  int row = 0;
  int inner = 0;

  if (out_matrix == NULL || left == NULL || right == NULL) {
    return;
  }

  for (column = 0; column < 4; ++column) {
    for (row = 0; row < 4; ++row) {
      float value = 0.0f;
      for (inner = 0; inner < 4; ++inner) {
        value += left[inner * 4 + row] * right[column * 4 + inner];
      }
      result[column * 4 + row] = value;
    }
  }

  memcpy(out_matrix, result, sizeof(result));
}

static void palm_render_matrix_from_trs(float *out_matrix,
                                        const PalmVec3 *translation,
                                        const float *rotation,
                                        const PalmVec3 *scale) {
  const PalmVec3 t =
      (translation != NULL) ? *translation : (PalmVec3){0.0f, 0.0f, 0.0f};
  const PalmVec3 s = (scale != NULL) ? *scale : (PalmVec3){1.0f, 1.0f, 1.0f};
  const float qx = (rotation != NULL) ? rotation[0] : 0.0f;
  const float qy = (rotation != NULL) ? rotation[1] : 0.0f;
  const float qz = (rotation != NULL) ? rotation[2] : 0.0f;
  const float qw = (rotation != NULL) ? rotation[3] : 1.0f;
  const float xx = qx * qx;
  const float yy = qy * qy;
  const float zz = qz * qz;
  const float xy = qx * qy;
  const float xz = qx * qz;
  const float yz = qy * qz;
  const float wx = qw * qx;
  const float wy = qw * qy;
  const float wz = qw * qz;

  if (out_matrix == NULL) {
    return;
  }

  palm_render_matrix_identity(out_matrix);
  out_matrix[0] = (1.0f - 2.0f * (yy + zz)) * s.x;
  out_matrix[1] = (2.0f * (xy + wz)) * s.x;
  out_matrix[2] = (2.0f * (xz - wy)) * s.x;
  out_matrix[4] = (2.0f * (xy - wz)) * s.y;
  out_matrix[5] = (1.0f - 2.0f * (xx + zz)) * s.y;
  out_matrix[6] = (2.0f * (yz + wx)) * s.y;
  out_matrix[8] = (2.0f * (xz + wy)) * s.z;
  out_matrix[9] = (2.0f * (yz - wx)) * s.z;
  out_matrix[10] = (1.0f - 2.0f * (xx + yy)) * s.z;
  out_matrix[12] = t.x;
  out_matrix[13] = t.y;
  out_matrix[14] = t.z;
}

static PalmVec3 palm_render_transform_point(const float *matrix,
                                            PalmVec3 value) {
  PalmVec3 result = value;

  if (matrix == NULL) {
    return result;
  }

  result.x = matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z +
             matrix[12];
  result.y = matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z +
             matrix[13];
  result.z = matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z +
             matrix[14];
  return result;
}

static PalmVec3 palm_render_transform_direction(const float *matrix,
                                                PalmVec3 value) {
  PalmVec3 result = value;

  if (matrix == NULL) {
    return result;
  }

  result.x = matrix[0] * value.x + matrix[4] * value.y + matrix[8] * value.z;
  result.y = matrix[1] * value.x + matrix[5] * value.y + matrix[9] * value.z;
  result.z = matrix[2] * value.x + matrix[6] * value.y + matrix[10] * value.z;
  return palm_render_vec3_normalize(result);
}

static int palm_render_json_parse(const char *json, size_t length,
                                  PalmJsonToken **out_tokens,
                                  size_t *out_token_count) {
  struct json_parser p = {0};
  size_t cap = 512;

  if (json == NULL || out_tokens == NULL || out_token_count == NULL ||
      length == 0U || length > (size_t)INT_MAX) {
    diagnostics_logf("palm_render: invalid JSON parse request length=%zu",
                     length);
    return 0;
  }

  *out_tokens = NULL;
  *out_token_count = 0U;

  p.toks = (PalmJsonToken *)malloc(cap * sizeof(struct json_token));
  if (p.toks == NULL) {
    diagnostics_logf("palm_render: failed to allocate initial JSON token buffer "
                     "capacity=%zu",
                     cap);
    return 0;
  }

  p.cap = (int)cap;
  while (json_load(&p, json, (int)length)) {
    PalmJsonToken *resized_tokens = NULL;

    if (p.err != JSON_OUT_OF_TOKEN) {
      break;
    }

    cap = (size_t)p.cap;
    resized_tokens =
        (PalmJsonToken *)realloc(p.toks, cap * sizeof(struct json_token));
    if (resized_tokens == NULL) {
      diagnostics_logf("palm_render: failed to grow JSON token buffer "
                       "capacity=%zu",
                       cap);
      free(p.toks);
      return 0;
    }

    p.toks = resized_tokens;
    p.cap = (int)cap;
  }

  if (p.err != JSON_OK || p.cnt <= 0 || p.toks == NULL) {
    diagnostics_logf(
        "palm_render: JSON parse failed status=%d token_count=%d capacity=%d "
        "first_type=%d length=%zu",
        (int)p.err, p.cnt, p.cap,
        (p.toks != NULL && p.cnt > 0) ? (int)p.toks[0].type : -1, length);
    free(p.toks);
    return 0;
  }

  *out_tokens = p.toks;
  *out_token_count = (size_t)p.cnt;
  diagnostics_logf("palm_render: JSON parse ok token_count=%d length=%zu",
                   p.cnt, length);
  return 1;
}

static int palm_render_json_root_object_get(const PalmJsonToken *tokens,
                                            size_t token_count,
                                            const char *key) {
  struct json_token *key_tok = NULL;

  if (tokens == NULL || token_count == 0U || key == NULL) {
    return -1;
  }

  key_tok = json_query((struct json_token *)tokens, (int)token_count, key);
  if (key_tok != NULL && key_tok->type != JSON_NONE) {
    return (int)(key_tok - tokens);
  }

  return -1;
}

static int palm_render_json_token_equals(const char *json,
                                         const PalmJsonToken *token,
                                         const char *expected) {
  (void)json;
  return json_cmp(token, expected) == 0;
}

static int palm_render_json_token_to_int(const char *json,
                                         const PalmJsonToken *token,
                                         int *out_value) {
  json_number num;
  (void)json;
  if (json_convert(&num, token) == JSON_NUMBER) {
    *out_value = (int)num;
    return 1;
  }
  return 0;
}

static int palm_render_json_token_to_size(const char *json,
                                          const PalmJsonToken *token,
                                          size_t *out_value) {
  json_number num;
  (void)json;
  if (json_convert(&num, token) == JSON_NUMBER) {
    *out_value = (size_t)num;
    return 1;
  }
  return 0;
}

static int palm_render_json_token_to_float(const char *json,
                                           const PalmJsonToken *token,
                                           float *out_value) {
  json_number num;
  (void)json;
  if (json_convert(&num, token) == JSON_NUMBER) {
    *out_value = (float)num;
    return 1;
  }
  return 0;
}

static int palm_render_json_object_get(const char *json,
                                       const PalmJsonToken *tokens,
                                       size_t token_count, size_t object_index,
                                       const char *key) {
  struct json_token *key_tok = NULL;
  (void)json;
  (void)token_count;

  if (tokens == NULL || key == NULL ||
      object_index >= token_count ||
      tokens[object_index].type != JSON_OBJECT) {
    return -1;
  }

  key_tok = json_query((struct json_token *)&tokens[object_index],
                       tokens[object_index].sub, key);
  if (key_tok != NULL && key_tok->type != JSON_NONE) {
    return (int)(key_tok - tokens);
  }
  return -1;
}

static int palm_render_json_array_get(const PalmJsonToken *tokens,
                                      size_t token_count, size_t array_index,
                                      size_t element_index) {
  struct json_token *arr_tok = (struct json_token *)&tokens[array_index];
  struct json_token *elem = NULL;
  size_t i = 0U;
  (void)token_count;

  if (arr_tok->type != JSON_ARRAY || (int)element_index >= arr_tok->children)
    return -1;

  elem = json_array_begin(arr_tok);
  for (i = 0U; i < element_index && elem; ++i) {
    elem = json_array_next(elem);
  }
  if (elem)
    return (int)(elem - tokens);
  return -1;
}

static int palm_render_gltf_accessor_component_count(const char *type_name) {
  if (type_name == NULL) {
    return 0;
  }
  if (strcmp(type_name, "SCALAR") == 0) {
    return 1;
  }
  if (strcmp(type_name, "VEC2") == 0) {
    return 2;
  }
  if (strcmp(type_name, "VEC3") == 0) {
    return 3;
  }
  if (strcmp(type_name, "VEC4") == 0) {
    return 4;
  }
  return 0;
}

static size_t palm_render_gltf_component_size(int component_type) {
  switch (component_type) {
  case 5120:
  case 5121:
    return 1U;
  case 5122:
  case 5123:
    return 2U;
  case 5125:
  case 5126:
    return 4U;
  default:
    return 0U;
  }
}

static float
palm_render_gltf_read_component_as_float(const unsigned char *source,
                                         int component_type, int normalized) {
  if (source == NULL) {
    return 0.0f;
  }

  switch (component_type) {
  case 5120: {
    const int8_t value = *(const int8_t *)source;
    return normalized ? palm_render_clamp((float)value / 127.0f, -1.0f, 1.0f)
                      : (float)value;
  }
  case 5121: {
    const uint8_t value = *(const uint8_t *)source;
    return normalized ? (float)value / 255.0f : (float)value;
  }
  case 5122: {
    const int16_t value = *(const int16_t *)source;
    return normalized ? palm_render_clamp((float)value / 32767.0f, -1.0f, 1.0f)
                      : (float)value;
  }
  case 5123: {
    const uint16_t value = *(const uint16_t *)source;
    return normalized ? (float)value / 65535.0f : (float)value;
  }
  case 5125:
    return (float)(*(const uint32_t *)source);
  case 5126:
    return *(const float *)source;
  default:
    return 0.0f;
  }
}

static unsigned int palm_render_gltf_read_index(const unsigned char *source,
                                                int component_type) {
  if (source == NULL) {
    return 0U;
  }

  switch (component_type) {
  case 5121:
    return (unsigned int)(*(const uint8_t *)source);
  case 5123:
    return (unsigned int)(*(const uint16_t *)source);
  case 5125:
    return *(const uint32_t *)source;
  default:
    return 0U;
  }
}

static int palm_render_load_obj_vertices(
    const char *relative_asset_path, PalmVertex **out_vertices,
    GLsizei *out_vertex_count, float *out_model_height, float *out_model_radius,
    char *out_diffuse_texture_path, size_t out_diffuse_texture_path_size) {
  char obj_path[PLATFORM_PATH_MAX] = {0};
  char selected_texture_path[PLATFORM_PATH_MAX] = {0};
  char *source = NULL;
  char *cursor = NULL;
  PalmVec3Array positions = {0};
  PalmVec3Array normals = {0};
  PalmVec2Array texcoords = {0};
  PalmVertexArray vertices = {0};
  PalmMaterialArray materials = {0};
  PalmColor current_color = {0.45f, 0.35f, 0.20f};
  const PalmMaterial *current_material = NULL;
  PalmVec3 bounds_min = {1.0e9f, 1.0e9f, 1.0e9f};
  PalmVec3 bounds_max = {-1.0e9f, -1.0e9f, -1.0e9f};
  PalmVec3 base_center = {0.0f, 0.0f, 0.0f};
  int texture_selection_valid = 1;
  size_t base_count = 0U;
  size_t i = 0U;

  if (relative_asset_path == NULL || out_vertices == NULL ||
      out_vertex_count == NULL || out_model_height == NULL ||
      out_model_radius == NULL) {
    return 0;
  }
  *out_vertices = NULL;
  *out_vertex_count = 0;
  *out_model_height = 1.0f;
  *out_model_radius = 1.0f;
  if (out_diffuse_texture_path != NULL && out_diffuse_texture_path_size > 0U) {
    out_diffuse_texture_path[0] = '\0';
  }

  if (!palm_render_resolve_asset_path(relative_asset_path, obj_path,
                                      sizeof(obj_path)) ||
      !palm_render_load_text_file(obj_path, "OBJ", &source)) {
    return 0;
  }

  cursor = source;
  while (*cursor != '\0') {
    char *line_start = cursor;
    char *line_end = cursor;
    char *trimmed = NULL;
    const char *argument = NULL;

    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
      ++line_end;
    }
    if (*line_end == '\r') {
      *line_end = '\0';
      cursor = line_end + 1;
      if (*cursor == '\n') {
        *cursor = '\0';
        cursor += 1;
      }
    } else if (*line_end == '\n') {
      *line_end = '\0';
      cursor = line_end + 1;
    } else {
      cursor = line_end;
    }

    trimmed = palm_render_trim_left(line_start);
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
      continue;
    }

    argument = palm_render_match_keyword(trimmed, "mtllib");
    if (argument != NULL) {
      char mtl_path[PLATFORM_PATH_MAX] = {0};
      char relative_name[PLATFORM_PATH_MAX] = {0};

      (void)snprintf(relative_name, sizeof(relative_name), "%s", argument);
      palm_render_trim_right_in_place(relative_name);
      if (!palm_render_build_relative_path(obj_path, relative_name, mtl_path,
                                           sizeof(mtl_path))) {
        diagnostics_logf("palm_render: skipped MTL reference '%s' for %s",
                         relative_name, obj_path);
      } else if (!palm_render_file_exists(mtl_path)) {
        diagnostics_logf("palm_render: missing MTL '%s' referenced by %s",
                         mtl_path, obj_path);
      } else if (!palm_render_parse_mtl(mtl_path, &materials)) {
        diagnostics_logf(
            "palm_render: failed to parse MTL '%s', using fallback colors",
            mtl_path);
      }
    } else if ((argument = palm_render_match_keyword(trimmed, "usemtl")) !=
               NULL) {
      char material_name[64] = {0};

      (void)snprintf(material_name, sizeof(material_name), "%s", argument);
      palm_render_trim_right_in_place(material_name);
      current_material = palm_render_find_material(&materials, material_name);
      current_color = (current_material != NULL)
                          ? current_material->diffuse
                          : (PalmColor){0.70f, 0.70f, 0.70f};
    } else if (strncmp(trimmed, "v ", 2U) == 0) {
      PalmVec3 value = {0};
      if (palm_render_sscanf(trimmed + 2, "%f %f %f", &value.x, &value.y,
                             &value.z) == 3) {
        if (!palm_render_push_vec3(&positions, value)) {
          palm_render_show_error("Memory Error",
                                 "Failed to store palm OBJ positions.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    } else if (strncmp(trimmed, "vt ", 3U) == 0) {
      PalmVec2 value = {0.0f, 0.0f};
      if (palm_render_sscanf(trimmed + 3, "%f %f", &value.x, &value.y) >= 2) {
        if (!palm_render_push_vec2(&texcoords, value)) {
          palm_render_show_error("Memory Error",
                                 "Failed to store palm OBJ texcoords.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    } else if (strncmp(trimmed, "vn ", 3U) == 0) {
      PalmVec3 value = {0};
      if (palm_render_sscanf(trimmed + 3, "%f %f %f", &value.x, &value.y,
                             &value.z) == 3) {
        if (!palm_render_push_vec3(&normals, value)) {
          palm_render_show_error("Memory Error",
                                 "Failed to store palm OBJ normals.");
          free(source);
          free(vertices.data);
          free(positions.data);
          free(normals.data);
          free(texcoords.data);
          free(materials.data);
          return 0;
        }
      }
    } else if (strncmp(trimmed, "f ", 2U) == 0) {
      PalmObjIndex corners[PALM_RENDER_MAX_FACE_VERTICES] = {0};
      int corner_count = 0;
      char *token = palm_render_trim_left(trimmed + 2);

      while (token[0] != '\0' && corner_count < PALM_RENDER_MAX_FACE_VERTICES) {
        PalmObjIndex index = {0};
        char *separator = token;
        char separator_char = '\0';

        while (*separator != '\0' && *separator != ' ' && *separator != '\t') {
          ++separator;
        }
        separator_char = *separator;
        if (separator_char != '\0') {
          *separator = '\0';
        }

        if (token[0] != '\0' && palm_render_parse_face_vertex(token, &index)) {
          corners[corner_count] = index;
          corner_count += 1;
        }

        if (separator_char == '\0') {
          token = separator;
        } else {
          token = palm_render_trim_left(separator + 1);
        }
      }

      if (corner_count >= 3) {
        int triangle_index = 0;

        for (triangle_index = 1; triangle_index + 1 < corner_count;
             ++triangle_index) {
          const PalmObjIndex triangle[3] = {corners[0], corners[triangle_index],
                                            corners[triangle_index + 1]};
          PalmVec3 triangle_positions[3];
          PalmVec3 triangle_normal = {0.0f, 1.0f, 0.0f};
          PalmVec2 triangle_texcoords[3] = {
              {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
          const int material_has_texture =
              (current_material != NULL && current_material->has_texture != 0 &&
               current_material->texture_path[0] != '\0' &&
               !palm_render_path_uses_black_key(
                   current_material->texture_path));
          int vertex_index = 0;

          if (texture_selection_valid) {
            if (material_has_texture) {
              if (selected_texture_path[0] == '\0') {
                /* Pertama kali menemukan material bertekstur - simpan sebagai
                 * tekstur terpilih untuk seluruh mesh (misalnya sawit.jpg untuk
                 * batang kelapa sawit). */
                (void)snprintf(selected_texture_path,
                               sizeof(selected_texture_path), "%s",
                               current_material->texture_path);
              } else if (strcmp(selected_texture_path,
                                current_material->texture_path) != 0) {
                /* Tekstur berbeda ditemukan. Kalau material ini butuh
                 * color-bake (misal Vegetation_Blur7 — warnanya sudah
                 * di-estimate ke diffuse saat parse MTL), tidak perlu
                 * UV dari tekstur ini — cukup pakai vertex color.
                 * Jadi JANGAN batalkan seleksi tekstur batang.
                 * Hanya batalkan jika material benar-benar butuh UV
                 * dari tekstur yang berbeda (needs_texture_color = 0). */
                if (!palm_render_material_needs_texture_color(current_material)) {
                  texture_selection_valid = 0;
                }
                /* else: warna sudah di-bake ke vertex color, skip UV saja */
              }
              /* Jika sama dengan selected_texture_path, lanjut normal. */
            }
            /* Material tanpa tekstur (misal leaf1) dibiarkan — face-nya akan
             * memakai UV {0,0} dan warna vertex yang sudah di-bake dari Kd,
             * sehingga tidak membatalkan tekstur batang yang sudah terpilih. */
          }

          for (vertex_index = 0; vertex_index < 3; ++vertex_index) {
            const int resolved_position = palm_render_resolve_obj_index(
                triangle[vertex_index].position_index, positions.count);
            if (resolved_position < 0) {
              palm_render_show_error(
                  "OBJ Error", "Palm OBJ contains an invalid position index.");
              free(source);
              free(vertices.data);
              free(positions.data);
              free(normals.data);
              free(texcoords.data);
              free(materials.data);
              return 0;
            }

            triangle_positions[vertex_index] =
                positions.data[resolved_position];
            if (material_has_texture && texture_selection_valid) {
              const int resolved_texcoord = palm_render_resolve_obj_index(
                  triangle[vertex_index].texcoord_index, texcoords.count);
              if (resolved_texcoord < 0) {
                texture_selection_valid = 0;
              } else {
                triangle_texcoords[vertex_index] =
                    texcoords.data[resolved_texcoord];
              }
            }
          }

          triangle_normal = palm_render_vec3_normalize(palm_render_vec3_cross(
              palm_render_vec3_subtract(triangle_positions[1],
                                        triangle_positions[0]),
              palm_render_vec3_subtract(triangle_positions[2],
                                        triangle_positions[0])));

          for (vertex_index = 0; vertex_index < 3; ++vertex_index) {
            const int resolved_normal = palm_render_resolve_obj_index(
                triangle[vertex_index].normal_index, normals.count);
            const PalmVec3 normal = (resolved_normal >= 0)
                                        ? normals.data[resolved_normal]
                                        : triangle_normal;
            PalmVertex vertex = {
                {triangle_positions[vertex_index].x,
                 triangle_positions[vertex_index].y,
                 triangle_positions[vertex_index].z},
                {normal.x, normal.y, normal.z},
                {current_color.r, current_color.g, current_color.b},
                {triangle_texcoords[vertex_index].x,
                 1.0f - triangle_texcoords[vertex_index].y}};

            if (!palm_render_push_vertex(&vertices, vertex)) {
              palm_render_show_error("Memory Error",
                                     "Failed to store palm OBJ triangles.");
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

  if (positions.count == 0U || vertices.count == 0U) {
    palm_render_show_error("OBJ Error",
                           "Palm OBJ did not contain any renderable geometry.");
    free(source);
    free(vertices.data);
    free(positions.data);
    free(normals.data);
    free(texcoords.data);
    free(materials.data);
    return 0;
  }

  for (i = 0U; i < positions.count; ++i) {
    const PalmVec3 value = positions.data[i];
    if (value.x < bounds_min.x) {
      bounds_min.x = value.x;
    }
    if (value.y < bounds_min.y) {
      bounds_min.y = value.y;
    }
    if (value.z < bounds_min.z) {
      bounds_min.z = value.z;
    }
    if (value.x > bounds_max.x) {
      bounds_max.x = value.x;
    }
    if (value.y > bounds_max.y) {
      bounds_max.y = value.y;
    }
    if (value.z > bounds_max.z) {
      bounds_max.z = value.z;
    }
  }

  for (i = 0U; i < positions.count; ++i) {
    const PalmVec3 value = positions.data[i];
    if (value.y <= bounds_min.y + 5.0f) {
      base_center.x += value.x;
      base_center.z += value.z;
      base_count += 1U;
    }
  }
  if (base_count > 0U) {
    base_center.x /= (float)base_count;
    base_center.z /= (float)base_count;
  }

  for (i = 0U; i < vertices.count; ++i) {
    vertices.data[i].position[0] -= base_center.x;
    vertices.data[i].position[1] -= bounds_min.y;
    vertices.data[i].position[2] -= base_center.z;
  }

  *out_vertices = vertices.data;
  *out_vertex_count = (GLsizei)vertices.count;
  *out_model_height =
      palm_render_clamp(bounds_max.y - bounds_min.y, 1.0f, 10000.0f);
  {
    const float model_width = bounds_max.x - bounds_min.x;
    const float model_depth = bounds_max.z - bounds_min.z;
    const float model_span =
        (model_width > model_depth) ? model_width : model_depth;
    *out_model_radius = palm_render_clamp(model_span * 0.5f, 1.0f, 10000.0f);
  }
  if (out_diffuse_texture_path != NULL && out_diffuse_texture_path_size > 0U &&
      texture_selection_valid && selected_texture_path[0] != '\0') {
    (void)snprintf(out_diffuse_texture_path, out_diffuse_texture_path_size,
                   "%s", selected_texture_path);
  }

  diagnostics_logf(
      "palm_render: loaded OBJ vertices=%d height=%.2f textured=%s source=%s",
      (int)*out_vertex_count, *out_model_height,
      (texture_selection_valid && selected_texture_path[0] != '\0') ? "yes"
                                                                    : "no",
      obj_path);

  free(source);
  free(positions.data);
  free(normals.data);
  free(texcoords.data);
  free(materials.data);
  return 1;
}

static int palm_render_parse_glb_document(const char *relative_asset_path,
                                          PalmGltfDocument *out_document) {
  char asset_path[PLATFORM_PATH_MAX] = {0};
  unsigned char *data = NULL;
  size_t data_size = 0U;
  const unsigned char *json_chunk = NULL;
  size_t json_chunk_size = 0U;
  const unsigned char *binary_chunk = NULL;
  size_t binary_chunk_size = 0U;
  char *json_text = NULL;
  PalmJsonToken *tokens = NULL;
  size_t token_count = 0U;
  size_t offset = 12U;
  int root_index = 0;
  int default_scene = 0;
  int value_index = -1;
  size_t i = 0U;

  if (relative_asset_path == NULL || out_document == NULL) {
    return 0;
  }

  memset(out_document, 0, sizeof(*out_document));
  diagnostics_logf("palm_render: parsing GLB source=%s", relative_asset_path);
  if (!palm_render_resolve_asset_path(relative_asset_path, asset_path,
                                      sizeof(asset_path)) ||
      !palm_render_load_binary_file(asset_path, "GLB", &data, &data_size)) {
    diagnostics_logf("palm_render: failed to resolve or load GLB source=%s",
                     relative_asset_path);
    return 0;
  }

  diagnostics_logf("palm_render: loaded GLB file path=%s size=%zu", asset_path,
                   data_size);

  {
    uint32_t version = 0U;
    if (data_size < 20U || memcmp(data, "glTF", 4U) != 0) {
      free(data);
      palm_render_show_error("GLB Error",
                             "GLB header is invalid or unsupported.");
      return 0;
    }
    memcpy(&version, data + 4U, sizeof(version));
    if (version != 2U) {
      free(data);
      palm_render_show_error("GLB Error",
                             "GLB header is invalid or unsupported.");
      return 0;
    }
  }

  while (offset + 8U <= data_size) {
    uint32_t chunk_length = 0U;
    uint32_t chunk_type = 0U;
    memcpy(&chunk_length, data + offset, sizeof(chunk_length));
    memcpy(&chunk_type, data + offset + 4U, sizeof(chunk_type));
    offset += 8U;
    if (offset + chunk_length > data_size) {
      free(data);
      palm_render_show_error("GLB Error",
                             "GLB chunk extends past the end of the file.");
      return 0;
    }

    if (chunk_type == 0x4E4F534AU) {
      json_chunk = data + offset;
      json_chunk_size = (size_t)chunk_length;
    } else if (chunk_type == 0x004E4942U) {
      binary_chunk = data + offset;
      binary_chunk_size = (size_t)chunk_length;
    }
    offset += (size_t)chunk_length;
  }

  if (json_chunk == NULL || json_chunk_size == 0U || binary_chunk == NULL ||
      binary_chunk_size == 0U) {
    free(data);
    palm_render_show_error("GLB Error",
                           "GLB file is missing JSON or BIN chunks.");
    return 0;
  }

  json_text = (char *)malloc(json_chunk_size + 1U);
  if (json_text == NULL) {
    free(data);
    palm_render_show_error("Memory Error",
                           "Failed to allocate memory for GLB JSON.");
    return 0;
  }
  memcpy(json_text, json_chunk, json_chunk_size);
  json_text[json_chunk_size] = '\0';

  diagnostics_logf("palm_render: GLB chunks json=%zu bin=%zu source=%s",
                   json_chunk_size, binary_chunk_size, asset_path);
  if (!palm_render_json_parse(json_text, json_chunk_size, &tokens,
                              &token_count) ||
      token_count == 0U) {
    diagnostics_logf("palm_render: GLB JSON parse failed source=%s json_size=%zu",
                     asset_path, json_chunk_size);
    palm_render_show_error("GLB Error",
                           "Failed to parse GLB JSON chunk.");
    free(json_text);
    free(tokens);
    free(data);
    return 0;
  }

  root_index = 0;
  out_document->owned_data = data;
  out_document->binary_chunk = binary_chunk;
  out_document->binary_chunk_size = binary_chunk_size;

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "bufferViews");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->buffer_view_count = (size_t)tokens[value_index].children;
    out_document->buffer_views = (PalmGltfBufferView *)calloc(
        out_document->buffer_view_count, sizeof(PalmGltfBufferView));
    if (out_document->buffer_view_count > 0U &&
        out_document->buffer_views == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error",
                             "Failed to allocate GLB buffer views.");
      return 0;
    }

    for (i = 0U; i < out_document->buffer_view_count; ++i) {
      const int view_index = palm_render_json_array_get(tokens, token_count,
                                                        (size_t)value_index, i);
      int member_index = -1;

      if (view_index < 0 || tokens[view_index].type != PALM_JSON_OBJECT) {
        continue;
      }

      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)view_index, "byteOffset");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_size(
            json_text, &tokens[member_index],
            &out_document->buffer_views[i].byte_offset);
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)view_index, "byteLength");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_size(
            json_text, &tokens[member_index],
            &out_document->buffer_views[i].byte_length);
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)view_index, "byteStride");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_size(
            json_text, &tokens[member_index],
            &out_document->buffer_views[i].byte_stride);
      }
    }
  }

  value_index = palm_render_json_root_object_get(tokens, token_count,
                                                 "accessors");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->accessor_count = (size_t)tokens[value_index].children;
    out_document->accessors = (PalmGltfAccessor *)calloc(
        out_document->accessor_count, sizeof(PalmGltfAccessor));
    if (out_document->accessor_count > 0U && out_document->accessors == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error",
                             "Failed to allocate GLB accessors.");
      return 0;
    }

    for (i = 0U; i < out_document->accessor_count; ++i) {
      const int accessor_index = palm_render_json_array_get(
          tokens, token_count, (size_t)value_index, i);
      int member_index = -1;

      out_document->accessors[i].buffer_view_index = -1;
      if (accessor_index < 0 ||
          tokens[accessor_index].type != PALM_JSON_OBJECT) {
        continue;
      }

      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)accessor_index, "bufferView");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_int(
            json_text, &tokens[member_index],
            &out_document->accessors[i].buffer_view_index);
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)accessor_index, "byteOffset");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_size(
            json_text, &tokens[member_index],
            &out_document->accessors[i].byte_offset);
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)accessor_index, "count");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_size(json_text, &tokens[member_index],
                                             &out_document->accessors[i].count);
      }
      member_index =
          palm_render_json_object_get(json_text, tokens, token_count,
                                      (size_t)accessor_index, "componentType");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_int(
            json_text, &tokens[member_index],
            &out_document->accessors[i].component_type);
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)accessor_index, "normalized");
      if (member_index >= 0) {
        out_document->accessors[i].normalized = palm_render_json_token_equals(
            json_text, &tokens[member_index], "true");
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)accessor_index, "type");
      if (member_index >= 0) {
        char type_name[16] = {0};
        const size_t type_length = (size_t)tokens[member_index].len;
        if (type_length > 0U && type_length < sizeof(type_name)) {
          memcpy(type_name, tokens[member_index].str, type_length);
          type_name[type_length] = '\0';
          out_document->accessors[i].component_count =
              palm_render_gltf_accessor_component_count(type_name);
        }
      }
    }
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "textures");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->texture_count = (size_t)tokens[value_index].children;
    out_document->textures = (PalmGltfTexture *)calloc(
        out_document->texture_count, sizeof(PalmGltfTexture));
    if (out_document->texture_count > 0U && out_document->textures == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error",
                             "Failed to allocate GLB textures.");
      return 0;
    }

    for (i = 0U; i < out_document->texture_count; ++i) {
      const int texture_index = palm_render_json_array_get(
          tokens, token_count, (size_t)value_index, i);
      int member_index = -1;

      out_document->textures[i].image_index = -1;
      if (texture_index < 0 || tokens[texture_index].type != PALM_JSON_OBJECT) {
        continue;
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)texture_index, "source");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_int(
            json_text, &tokens[member_index],
            &out_document->textures[i].image_index);
      }
    }
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "materials");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->material_count = (size_t)tokens[value_index].children;
    out_document->materials = (PalmGltfMaterial *)calloc(
        out_document->material_count, sizeof(PalmGltfMaterial));
    if (out_document->material_count > 0U && out_document->materials == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error",
                             "Failed to allocate GLB materials.");
      return 0;
    }

    for (i = 0U; i < out_document->material_count; ++i) {
      const int material_index = palm_render_json_array_get(
          tokens, token_count, (size_t)value_index, i);
      int member_index = -1;
      PalmGltfMaterial material = {{1.0f, 1.0f, 1.0f}, 1, -1};

      if (material_index < 0 ||
          tokens[material_index].type != PALM_JSON_OBJECT) {
        out_document->materials[i] = material;
        continue;
      }

      member_index = palm_render_json_object_get(json_text, tokens, token_count,
                                                 (size_t)material_index,
                                                 "pbrMetallicRoughness");
      if (member_index >= 0 && tokens[member_index].type == PALM_JSON_OBJECT) {
        int nested_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)member_index,
            "baseColorFactor");
        if (nested_index >= 0 && tokens[nested_index].type == PALM_JSON_ARRAY) {
          int component_index = palm_render_json_array_get(
              tokens, token_count, (size_t)nested_index, 0U);
          if (component_index >= 0) {
            (void)palm_render_json_token_to_float(
                json_text, &tokens[component_index], &material.base_color.r);
          }
          component_index = palm_render_json_array_get(
              tokens, token_count, (size_t)nested_index, 1U);
          if (component_index >= 0) {
            (void)palm_render_json_token_to_float(
                json_text, &tokens[component_index], &material.base_color.g);
          }
          component_index = palm_render_json_array_get(
              tokens, token_count, (size_t)nested_index, 2U);
          if (component_index >= 0) {
            (void)palm_render_json_token_to_float(
                json_text, &tokens[component_index], &material.base_color.b);
          }
        }
        nested_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)member_index,
            "baseColorTexture");
        if (nested_index >= 0 &&
            tokens[nested_index].type == PALM_JSON_OBJECT) {
          const int texture_member = palm_render_json_object_get(
              json_text, tokens, token_count, (size_t)nested_index, "index");
          if (texture_member >= 0) {
            (void)palm_render_json_token_to_int(
                json_text, &tokens[texture_member],
                &material.base_color_texture_index);
          }
        }
      }

      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)material_index, "extensions");
      if (member_index >= 0 && tokens[member_index].type == PALM_JSON_OBJECT) {
        const int extension_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)member_index,
            "KHR_materials_pbrSpecularGlossiness");
        if (extension_index >= 0 &&
            tokens[extension_index].type == PALM_JSON_OBJECT) {
          int nested_index = palm_render_json_object_get(
              json_text, tokens, token_count, (size_t)extension_index,
              "diffuseFactor");
          if (nested_index >= 0 &&
              tokens[nested_index].type == PALM_JSON_ARRAY) {
            int component_index = palm_render_json_array_get(
                tokens, token_count, (size_t)nested_index, 0U);
            if (component_index >= 0) {
              (void)palm_render_json_token_to_float(
                  json_text, &tokens[component_index], &material.base_color.r);
            }
            component_index = palm_render_json_array_get(
                tokens, token_count, (size_t)nested_index, 1U);
            if (component_index >= 0) {
              (void)palm_render_json_token_to_float(
                  json_text, &tokens[component_index], &material.base_color.g);
            }
            component_index = palm_render_json_array_get(
                tokens, token_count, (size_t)nested_index, 2U);
            if (component_index >= 0) {
              (void)palm_render_json_token_to_float(
                  json_text, &tokens[component_index], &material.base_color.b);
            }
          }
          nested_index = palm_render_json_object_get(
              json_text, tokens, token_count, (size_t)extension_index,
              "diffuseTexture");
          if (nested_index >= 0 &&
              tokens[nested_index].type == PALM_JSON_OBJECT) {
            const int texture_member = palm_render_json_object_get(
                json_text, tokens, token_count, (size_t)nested_index, "index");
            if (texture_member >= 0) {
              (void)palm_render_json_token_to_int(
                  json_text, &tokens[texture_member],
                  &material.base_color_texture_index);
            }
          }
        }
      }

      out_document->materials[i] = material;
    }
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "images");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->image_count = (size_t)tokens[value_index].children;
    out_document->images = (PalmGltfImage *)calloc(out_document->image_count,
                                                   sizeof(PalmGltfImage));
    out_document->decoded_images = (PalmGltfDecodedImage *)calloc(
        out_document->image_count, sizeof(PalmGltfDecodedImage));
    if (out_document->image_count > 0U &&
        (out_document->images == NULL ||
         out_document->decoded_images == NULL)) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error", "Failed to allocate GLB images.");
      return 0;
    }

    for (i = 0U; i < out_document->image_count; ++i) {
      const int image_index = palm_render_json_array_get(
          tokens, token_count, (size_t)value_index, i);
      int member_index = -1;
      int buffer_view_index = -1;

      if (image_index < 0 || tokens[image_index].type != PALM_JSON_OBJECT) {
        continue;
      }
      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)image_index, "bufferView");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_int(json_text, &tokens[member_index],
                                            &buffer_view_index);
      }
      if (buffer_view_index >= 0 &&
          (size_t)buffer_view_index < out_document->buffer_view_count) {
        const PalmGltfBufferView *buffer_view =
            &out_document->buffer_views[buffer_view_index];
        if (buffer_view->byte_offset + buffer_view->byte_length <=
            out_document->binary_chunk_size) {
          out_document->images[i].bytes =
              out_document->binary_chunk + buffer_view->byte_offset;
          out_document->images[i].byte_length = buffer_view->byte_length;
        }
      }
    }
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "meshes");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->mesh_count = (size_t)tokens[value_index].children;
    out_document->meshes =
        (PalmGltfMesh *)calloc(out_document->mesh_count, sizeof(PalmGltfMesh));
    if (out_document->mesh_count > 0U && out_document->meshes == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error", "Failed to allocate GLB meshes.");
      return 0;
    }

    for (i = 0U; i < out_document->mesh_count; ++i) {
      const int mesh_index = palm_render_json_array_get(tokens, token_count,
                                                        (size_t)value_index, i);
      const int primitives_index =
          (mesh_index >= 0)
              ? palm_render_json_object_get(json_text, tokens, token_count,
                                            (size_t)mesh_index, "primitives")
              : -1;
      size_t primitive_index = 0U;

      if (primitives_index < 0 ||
          tokens[primitives_index].type != PALM_JSON_ARRAY) {
        continue;
      }

      out_document->meshes[i].primitive_count =
          (size_t)tokens[primitives_index].children;
      out_document->meshes[i].primitives = (PalmGltfPrimitive *)calloc(
          out_document->meshes[i].primitive_count, sizeof(PalmGltfPrimitive));
      if (out_document->meshes[i].primitive_count > 0U &&
          out_document->meshes[i].primitives == NULL) {
        palm_render_destroy_gltf_document(out_document);
        free(json_text);
        free(tokens);
        palm_render_show_error("Memory Error",
                               "Failed to allocate GLB primitives.");
        return 0;
      }

      for (primitive_index = 0U;
           primitive_index < out_document->meshes[i].primitive_count;
           ++primitive_index) {
        const int primitive_token_index = palm_render_json_array_get(
            tokens, token_count, (size_t)primitives_index, primitive_index);
        PalmGltfPrimitive primitive = {-1, -1, -1, -1, -1, 4};
        int member_index = -1;

        if (primitive_token_index < 0 ||
            tokens[primitive_token_index].type != PALM_JSON_OBJECT) {
          out_document->meshes[i].primitives[primitive_index] = primitive;
          continue;
        }

        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)primitive_token_index,
            "indices");
        if (member_index >= 0) {
          (void)palm_render_json_token_to_int(
              json_text, &tokens[member_index],
              &primitive.indices_accessor_index);
        }
        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)primitive_token_index,
            "material");
        if (member_index >= 0) {
          (void)palm_render_json_token_to_int(json_text, &tokens[member_index],
                                              &primitive.material_index);
        }
        member_index =
            palm_render_json_object_get(json_text, tokens, token_count,
                                        (size_t)primitive_token_index, "mode");
        if (member_index >= 0) {
          (void)palm_render_json_token_to_int(json_text, &tokens[member_index],
                                              &primitive.mode);
        }

        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)primitive_token_index,
            "attributes");
        if (member_index >= 0 &&
            tokens[member_index].type == PALM_JSON_OBJECT) {
          int attribute_index = palm_render_json_object_get(
              json_text, tokens, token_count, (size_t)member_index, "POSITION");
          if (attribute_index >= 0) {
            (void)palm_render_json_token_to_int(
                json_text, &tokens[attribute_index],
                &primitive.position_accessor_index);
          }
          attribute_index = palm_render_json_object_get(
              json_text, tokens, token_count, (size_t)member_index, "NORMAL");
          if (attribute_index >= 0) {
            (void)palm_render_json_token_to_int(
                json_text, &tokens[attribute_index],
                &primitive.normal_accessor_index);
          }
          attribute_index =
              palm_render_json_object_get(json_text, tokens, token_count,
                                          (size_t)member_index, "TEXCOORD_0");
          if (attribute_index >= 0) {
            (void)palm_render_json_token_to_int(
                json_text, &tokens[attribute_index],
                &primitive.texcoord_accessor_index);
          }
        }

        out_document->meshes[i].primitives[primitive_index] = primitive;
      }
    }
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "nodes");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    out_document->node_count = (size_t)tokens[value_index].children;
    out_document->nodes =
        (PalmGltfNode *)calloc(out_document->node_count, sizeof(PalmGltfNode));
    if (out_document->node_count > 0U && out_document->nodes == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error", "Failed to allocate GLB nodes.");
      return 0;
    }

    for (i = 0U; i < out_document->node_count; ++i) {
      const int node_index = palm_render_json_array_get(tokens, token_count,
                                                        (size_t)value_index, i);
      int member_index = -1;
      PalmVec3 translation = {0.0f, 0.0f, 0.0f};
      PalmVec3 scale = {1.0f, 1.0f, 1.0f};
      float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      size_t child_index = 0U;

      out_document->nodes[i].mesh_index = -1;
      palm_render_matrix_identity(out_document->nodes[i].transform);
      if (node_index < 0 || tokens[node_index].type != PALM_JSON_OBJECT) {
        continue;
      }

      member_index = palm_render_json_object_get(json_text, tokens, token_count,
                                                 (size_t)node_index, "mesh");
      if (member_index >= 0) {
        (void)palm_render_json_token_to_int(json_text, &tokens[member_index],
                                            &out_document->nodes[i].mesh_index);
      }

      member_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)node_index, "children");
      if (member_index >= 0 && tokens[member_index].type == PALM_JSON_ARRAY) {
        out_document->nodes[i].child_count =
            (size_t)tokens[member_index].children;
        out_document->nodes[i].children =
            (int *)calloc(out_document->nodes[i].child_count, sizeof(int));
        if (out_document->nodes[i].child_count > 0U &&
            out_document->nodes[i].children == NULL) {
          palm_render_destroy_gltf_document(out_document);
          free(json_text);
          free(tokens);
          palm_render_show_error("Memory Error",
                                 "Failed to allocate GLB node children.");
          return 0;
        }
        for (child_index = 0U; child_index < out_document->nodes[i].child_count;
             ++child_index) {
          const int child_token = palm_render_json_array_get(
              tokens, token_count, (size_t)member_index, child_index);
          if (child_token >= 0) {
            (void)palm_render_json_token_to_int(
                json_text, &tokens[child_token],
                &out_document->nodes[i].children[child_index]);
          }
        }
      }

      member_index = palm_render_json_object_get(json_text, tokens, token_count,
                                                 (size_t)node_index, "matrix");
      if (member_index >= 0 && tokens[member_index].type == PALM_JSON_ARRAY &&
          tokens[member_index].children >= 16) {
        size_t matrix_index = 0U;
        for (matrix_index = 0U; matrix_index < 16U; ++matrix_index) {
          const int value_token = palm_render_json_array_get(
              tokens, token_count, (size_t)member_index, matrix_index);
          if (value_token >= 0) {
            (void)palm_render_json_token_to_float(
                json_text, &tokens[value_token],
                &out_document->nodes[i].transform[matrix_index]);
          }
        }
      } else {
        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)node_index, "translation");
        if (member_index >= 0 && tokens[member_index].type == PALM_JSON_ARRAY) {
          const int tx = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 0U);
          const int ty = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 1U);
          const int tz = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 2U);
          if (tx >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[tx],
                                                  &translation.x);
          }
          if (ty >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[ty],
                                                  &translation.y);
          }
          if (tz >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[tz],
                                                  &translation.z);
          }
        }

        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)node_index, "rotation");
        if (member_index >= 0 && tokens[member_index].type == PALM_JSON_ARRAY) {
          size_t component_index = 0U;
          for (component_index = 0U; component_index < 4U; ++component_index) {
            const int component_token = palm_render_json_array_get(
                tokens, token_count, (size_t)member_index, component_index);
            if (component_token >= 0) {
              (void)palm_render_json_token_to_float(json_text,
                                                    &tokens[component_token],
                                                    &rotation[component_index]);
            }
          }
        }

        member_index = palm_render_json_object_get(
            json_text, tokens, token_count, (size_t)node_index, "scale");
        if (member_index >= 0 && tokens[member_index].type == PALM_JSON_ARRAY) {
          const int sx = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 0U);
          const int sy = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 1U);
          const int sz = palm_render_json_array_get(tokens, token_count,
                                                    (size_t)member_index, 2U);
          if (sx >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[sx],
                                                  &scale.x);
          }
          if (sy >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[sy],
                                                  &scale.y);
          }
          if (sz >= 0) {
            (void)palm_render_json_token_to_float(json_text, &tokens[sz],
                                                  &scale.z);
          }
        }

        palm_render_matrix_from_trs(out_document->nodes[i].transform,
                                    &translation, rotation, &scale);
      }
    }
  }

  value_index = palm_render_json_root_object_get(tokens, token_count, "scene");
  if (value_index >= 0) {
    (void)palm_render_json_token_to_int(json_text, &tokens[value_index],
                                        &default_scene);
  }

  value_index =
      palm_render_json_root_object_get(tokens, token_count, "scenes");
  if (value_index >= 0 && tokens[value_index].type == PALM_JSON_ARRAY) {
    const int scene_index = palm_render_json_array_get(
        tokens, token_count, (size_t)value_index,
        (size_t)((default_scene >= 0) ? default_scene : 0));
    if (scene_index >= 0 && tokens[scene_index].type == PALM_JSON_OBJECT) {
      const int scene_nodes_index = palm_render_json_object_get(
          json_text, tokens, token_count, (size_t)scene_index, "nodes");
      if (scene_nodes_index >= 0 &&
          tokens[scene_nodes_index].type == PALM_JSON_ARRAY) {
        out_document->scene_root_count =
            (size_t)tokens[scene_nodes_index].children;
        out_document->scene_roots =
            (int *)calloc(out_document->scene_root_count, sizeof(int));
        if (out_document->scene_root_count > 0U &&
            out_document->scene_roots == NULL) {
          palm_render_destroy_gltf_document(out_document);
          free(json_text);
          free(tokens);
          palm_render_show_error("Memory Error",
                                 "Failed to allocate GLB scene roots.");
          return 0;
        }

        for (i = 0U; i < out_document->scene_root_count; ++i) {
          const int root_token = palm_render_json_array_get(
              tokens, token_count, (size_t)scene_nodes_index, i);
          if (root_token >= 0) {
            (void)palm_render_json_token_to_int(json_text, &tokens[root_token],
                                                &out_document->scene_roots[i]);
          }
        }
      }
    }
  }

  if (out_document->scene_root_count == 0U && out_document->node_count > 0U) {
    out_document->scene_root_count = 1U;
    out_document->scene_roots = (int *)calloc(1U, sizeof(int));
    if (out_document->scene_roots == NULL) {
      palm_render_destroy_gltf_document(out_document);
      free(json_text);
      free(tokens);
      palm_render_show_error("Memory Error",
                             "Failed to allocate fallback GLB root node.");
      return 0;
    }
    out_document->scene_roots[0] = 0;
  }

  diagnostics_logf(
      "palm_render: parsed GLB meshes=%d materials=%d images=%d source=%s",
      (int)out_document->mesh_count, (int)out_document->material_count,
      (int)out_document->image_count, asset_path);

  free(json_text);
  free(tokens);
  return 1;
}

static void palm_render_destroy_gltf_document(PalmGltfDocument *document) {
  size_t i = 0U;

  if (document == NULL) {
    return;
  }

  if (document->decoded_images != NULL) {
    for (i = 0U; i < document->image_count; ++i) {
      if (document->decoded_images[i].pixels != NULL) {
        stbi_image_free(document->decoded_images[i].pixels);
      }
    }
    free(document->decoded_images);
  }

  if (document->nodes != NULL) {
    for (i = 0U; i < document->node_count; ++i) {
      free(document->nodes[i].children);
    }
  }

  if (document->meshes != NULL) {
    for (i = 0U; i < document->mesh_count; ++i) {
      free(document->meshes[i].primitives);
    }
  }

  free(document->scene_roots);
  free(document->nodes);
  free(document->meshes);
  free(document->images);
  free(document->materials);
  free(document->textures);
  free(document->accessors);
  free(document->buffer_views);
  free(document->owned_data);
  memset(document, 0, sizeof(*document));
}

static int palm_render_decode_gltf_image(PalmGltfDocument *document,
                                         int image_index) {
  PalmGltfDecodedImage *image = NULL;

  if (document == NULL || image_index < 0 ||
      (size_t)image_index >= document->image_count) {
    return 0;
  }

  image = &document->decoded_images[image_index];
  if (image->pixels != NULL) {
    return 1;
  }

  if (document->images[image_index].bytes == NULL ||
      document->images[image_index].byte_length == 0U) {
    return 0;
  }

  image->pixels =
      stbi_load_from_memory(document->images[image_index].bytes,
                            (int)document->images[image_index].byte_length,
                            &image->width, &image->height, &image->channels, 4);
  image->channels = 4;
  return image->pixels != NULL;
}

static PalmColor
palm_render_sample_gltf_material_color(const PalmGltfDocument *document,
                                       int material_index, PalmVec2 texcoord) {
  PalmColor color = {1.0f, 1.0f, 1.0f};

  if (document == NULL || material_index < 0 ||
      (size_t)material_index >= document->material_count) {
    return color;
  }

  color = document->materials[material_index].base_color;
  if (document->materials[material_index].base_color_texture_index >= 0 &&
      (size_t)document->materials[material_index].base_color_texture_index <
          document->texture_count) {
    const PalmGltfTexture *texture =
        &document->textures[document->materials[material_index]
                                .base_color_texture_index];
    PalmGltfDocument *mutable_document = (PalmGltfDocument *)document;

    if (texture->image_index >= 0 &&
        (size_t)texture->image_index < document->image_count &&
        palm_render_decode_gltf_image(mutable_document, texture->image_index)) {
      const PalmGltfDecodedImage *image =
          &document->decoded_images[texture->image_index];
      const float wrapped_u = texcoord.x - floorf(texcoord.x);
      const float wrapped_v = texcoord.y - floorf(texcoord.y);
      const float pixel_x = wrapped_u * (float)(image->width - 1);
      const float pixel_y = wrapped_v * (float)(image->height - 1);
      const int x0 = (int)floorf(pixel_x);
      const int y0 = (int)floorf(pixel_y);
      const int x1 = (x0 + 1 < image->width) ? (x0 + 1) : x0;
      const int y1 = (y0 + 1 < image->height) ? (y0 + 1) : y0;
      const float tx = pixel_x - (float)x0;
      const float ty = pixel_y - (float)y0;
      const unsigned char *c00 = image->pixels + ((y0 * image->width + x0) * 4);
      const unsigned char *c10 = image->pixels + ((y0 * image->width + x1) * 4);
      const unsigned char *c01 = image->pixels + ((y1 * image->width + x0) * 4);
      const unsigned char *c11 = image->pixels + ((y1 * image->width + x1) * 4);
      const float sample_r0 =
          palm_render_mix((float)c00[0] / 255.0f, (float)c10[0] / 255.0f, tx);
      const float sample_g0 =
          palm_render_mix((float)c00[1] / 255.0f, (float)c10[1] / 255.0f, tx);
      const float sample_b0 =
          palm_render_mix((float)c00[2] / 255.0f, (float)c10[2] / 255.0f, tx);
      const float sample_r1 =
          palm_render_mix((float)c01[0] / 255.0f, (float)c11[0] / 255.0f, tx);
      const float sample_g1 =
          palm_render_mix((float)c01[1] / 255.0f, (float)c11[1] / 255.0f, tx);
      const float sample_b1 =
          palm_render_mix((float)c01[2] / 255.0f, (float)c11[2] / 255.0f, tx);

      color.r *= palm_render_mix(sample_r0, sample_r1, ty);
      color.g *= palm_render_mix(sample_g0, sample_g1, ty);
      color.b *= palm_render_mix(sample_b0, sample_b1, ty);
    }
  }

  color.r = palm_render_clamp(color.r, 0.0f, 1.0f);
  color.g = palm_render_clamp(color.g, 0.0f, 1.0f);
  color.b = palm_render_clamp(color.b, 0.0f, 1.0f);
  return color;
}

static int palm_render_append_gltf_node_vertices(
    const PalmGltfDocument *document, int node_index,
    const float *parent_matrix, PalmVertexArray *vertices) {
  float world_matrix[16] = {0};
  size_t primitive_index = 0U;
  size_t child_index = 0U;

  if (document == NULL || vertices == NULL) {
    return 0;
  }
  if (node_index < 0 || (size_t)node_index >= document->node_count) {
    return 1;
  }

  if (parent_matrix != NULL) {
    palm_render_matrix_multiply(world_matrix, parent_matrix,
                                document->nodes[node_index].transform);
  } else {
    memcpy(world_matrix, document->nodes[node_index].transform,
           sizeof(world_matrix));
  }

  if (document->nodes[node_index].mesh_index >= 0 &&
      (size_t)document->nodes[node_index].mesh_index < document->mesh_count) {
    const PalmGltfMesh *mesh =
        &document->meshes[document->nodes[node_index].mesh_index];
    for (primitive_index = 0U; primitive_index < mesh->primitive_count;
         ++primitive_index) {
      const PalmGltfPrimitive *primitive = &mesh->primitives[primitive_index];
      size_t triangle_index = 0U;

      if (primitive->mode != 4 || primitive->indices_accessor_index < 0 ||
          primitive->position_accessor_index < 0 ||
          (size_t)primitive->indices_accessor_index >=
              document->accessor_count ||
          (size_t)primitive->position_accessor_index >=
              document->accessor_count) {
        continue;
      }

      {
        const PalmGltfAccessor *index_accessor =
            &document->accessors[primitive->indices_accessor_index];
        const PalmGltfAccessor *position_accessor =
            &document->accessors[primitive->position_accessor_index];
        const PalmGltfAccessor *normal_accessor =
            (primitive->normal_accessor_index >= 0 &&
             (size_t)primitive->normal_accessor_index <
                 document->accessor_count)
                ? &document->accessors[primitive->normal_accessor_index]
                : NULL;
        const PalmGltfAccessor *texcoord_accessor =
            (primitive->texcoord_accessor_index >= 0 &&
             (size_t)primitive->texcoord_accessor_index <
                 document->accessor_count)
                ? &document->accessors[primitive->texcoord_accessor_index]
                : NULL;
        const PalmGltfBufferView *index_view = NULL;
        const PalmGltfBufferView *position_view = NULL;
        const PalmGltfBufferView *normal_view = NULL;
        const PalmGltfBufferView *texcoord_view = NULL;
        const size_t index_component_size =
            palm_render_gltf_component_size(index_accessor->component_type);
        const size_t position_component_size =
            palm_render_gltf_component_size(position_accessor->component_type);
        size_t index_stride = 0U;
        size_t position_stride = 0U;
        size_t normal_stride = 0U;
        size_t texcoord_stride = 0U;
        const unsigned char *index_base = NULL;
        const unsigned char *position_base = NULL;
        const unsigned char *normal_base = NULL;
        const unsigned char *texcoord_base = NULL;

        if (index_accessor->buffer_view_index < 0 ||
            position_accessor->buffer_view_index < 0 ||
            (size_t)index_accessor->buffer_view_index >=
                document->buffer_view_count ||
            (size_t)position_accessor->buffer_view_index >=
                document->buffer_view_count ||
            position_accessor->component_count < 3 ||
            index_component_size == 0U || position_component_size == 0U) {
          continue;
        }

        index_view = &document->buffer_views[index_accessor->buffer_view_index];
        position_view =
            &document->buffer_views[position_accessor->buffer_view_index];
        normal_view =
            (normal_accessor != NULL &&
             normal_accessor->buffer_view_index >= 0 &&
             (size_t)normal_accessor->buffer_view_index <
                 document->buffer_view_count)
                ? &document->buffer_views[normal_accessor->buffer_view_index]
                : NULL;
        texcoord_view =
            (texcoord_accessor != NULL &&
             texcoord_accessor->buffer_view_index >= 0 &&
             (size_t)texcoord_accessor->buffer_view_index <
                 document->buffer_view_count)
                ? &document->buffer_views[texcoord_accessor->buffer_view_index]
                : NULL;

        index_stride = (index_view->byte_stride != 0U) ? index_view->byte_stride
                                                       : index_component_size;
        position_stride = (position_view->byte_stride != 0U)
                              ? position_view->byte_stride
                              : position_component_size *
                                    (size_t)position_accessor->component_count;
        normal_stride =
            (normal_view != NULL && normal_view->byte_stride != 0U)
                ? normal_view->byte_stride
                : ((normal_accessor != NULL)
                       ? palm_render_gltf_component_size(
                             normal_accessor->component_type) *
                             (size_t)normal_accessor->component_count
                       : 0U);
        texcoord_stride =
            (texcoord_view != NULL && texcoord_view->byte_stride != 0U)
                ? texcoord_view->byte_stride
                : ((texcoord_accessor != NULL)
                       ? palm_render_gltf_component_size(
                             texcoord_accessor->component_type) *
                             (size_t)texcoord_accessor->component_count
                       : 0U);

        if (index_view->byte_offset + index_accessor->byte_offset >=
                document->binary_chunk_size ||
            position_view->byte_offset + position_accessor->byte_offset >=
                document->binary_chunk_size) {
          continue;
        }

        index_base = document->binary_chunk + index_view->byte_offset +
                     index_accessor->byte_offset;
        position_base = document->binary_chunk + position_view->byte_offset +
                        position_accessor->byte_offset;
        if (normal_view != NULL && normal_accessor != NULL &&
            normal_view->byte_offset + normal_accessor->byte_offset <
                document->binary_chunk_size) {
          normal_base = document->binary_chunk + normal_view->byte_offset +
                        normal_accessor->byte_offset;
        }
        if (texcoord_view != NULL && texcoord_accessor != NULL &&
            texcoord_view->byte_offset + texcoord_accessor->byte_offset <
                document->binary_chunk_size) {
          texcoord_base = document->binary_chunk + texcoord_view->byte_offset +
                          texcoord_accessor->byte_offset;
        }

        for (triangle_index = 0U; triangle_index + 2U < index_accessor->count;
             triangle_index += 3U) {
          unsigned int vertex_indices[3] = {0U, 0U, 0U};
          PalmVec3 triangle_positions[3];
          PalmVec3 triangle_normals[3];
          PalmVec2 triangle_texcoords[3];
          PalmColor triangle_colors[3];
          PalmVec3 face_normal = {0.0f, 1.0f, 0.0f};
          int has_normals = (normal_accessor != NULL && normal_base != NULL &&
                             normal_accessor->component_count >= 3);
          int triangle_valid = 1;
          int local_vertex = 0;

          for (local_vertex = 0; local_vertex < 3; ++local_vertex) {
            const unsigned char *index_ptr =
                index_base +
                (triangle_index + (size_t)local_vertex) * index_stride;
            vertex_indices[local_vertex] = palm_render_gltf_read_index(
                index_ptr, index_accessor->component_type);
            if ((size_t)vertex_indices[local_vertex] >=
                position_accessor->count) {
              triangle_valid = 0;
              break;
            }
          }
          if (!triangle_valid) {
            continue;
          }

          for (local_vertex = 0; local_vertex < 3; ++local_vertex) {
            const unsigned char *position_ptr =
                position_base +
                (size_t)vertex_indices[local_vertex] * position_stride;
            PalmVec3 position = {
                palm_render_gltf_read_component_as_float(
                    position_ptr, position_accessor->component_type,
                    position_accessor->normalized),
                palm_render_gltf_read_component_as_float(
                    position_ptr + position_component_size,
                    position_accessor->component_type,
                    position_accessor->normalized),
                palm_render_gltf_read_component_as_float(
                    position_ptr + position_component_size * 2U,
                    position_accessor->component_type,
                    position_accessor->normalized)};
            PalmVec2 texcoord = {0.0f, 0.0f};
            triangle_positions[local_vertex] =
                palm_render_transform_point(world_matrix, position);

            if (texcoord_accessor != NULL && texcoord_base != NULL &&
                texcoord_accessor->component_count >= 2 &&
                (size_t)vertex_indices[local_vertex] <
                    texcoord_accessor->count) {
              const size_t texcoord_component_size =
                  palm_render_gltf_component_size(
                      texcoord_accessor->component_type);
              const unsigned char *texcoord_ptr =
                  texcoord_base +
                  (size_t)vertex_indices[local_vertex] * texcoord_stride;
              texcoord.x = palm_render_gltf_read_component_as_float(
                  texcoord_ptr, texcoord_accessor->component_type,
                  texcoord_accessor->normalized);
              texcoord.y = palm_render_gltf_read_component_as_float(
                  texcoord_ptr + texcoord_component_size,
                  texcoord_accessor->component_type,
                  texcoord_accessor->normalized);
            }
            triangle_texcoords[local_vertex] = texcoord;
            triangle_colors[local_vertex] =
                palm_render_sample_gltf_material_color(
                    document, primitive->material_index, texcoord);

            if (has_normals &&
                (size_t)vertex_indices[local_vertex] < normal_accessor->count) {
              const size_t normal_component_size =
                  palm_render_gltf_component_size(
                      normal_accessor->component_type);
              const unsigned char *normal_ptr =
                  normal_base +
                  (size_t)vertex_indices[local_vertex] * normal_stride;
              const PalmVec3 normal = {
                  palm_render_gltf_read_component_as_float(
                      normal_ptr, normal_accessor->component_type,
                      normal_accessor->normalized),
                  palm_render_gltf_read_component_as_float(
                      normal_ptr + normal_component_size,
                      normal_accessor->component_type,
                      normal_accessor->normalized),
                  palm_render_gltf_read_component_as_float(
                      normal_ptr + normal_component_size * 2U,
                      normal_accessor->component_type,
                      normal_accessor->normalized)};
              triangle_normals[local_vertex] =
                  palm_render_transform_direction(world_matrix, normal);
            }
          }

          if (!has_normals) {
            face_normal = palm_render_vec3_normalize(palm_render_vec3_cross(
                palm_render_vec3_subtract(triangle_positions[1],
                                          triangle_positions[0]),
                palm_render_vec3_subtract(triangle_positions[2],
                                          triangle_positions[0])));
          }

          for (local_vertex = 0; local_vertex < 3; ++local_vertex) {
            const PalmVec3 normal =
                has_normals ? triangle_normals[local_vertex] : face_normal;
            const PalmVertex vertex = {{triangle_positions[local_vertex].x,
                                        triangle_positions[local_vertex].y,
                                        triangle_positions[local_vertex].z},
                                       {normal.x, normal.y, normal.z},
                                       {triangle_colors[local_vertex].r,
                                        triangle_colors[local_vertex].g,
                                        triangle_colors[local_vertex].b},
                                       {0.0f, 0.0f}};

            if (!palm_render_push_vertex(vertices, vertex)) {
              palm_render_show_error("Memory Error",
                                     "Failed to store GLB triangles.");
              return 0;
            }
          }
        }
      }
    }
  }

  for (child_index = 0U; child_index < document->nodes[node_index].child_count;
       ++child_index) {
    if (!palm_render_append_gltf_node_vertices(
            document, document->nodes[node_index].children[child_index],
            world_matrix, vertices)) {
      return 0;
    }
  }

  return 1;
}

static int palm_render_load_glb_vertices(const char *relative_asset_path,
                                         PalmVertex **out_vertices,
                                         GLsizei *out_vertex_count,
                                         float *out_model_height,
                                         float *out_model_radius) {
  PalmGltfDocument document = {0};
  PalmVertexArray vertices = {0};
  PalmVec3 bounds_min = {1.0e9f, 1.0e9f, 1.0e9f};
  PalmVec3 bounds_max = {-1.0e9f, -1.0e9f, -1.0e9f};
  PalmVec3 base_center = {0.0f, 0.0f, 0.0f};
  size_t base_count = 0U;
  size_t i = 0U;
  float identity[16] = {0};

  if (relative_asset_path == NULL || out_vertices == NULL ||
      out_vertex_count == NULL || out_model_height == NULL ||
      out_model_radius == NULL) {
    return 0;
  }

  *out_vertices = NULL;
  *out_vertex_count = 0;
  *out_model_height = 1.0f;
  *out_model_radius = 1.0f;

  if (!palm_render_parse_glb_document(relative_asset_path, &document)) {
    return 0;
  }

  palm_render_matrix_identity(identity);
  for (i = 0U; i < document.scene_root_count; ++i) {
    if (!palm_render_append_gltf_node_vertices(
            &document, document.scene_roots[i], identity, &vertices)) {
      palm_render_destroy_gltf_document(&document);
      free(vertices.data);
      return 0;
    }
  }

  if (vertices.count == 0U) {
    palm_render_destroy_gltf_document(&document);
    free(vertices.data);
    palm_render_show_error("GLB Error",
                           "GLB did not contain any renderable geometry.");
    return 0;
  }

  for (i = 0U; i < vertices.count; ++i) {
    const PalmVertex *vertex = &vertices.data[i];
    if (vertex->position[0] < bounds_min.x) {
      bounds_min.x = vertex->position[0];
    }
    if (vertex->position[1] < bounds_min.y) {
      bounds_min.y = vertex->position[1];
    }
    if (vertex->position[2] < bounds_min.z) {
      bounds_min.z = vertex->position[2];
    }
    if (vertex->position[0] > bounds_max.x) {
      bounds_max.x = vertex->position[0];
    }
    if (vertex->position[1] > bounds_max.y) {
      bounds_max.y = vertex->position[1];
    }
    if (vertex->position[2] > bounds_max.z) {
      bounds_max.z = vertex->position[2];
    }
  }

  for (i = 0U; i < vertices.count; ++i) {
    const PalmVertex *vertex = &vertices.data[i];
    if (vertex->position[1] <=
        bounds_min.y + palm_render_clamp((bounds_max.y - bounds_min.y) * 0.03f,
                                         0.5f, 6.0f)) {
      base_center.x += vertex->position[0];
      base_center.z += vertex->position[2];
      base_count += 1U;
    }
  }
  if (base_count > 0U) {
    base_center.x /= (float)base_count;
    base_center.z /= (float)base_count;
  }

  for (i = 0U; i < vertices.count; ++i) {
    vertices.data[i].position[0] -= base_center.x;
    vertices.data[i].position[1] -= bounds_min.y;
    vertices.data[i].position[2] -= base_center.z;
  }

  *out_vertices = vertices.data;
  *out_vertex_count = (GLsizei)vertices.count;
  *out_model_height =
      palm_render_clamp(bounds_max.y - bounds_min.y, 1.0f, 10000.0f);
  {
    const float model_width = bounds_max.x - bounds_min.x;
    const float model_depth = bounds_max.z - bounds_min.z;
    const float model_span =
        (model_width > model_depth) ? model_width : model_depth;
    *out_model_radius = palm_render_clamp(model_span * 0.5f, 1.0f, 10000.0f);
  }

  diagnostics_logf("palm_render: loaded GLB vertices=%d height=%.2f source=%s",
                   (int)*out_vertex_count, *out_model_height,
                   relative_asset_path);

  palm_render_destroy_gltf_document(&document);
  return 1;
}

static int palm_render_load_model_vertices(
    const char *relative_asset_path, PalmVertex **out_vertices,
    GLsizei *out_vertex_count, float *out_model_height, float *out_model_radius,
    char *out_diffuse_texture_path, size_t out_diffuse_texture_path_size) {
  const char *extension =
      strrchr((relative_asset_path != NULL) ? relative_asset_path : "", '.');
  int is_glb = 0;

  if (extension != NULL && strlen(extension) == 4U) {
    is_glb = tolower((unsigned char)extension[1]) == 'g' &&
             tolower((unsigned char)extension[2]) == 'l' &&
             tolower((unsigned char)extension[3]) == 'b' &&
             extension[4] == '\0';
  }

  if (out_diffuse_texture_path != NULL && out_diffuse_texture_path_size > 0U) {
    out_diffuse_texture_path[0] = '\0';
  }

  diagnostics_logf("palm_render: dispatch model loader source=%s format=%s",
                   (relative_asset_path != NULL) ? relative_asset_path : "",
                   is_glb ? "glb" : "obj");

  if (is_glb) {
    return palm_render_load_glb_vertices(relative_asset_path, out_vertices,
                                         out_vertex_count, out_model_height,
                                         out_model_radius);
  }

  return palm_render_load_obj_vertices(
      relative_asset_path, out_vertices, out_vertex_count, out_model_height,
      out_model_radius, out_diffuse_texture_path,
      out_diffuse_texture_path_size);
}

static int palm_render_reserve_instances(PalmRenderVariant *variant,
                                         size_t required_instance_capacity) {
  if (variant == NULL) {
    return 0;
  }
  if (required_instance_capacity <= variant->cpu_instance_capacity) {
    return 1;
  }

  if (!palm_render_reserve_memory(
          &variant->cpu_instances, &variant->cpu_instance_capacity,
          required_instance_capacity, sizeof(PalmInstanceData))) {
    palm_render_show_error("Memory Error",
                           "Failed to allocate palm instance data.");
    return 0;
  }

  return 1;
}

static int
palm_render_reserve_visible_instances(PalmRenderVariant *variant,
                                      size_t required_instance_capacity) {
  if (variant == NULL) {
    return 0;
  }
  if (required_instance_capacity <= variant->cpu_visible_instance_capacity) {
    return 1;
  }

  if (!palm_render_reserve_memory(&variant->cpu_visible_instances,
                                  &variant->cpu_visible_instance_capacity,
                                  required_instance_capacity,
                                  sizeof(PalmInstanceData))) {
    palm_render_show_error("Memory Error",
                           "Failed to allocate visible palm instance data.");
    return 0;
  }

  return 1;
}

static void palm_render_reset_instances(PalmRenderMesh *mesh) {
  int variant_index = 0;

  if (mesh == NULL) {
    return;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    mesh->variants[variant_index].instance_count = 0;
    mesh->variants[variant_index].visible_instance_count = 0;
  }
}

static int palm_render_upload_instances(PalmRenderMesh *mesh,
                                        const ViewFrustum *frustum) {
  int variant_index = 0;

  if (mesh == NULL) {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    PalmRenderVariant *variant = &mesh->variants[variant_index];
    const void *upload_instances = NULL;
    GLsizei upload_count = variant->instance_count;

    if (frustum != NULL && frustum->valid != 0) {
      const PalmInstanceData *source_instances =
          (const PalmInstanceData *)variant->cpu_instances;
      PalmInstanceData *visible_instances = NULL;
      GLsizei instance_index = 0;

      if (variant->instance_count > 0 &&
          !palm_render_reserve_visible_instances(
              variant, (size_t)variant->instance_count)) {
        return 0;
      }

      visible_instances = (PalmInstanceData *)variant->cpu_visible_instances;
      variant->visible_instance_count = 0;
      for (instance_index = 0; instance_index < variant->instance_count;
           ++instance_index) {
        if (!palm_render_instance_intersects_frustum(
                variant, &source_instances[instance_index], frustum)) {
          continue;
        }
        visible_instances[variant->visible_instance_count] =
            source_instances[instance_index];
        variant->visible_instance_count += 1;
      }

      upload_instances =
          (variant->visible_instance_count > 0) ? visible_instances : NULL;
      upload_count = variant->visible_instance_count;
    } else {
      variant->visible_instance_count = variant->instance_count;
      upload_instances =
          (variant->instance_count > 0) ? variant->cpu_instances : NULL;
    }

    glBindBuffer(GL_ARRAY_BUFFER, variant->instance_buffer);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(PalmInstanceData) * (size_t)upload_count,
                 upload_instances, GL_DYNAMIC_DRAW);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return 1;
}

static int palm_render_float_nearly_equal(float a, float b, float epsilon) {
  return fabsf(a - b) <= epsilon;
}

static int
palm_render_instance_intersects_frustum(const PalmRenderVariant *variant,
                                        const PalmInstanceData *instance,
                                        const ViewFrustum *frustum) {
  float scale = 1.0f;
  float half_height = 0.5f;
  float footprint_radius = 0.5f;
  float sphere_radius = 0.5f;

  if (variant == NULL || instance == NULL || frustum == NULL ||
      frustum->valid == 0) {
    return 1;
  }

  scale = fabsf(instance->transform[5]);
  if (scale < 0.0001f) {
    scale = 1.0f;
  }

  half_height = variant->model_height * scale * 0.5f;
  footprint_radius = variant->model_radius * scale;
  sphere_radius =
      sqrtf(half_height * half_height + footprint_radius * footprint_radius);

  return view_frustum_contains_sphere(frustum, instance->transform[12],
                                      instance->transform[13] + half_height,
                                      instance->transform[14], sphere_radius);
}

static int palm_render_cache_matches(const PalmRenderMesh *mesh, int grid_min_x,
                                     int grid_max_x, int grid_min_z,
                                     int grid_max_z, float radius,
                                     float cell_size,
                                     const SceneSettings *settings) {
  if (mesh == NULL || settings == NULL || mesh->cache_valid == 0) {
    return 0;
  }

  return mesh->cache_grid_min_x == grid_min_x &&
         mesh->cache_grid_max_x == grid_max_x &&
         mesh->cache_grid_min_z == grid_min_z &&
         mesh->cache_grid_max_z == grid_max_z &&
         palm_render_float_nearly_equal(mesh->cache_radius, radius, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_cell_size, cell_size,
                                        0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_palm_size,
                                        settings->palm_size, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_palm_count,
                                        settings->palm_count, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_palm_fruit_density,
                                        settings->palm_fruit_density, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_palm_render_radius,
                                        settings->palm_render_radius, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_terrain_base_height,
                                        settings->terrain_base_height,
                                        0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_terrain_height_scale,
                                        settings->terrain_height_scale,
                                        0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_terrain_roughness,
                                        settings->terrain_roughness, 0.001f) &&
         palm_render_float_nearly_equal(mesh->cache_terrain_ridge_strength,
                                        settings->terrain_ridge_strength,
                                        0.001f);
}

static int palm_render_has_category(const PalmRenderMesh *mesh, int category) {
  int variant_index = 0;

  if (mesh == NULL) {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    const PalmRenderVariant *variant = &mesh->variants[variant_index];
    if (variant->category == category && variant->vao != 0U &&
        variant->vertex_count > 0) {
      return 1;
    }
  }

  return 0;
}

static GLsizei
palm_render_get_max_vertex_count_for_category(const PalmRenderMesh *mesh,
                                              int category) {
  GLsizei max_vertex_count = 0;
  int variant_index = 0;

  if (mesh == NULL) {
    return 0;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    const PalmRenderVariant *variant = &mesh->variants[variant_index];
    if (variant->category == category &&
        variant->vertex_count > max_vertex_count) {
      max_vertex_count = variant->vertex_count;
    }
  }

  return max_vertex_count;
}

static PalmRenderVariant *palm_render_pick_variant(PalmRenderMesh *mesh,
                                                   int category, int grid_x,
                                                   int grid_z,
                                                   unsigned int seed) {
  PalmRenderVariant *matches[PALM_RENDER_MAX_VARIANTS] = {0};
  int match_count = 0;
  int variant_index = 0;
  int selected_index = 0;

  if (mesh == NULL) {
    return NULL;
  }

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    PalmRenderVariant *variant = &mesh->variants[variant_index];
    if (variant->category == category && variant->vao != 0U &&
        variant->vertex_count > 0) {
      matches[match_count] = variant;
      match_count += 1;
    }
  }

  if (match_count <= 0) {
    return NULL;
  }

  selected_index =
      (int)(palm_render_hash_unit(grid_x, grid_z, seed) * (float)match_count);
  if (selected_index >= match_count) {
    selected_index = match_count - 1;
  }

  return matches[selected_index];
}

static int palm_render_populate_palm_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality) {
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count =
      palm_render_get_max_vertex_count_for_category(mesh,
                                                    PALM_RENDER_CATEGORY_PALM);
  const float house_radius =
      palm_render_clamp(settings->palm_render_radius * 3.35f, 220.0f, 720.0f);
  const float house_cell_size = 128.0f;
  float radius = 0.0f;
  int effective_palm_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL ||
      !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_PALM) ||
      max_vertex_count <= 0) {
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

  if (effective_palm_target <= 0 || cell_size <= 0.0f) {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) *
                                      (size_t)(grid_max_z - grid_min_z + 1);
    const int house_grid_min_x =
        (int)floorf((camera->x - house_radius) / house_cell_size);
    const int house_grid_max_x =
        (int)ceilf((camera->x + house_radius) / house_cell_size);
    const int house_grid_min_z =
        (int)floorf((camera->z - house_radius) / house_cell_size);
    const int house_grid_max_z =
        (int)ceilf((camera->z + house_radius) / house_cell_size);
    const size_t estimated_house_capacity =
        (size_t)(house_grid_max_x - house_grid_min_x + 1) *
        (size_t)(house_grid_max_z - house_grid_min_z + 1) * 2U;
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z,
                                  grid_max_z, radius, cell_size, settings) &&
        mesh->cache_house_grid_min_x == house_grid_min_x &&
        mesh->cache_house_grid_max_x == house_grid_max_x &&
        mesh->cache_house_grid_min_z == house_grid_min_z &&
        mesh->cache_house_grid_max_z == house_grid_max_z &&
        palm_render_float_nearly_equal(mesh->cache_house_radius, house_radius,
                                       0.001f) &&
        palm_render_float_nearly_equal(mesh->cache_house_cell_size,
                                       house_cell_size, 0.001f)) {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count;
         ++variant_index) {
      PalmRenderVariant *variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_PALM &&
          !palm_render_reserve_instances(variant, estimated_capacity)) {
        return 0;
      }
      if (variant->category == PALM_RENDER_CATEGORY_HOUSE &&
          !palm_render_reserve_instances(variant, estimated_house_capacity)) {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z) {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x) {
        const float offset_x = palm_render_hash_unit(grid_x, grid_z, 0U);
        const float offset_z = palm_render_hash_unit(grid_x, grid_z, 1U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 2U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 3U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant *variant = palm_render_pick_variant(
            mesh, PALM_RENDER_CATEGORY_PALM, grid_x, grid_z, 5U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 4U) *
                          (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_render_height(x, z, settings);
        const float fruit_density =
            palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        PalmColor tint = {
            palm_render_mix(0.94f, 1.04f, variation) + fruit_density * 0.05f,
            palm_render_mix(0.92f, 1.10f, scale_jitter),
            palm_render_mix(0.92f, 1.03f, variation) - fruit_density * 0.03f};
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f) {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit) {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity) {
          continue;
        }

        desired_height =
            palm_render_mix(variant->desired_height_min,
                            variant->desired_height_max, variation) *
            settings->palm_size *
            palm_render_mix(variant->scale_jitter_min,
                            variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min,
                                      variant->embed_depth_max, variation) *
                          settings->palm_size +
                      slope * 0.18f;

        palm_render_build_instance_transform(
            &((PalmInstanceData *)
                  variant->cpu_instances)[variant->instance_count],
            x, ground_y - embed_depth, z,
            desired_height / variant->model_height, yaw, tint);
        variant->instance_count += 1;
      }
    }

    if (!palm_render_populate_house_instances(mesh, camera, settings,
                                              house_radius, house_cell_size)) {
      return 0;
    }

    mesh->cache_valid = 1;
    mesh->cache_grid_min_x = grid_min_x;
    mesh->cache_grid_max_x = grid_max_x;
    mesh->cache_grid_min_z = grid_min_z;
    mesh->cache_grid_max_z = grid_max_z;
    mesh->cache_house_grid_min_x = house_grid_min_x;
    mesh->cache_house_grid_max_x = house_grid_max_x;
    mesh->cache_house_grid_min_z = house_grid_min_z;
    mesh->cache_house_grid_max_z = house_grid_max_z;
    mesh->cache_radius = radius;
    mesh->cache_cell_size = cell_size;
    mesh->cache_house_radius = house_radius;
    mesh->cache_house_cell_size = house_cell_size;
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

static int palm_render_add_house_instance(PalmRenderMesh *mesh, int grid_x,
                                          int grid_z, float x, float z,
                                          float distance,
                                          const SceneSettings *settings,
                                          int *in_out_near_count,
                                          int *in_out_far_count) {
  const float variation = palm_render_hash_unit(grid_x, grid_z, 80U);
  const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 81U);
  const float yaw =
      palm_render_hash_unit(grid_x, grid_z, 82U) * (k_palm_render_pi * 2.0f);
  const float slope = palm_render_estimate_slope(x, z, settings);
  PalmRenderVariant *variant = palm_render_pick_variant(
      mesh, PALM_RENDER_CATEGORY_HOUSE, grid_x, grid_z, 83U);
  const float ground_y = terrain_get_render_height(x, z, settings);
  const float size_bias = palm_render_mix(
      0.94f, 1.08f, palm_render_clamp(settings->palm_size, 0.0f, 1.0f));
  PalmColor tint = {palm_render_mix(0.92f, 1.04f, variation),
                    palm_render_mix(0.92f, 1.02f, scale_jitter),
                    palm_render_mix(0.90f, 1.00f, variation)};
  float desired_height = 0.0f;
  float embed_depth = 0.0f;

  if (mesh == NULL || settings == NULL || variant == NULL ||
      variant->model_height <= 0.0001f || slope > variant->slope_limit) {
    return 1;
  }
  if ((size_t)variant->instance_count >= variant->cpu_instance_capacity) {
    return 1;
  }

  desired_height = palm_render_mix(variant->desired_height_min,
                                   variant->desired_height_max, variation) *
                   size_bias *
                   palm_render_mix(variant->scale_jitter_min,
                                   variant->scale_jitter_max, scale_jitter);
  embed_depth = palm_render_mix(variant->embed_depth_min,
                                variant->embed_depth_max, variation) +
                slope * 0.08f;

  palm_render_build_instance_transform(
      &((PalmInstanceData *)variant->cpu_instances)[variant->instance_count], x,
      ground_y - embed_depth, z, desired_height / variant->model_height, yaw,
      tint);
  variant->instance_count += 1;

  if (in_out_near_count != NULL && distance <= 110.0f) {
    *in_out_near_count += 1;
  }
  if (in_out_far_count != NULL && distance >= 260.0f) {
    *in_out_far_count += 1;
  }
  return 1;
}

static int palm_render_populate_house_instances(PalmRenderMesh *mesh,
                                                const CameraState *camera,
                                                const SceneSettings *settings,
                                                float house_radius,
                                                float house_cell_size) {
  const int grid_min_x =
      (int)floorf((camera->x - house_radius) / house_cell_size);
  const int grid_max_x =
      (int)ceilf((camera->x + house_radius) / house_cell_size);
  const int grid_min_z =
      (int)floorf((camera->z - house_radius) / house_cell_size);
  const int grid_max_z =
      (int)ceilf((camera->z + house_radius) / house_cell_size);
  int near_count = 0;
  int far_count = 0;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL ||
      !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_HOUSE)) {
    return 1;
  }

  for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z) {
    int grid_x = 0;

    for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x) {
      const float district = palm_render_hash_unit(grid_x / 2, grid_z / 2, 84U);
      const float local_budget = palm_render_hash_unit(grid_x, grid_z, 85U);
      const int local_count = (local_budget > 0.82f) ? 2 : 1;
      int local_index = 0;

      if (district < 0.28f) {
        continue;
      }

      for (local_index = 0; local_index < local_count; ++local_index) {
        const float offset_x = palm_render_mix(
            0.14f, 0.86f,
            palm_render_hash_unit(grid_x * 3 + local_index, grid_z, 86U));
        const float offset_z = palm_render_mix(
            0.14f, 0.86f,
            palm_render_hash_unit(grid_x, grid_z * 3 + local_index, 87U));
        const float x = ((float)grid_x + offset_x) * house_cell_size;
        const float z = ((float)grid_z + offset_z) * house_cell_size;
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance = sqrtf(dx * dx + dz * dz);
        const float near_weight =
            1.0f - palm_render_clamp(distance / 115.0f, 0.0f, 1.0f);
        const float far_weight = palm_render_clamp(
            (distance - house_radius * 0.56f) / (house_radius * 0.34f), 0.0f,
            1.0f);
        const float middle_penalty =
            1.0f -
            fabsf(palm_render_clamp(distance / house_radius, 0.0f, 1.0f) -
                  0.5f) *
                2.0f;
        const float spawn_bias =
            palm_render_clamp(palm_render_mix(near_weight, far_weight, 0.58f) -
                                  middle_penalty * 0.18f + district * 0.14f,
                              0.0f, 1.0f);
        const float gate = palm_render_hash_unit(
            grid_x + local_index * 17, grid_z - local_index * 13, 88U);

        if (distance > house_radius || gate > spawn_bias) {
          continue;
        }
        if (!palm_render_add_house_instance(
                mesh, grid_x * 5 + local_index, grid_z * 5 - local_index, x, z,
                distance, settings, &near_count, &far_count)) {
          return 0;
        }
      }
    }
  }

  if (near_count <= 0) {
    int attempt = 0;
    for (attempt = 0; attempt < 4; ++attempt) {
      const float angle =
          palm_render_hash_unit(attempt, 0, 89U) * (k_palm_render_pi * 2.0f);
      const float distance =
          palm_render_mix(34.0f, 92.0f, palm_render_hash_unit(attempt, 0, 90U));
      const float x = camera->x + cosf(angle) * distance;
      const float z = camera->z + sinf(angle) * distance;
      if (!palm_render_add_house_instance(mesh, 900 + attempt, -900 - attempt,
                                          x, z, distance, settings, &near_count,
                                          &far_count)) {
        return 0;
      }
      if (near_count > 0) {
        break;
      }
    }
  }

  if (far_count <= 0) {
    int attempt = 0;
    for (attempt = 0; attempt < 5; ++attempt) {
      const float angle =
          palm_render_hash_unit(attempt, 0, 91U) * (k_palm_render_pi * 2.0f);
      const float distance =
          palm_render_mix(house_radius * 0.68f, house_radius * 0.94f,
                          palm_render_hash_unit(attempt, 0, 92U));
      const float x = camera->x + cosf(angle) * distance;
      const float z = camera->z + sinf(angle) * distance;
      if (!palm_render_add_house_instance(mesh, 1200 + attempt, 1400 - attempt,
                                          x, z, distance, settings, &near_count,
                                          &far_count)) {
        return 0;
      }
      if (far_count > 0) {
        break;
      }
    }
  }

  return 1;
}

static int palm_render_populate_tree_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality) {
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count =
      palm_render_get_max_vertex_count_for_category(mesh,
                                                    PALM_RENDER_CATEGORY_TREE);
  const float tree_density_scale =
      (quality != NULL)
          ? palm_render_clamp(quality->tree_density_scale, 0.25f, 1.0f)
          : 1.0f;
  float radius = 0.0f;
  int effective_tree_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL ||
      !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_TREE) ||
      max_vertex_count <= 0) {
    return 1;
  }

  lod_config.requested_radius =
      settings->palm_render_radius *
      palm_render_mix(0.78f, 1.0f, tree_density_scale);
  lod_config.requested_radius_min = 80.0f;
  lod_config.requested_radius_max = 900.0f;
  lod_config.radius_scale_low = 1.08f;
  lod_config.radius_scale_high = 1.22f;
  lod_config.effective_radius_min = 94.0f;
  lod_config.effective_radius_max = 1120.0f;
  lod_config.requested_instance_count =
      settings->palm_count * 0.22f * tree_density_scale;
  lod_config.requested_instance_count_min = 0.0f;
  lod_config.requested_instance_count_max = 1500.0f;
  lod_config.instance_budget_min = 12;
  lod_config.instance_budget_max = 26;
  lod_config.source_vertex_count = (float)max_vertex_count;
  lod_config.fallback_vertex_count = 92502.0f;
  lod_config.vertex_budget_low = 1100000.0f;
  lod_config.vertex_budget_high = 2400000.0f;
  lod_config.cell_size_min = 24.0f;
  lod_config.cell_size_max = 160.0f;

  lod_state = procedural_lod_resolve(quality, &lod_config);
  radius = lod_state.effective_radius;
  effective_tree_target = lod_state.effective_instance_count;
  cell_size = lod_state.cell_size;

  if (effective_tree_target <= 0 || cell_size <= 0.0f) {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) *
                                      (size_t)(grid_max_z - grid_min_z + 1);
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z,
                                  grid_max_z, radius, cell_size, settings)) {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count;
         ++variant_index) {
      PalmRenderVariant *variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_TREE &&
          !palm_render_reserve_instances(variant, estimated_capacity)) {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z) {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x) {
        const float offset_x = palm_render_hash_unit(grid_x, grid_z, 40U);
        const float offset_z = palm_render_hash_unit(grid_x, grid_z, 41U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 42U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 43U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant *variant = palm_render_pick_variant(
            mesh, PALM_RENDER_CATEGORY_TREE, grid_x, grid_z, 44U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 45U) *
                          (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_render_height(x, z, settings);
        const float fruit_density =
            palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        PalmColor tint = {
            palm_render_mix(0.90f, 1.02f, variation),
            palm_render_mix(0.92f, 1.06f, scale_jitter) - fruit_density * 0.01f,
            palm_render_mix(0.88f, 0.99f, variation) - fruit_density * 0.02f};
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f) {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit) {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity) {
          continue;
        }

        desired_height =
            palm_render_mix(variant->desired_height_min,
                            variant->desired_height_max, variation) *
            settings->palm_size *
            palm_render_mix(variant->scale_jitter_min,
                            variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min,
                                      variant->embed_depth_max, variation) *
                          settings->palm_size +
                      slope * 0.14f;

        palm_render_build_instance_transform(
            &((PalmInstanceData *)
                  variant->cpu_instances)[variant->instance_count],
            x, ground_y - embed_depth, z,
            desired_height / variant->model_height, yaw, tint);
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
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality) {
  ProceduralLodConfig lod_config;
  ProceduralLodState lod_state;
  const GLsizei max_vertex_count =
      palm_render_get_max_vertex_count_for_category(mesh,
                                                    PALM_RENDER_CATEGORY_GRASS);
  const float grass_density_scale =
      (quality != NULL)
          ? palm_render_clamp(quality->grass_density_scale, 0.10f, 1.0f)
          : 1.0f;
  float radius = 0.0f;
  int effective_grass_target = 0;
  float cell_size = 0.0f;
  int grid_z = 0;

  if (mesh == NULL || camera == NULL || settings == NULL ||
      !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_GRASS) ||
      max_vertex_count <= 0) {
    return 1;
  }

  lod_config.requested_radius =
      settings->palm_render_radius *
      palm_render_mix(0.62f, 1.0f, grass_density_scale);
  lod_config.requested_radius_min = 80.0f;
  lod_config.requested_radius_max = 900.0f;
  lod_config.radius_scale_low = 1.00f;
  lod_config.radius_scale_high = 1.08f;
  lod_config.effective_radius_min = 72.0f;
  lod_config.effective_radius_max = 760.0f;
  lod_config.requested_instance_count =
      settings->palm_count * 12.0f * grass_density_scale;
  lod_config.requested_instance_count_min = 0.0f;
  lod_config.requested_instance_count_max = 12000.0f;
  lod_config.instance_budget_min = 48;
  lod_config.instance_budget_max = 520;
  lod_config.source_vertex_count = (float)max_vertex_count;
  lod_config.fallback_vertex_count = 900.0f;
  lod_config.vertex_budget_low = 650000.0f;
  lod_config.vertex_budget_high = 1800000.0f;
  lod_config.cell_size_min = 6.0f;
  lod_config.cell_size_max = 28.0f;

  lod_state = procedural_lod_resolve(quality, &lod_config);
  radius = lod_state.effective_radius;
  effective_grass_target = lod_state.effective_instance_count;
  cell_size = lod_state.cell_size;

  if (effective_grass_target <= 0 || cell_size <= 0.0f) {
    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;
    return 1;
  }

  {
    const int grid_min_x = (int)floorf((camera->x - radius) / cell_size);
    const int grid_max_x = (int)ceilf((camera->x + radius) / cell_size);
    const int grid_min_z = (int)floorf((camera->z - radius) / cell_size);
    const int grid_max_z = (int)ceilf((camera->z + radius) / cell_size);
    const size_t estimated_capacity = (size_t)(grid_max_x - grid_min_x + 1) *
                                      (size_t)(grid_max_z - grid_min_z + 1);
    int variant_index = 0;

    if (palm_render_cache_matches(mesh, grid_min_x, grid_max_x, grid_min_z,
                                  grid_max_z, radius, cell_size, settings)) {
      return 2;
    }

    palm_render_reset_instances(mesh);
    mesh->cache_valid = 0;

    for (variant_index = 0; variant_index < mesh->variant_count;
         ++variant_index) {
      PalmRenderVariant *variant = &mesh->variants[variant_index];
      if (variant->category == PALM_RENDER_CATEGORY_GRASS &&
          !palm_render_reserve_instances(variant, estimated_capacity)) {
        return 0;
      }
    }

    for (grid_z = grid_min_z; grid_z <= grid_max_z; ++grid_z) {
      int grid_x = 0;

      for (grid_x = grid_min_x; grid_x <= grid_max_x; ++grid_x) {
        const float offset_x = palm_render_mix(
            0.18f, 0.82f, palm_render_hash_unit(grid_x, grid_z, 20U));
        const float offset_z = palm_render_mix(
            0.18f, 0.82f, palm_render_hash_unit(grid_x, grid_z, 21U));
        const float coverage = palm_render_hash_unit(grid_x, grid_z, 22U);
        const int patch_x = (int)floorf((float)grid_x * 0.35f);
        const int patch_z = (int)floorf((float)grid_z * 0.35f);
        const float patch = palm_render_hash_unit(patch_x, patch_z, 23U);
        const float variation = palm_render_hash_unit(grid_x, grid_z, 24U);
        const float scale_jitter = palm_render_hash_unit(grid_x, grid_z, 25U);
        const float x = ((float)grid_x + offset_x) * cell_size;
        const float z = ((float)grid_z + offset_z) * cell_size;
        PalmRenderVariant *variant = palm_render_pick_variant(
            mesh, PALM_RENDER_CATEGORY_GRASS, grid_x, grid_z, 27U);
        const float dx = x - camera->x;
        const float dz = z - camera->z;
        const float distance_sq = dx * dx + dz * dz;
        const float slope = palm_render_estimate_slope(x, z, settings);
        const float yaw = palm_render_hash_unit(grid_x, grid_z, 26U) *
                          (k_palm_render_pi * 2.0f);
        const float ground_y = terrain_get_render_height(x, z, settings);
        const float fruit_density =
            palm_render_clamp(settings->palm_fruit_density, 0.0f, 1.0f);
        const float hole_threshold = palm_render_mix(0.05f, 0.24f, patch);
        PalmColor tint = {
            palm_render_mix(0.88f, 1.08f, variation) - fruit_density * 0.02f,
            palm_render_mix(0.92f, 1.16f, scale_jitter) + fruit_density * 0.03f,
            palm_render_mix(0.82f, 1.00f, variation) - fruit_density * 0.04f};
        float desired_height = 0.0f;
        float embed_depth = 0.0f;

        if (variant == NULL || variant->model_height <= 0.0001f) {
          continue;
        }
        if (distance_sq > radius * radius || slope > variant->slope_limit ||
            coverage < hole_threshold) {
          continue;
        }
        if ((size_t)variant->instance_count >= variant->cpu_instance_capacity) {
          continue;
        }

        desired_height =
            palm_render_mix(variant->desired_height_min,
                            variant->desired_height_max, variation) *
            settings->palm_size *
            palm_render_mix(variant->scale_jitter_min,
                            variant->scale_jitter_max, scale_jitter);
        embed_depth = palm_render_mix(variant->embed_depth_min,
                                      variant->embed_depth_max, variation) *
                          settings->palm_size +
                      slope * 0.04f;

        palm_render_build_instance_transform(
            &((PalmInstanceData *)
                  variant->cpu_instances)[variant->instance_count],
            x, ground_y - embed_depth, z,
            desired_height / variant->model_height, yaw, tint);
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

static int palm_render_populate_mountain_instances(
    PalmRenderMesh *mesh, const CameraState *camera,
    const SceneSettings *settings, const RendererQualityProfile *quality) {
  const GLsizei max_vertex_count =
      palm_render_get_max_vertex_count_for_category(
          mesh, PALM_RENDER_CATEGORY_MOUNTAIN);
  const float render_scale = (quality != NULL) ? quality->render_scale : 1.0f;
  const int mountain_count =
      (render_scale <= 0.62f) ? 8 : ((render_scale <= 0.74f) ? 10 : 14);
  const float terrain_step = palm_render_get_terrain_step_for_quality(quality);
  const float ring_radius = k_palm_render_terrain_half_extent + 420.0f;
  float center_x = 0.0f;
  float center_z = 0.0f;
  int center_grid_x = 0;
  int center_grid_z = 0;
  int variant_index = 0;
  int mountain_index = 0;

  if (mesh == NULL || camera == NULL || settings == NULL ||
      !palm_render_has_category(mesh, PALM_RENDER_CATEGORY_MOUNTAIN) ||
      max_vertex_count <= 0) {
    return 1;
  }

  palm_render_get_terrain_origin_from_camera(camera, quality, &center_x,
                                             &center_z);
  center_grid_x = (int)floorf(center_x / terrain_step);
  center_grid_z = (int)floorf(center_z / terrain_step);

  if (palm_render_cache_matches(mesh, center_grid_x, center_grid_x,
                                center_grid_z, center_grid_z, ring_radius,
                                terrain_step, settings)) {
    return 2;
  }

  palm_render_reset_instances(mesh);
  mesh->cache_valid = 0;

  for (variant_index = 0; variant_index < mesh->variant_count;
       ++variant_index) {
    PalmRenderVariant *variant = &mesh->variants[variant_index];
    if (variant->category == PALM_RENDER_CATEGORY_MOUNTAIN &&
        !palm_render_reserve_instances(variant, (size_t)mountain_count)) {
      return 0;
    }
  }

  for (mountain_index = 0; mountain_index < mountain_count; ++mountain_index) {
    const float variation = palm_render_hash_unit(mountain_index, 0, 60U);
    const float scale_jitter = palm_render_hash_unit(mountain_index, 0, 61U);
    const float angle_jitter = palm_render_mix(
        -0.18f, 0.18f, palm_render_hash_unit(mountain_index, 0, 62U));
    const float radius_jitter = palm_render_mix(
        -280.0f, 240.0f, palm_render_hash_unit(mountain_index, 0, 63U));
    const float angle =
        (((float)mountain_index + 0.5f) / (float)mountain_count) *
            (k_palm_render_pi * 2.0f) +
        angle_jitter;
    const float x = center_x + cosf(angle) * (ring_radius + radius_jitter);
    const float z = center_z + sinf(angle) * (ring_radius + radius_jitter);
    const float ground_y = terrain_get_render_height(x, z, settings);
    const float yaw =
        angle + palm_render_mix(-0.40f, 0.40f,
                                palm_render_hash_unit(mountain_index, 0, 64U));
    PalmRenderVariant *variant = palm_render_pick_variant(
        mesh, PALM_RENDER_CATEGORY_MOUNTAIN, mountain_index,
        center_grid_x + center_grid_z, 65U);
    PalmColor tint = {palm_render_mix(0.92f, 1.04f, variation),
                      palm_render_mix(0.88f, 1.00f, scale_jitter),
                      palm_render_mix(0.82f, 0.94f, variation)};
    float desired_height = 0.0f;
    float scale = 1.0f;
    float footprint_radius = 1.0f;
    float lowest_ground_y = ground_y;
    float terrain_drop = 0.0f;
    float embed_depth = 0.0f;

    if (variant == NULL || variant->model_height <= 0.0001f ||
        variant->model_radius <= 0.0001f) {
      continue;
    }
    if ((size_t)variant->instance_count >= variant->cpu_instance_capacity) {
      continue;
    }

    desired_height = palm_render_mix(variant->desired_height_min,
                                     variant->desired_height_max, variation) *
                     palm_render_mix(variant->scale_jitter_min,
                                     variant->scale_jitter_max, scale_jitter);
    scale = desired_height / variant->model_height;
    footprint_radius = variant->model_radius * scale * 0.72f;
    lowest_ground_y = palm_render_sample_lowest_terrain_ring(
        x, z, footprint_radius, settings);
    terrain_drop = ground_y - lowest_ground_y;
    if (terrain_drop < 0.0f) {
      terrain_drop = 0.0f;
    }
    embed_depth = palm_render_mix(variant->embed_depth_min,
                                  variant->embed_depth_max, variation) +
                  desired_height * 0.24f + terrain_drop;

    palm_render_build_instance_transform(
        &((PalmInstanceData *)variant->cpu_instances)[variant->instance_count],
        x, ground_y - embed_depth, z, scale, yaw, tint);
    variant->instance_count += 1;
  }

  mesh->cache_valid = 1;
  mesh->cache_grid_min_x = center_grid_x;
  mesh->cache_grid_max_x = center_grid_x;
  mesh->cache_grid_min_z = center_grid_z;
  mesh->cache_grid_max_z = center_grid_z;
  mesh->cache_radius = ring_radius;
  mesh->cache_cell_size = terrain_step;
  mesh->cache_palm_size = settings->palm_size;
  mesh->cache_palm_count = settings->palm_count;
  mesh->cache_palm_fruit_density = settings->palm_fruit_density;
  mesh->cache_palm_render_radius = settings->palm_render_radius;
  mesh->cache_terrain_base_height = settings->terrain_base_height;
  mesh->cache_terrain_height_scale = settings->terrain_height_scale;
  mesh->cache_terrain_roughness = settings->terrain_roughness;
  mesh->cache_terrain_ridge_strength = settings->terrain_ridge_strength;
  return 1;
}

static float palm_render_clamp(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static float palm_render_mix(float a, float b, float t) {
  return a + (b - a) * t;
}

static float palm_render_get_terrain_step_for_quality(
    const RendererQualityProfile *quality) {
  int terrain_resolution = 257;
  int shadow_terrain_resolution = 0;
  int active_resolution = 257;

  if (quality != NULL) {
    if (quality->terrain_resolution > 2) {
      terrain_resolution = quality->terrain_resolution;
    }
    if (quality->shadow_terrain_resolution > 2) {
      shadow_terrain_resolution = quality->shadow_terrain_resolution;
    }
  }

  active_resolution = (shadow_terrain_resolution > terrain_resolution)
                          ? shadow_terrain_resolution
                          : terrain_resolution;
  if (active_resolution < 3) {
    active_resolution = 257;
  }

  return (k_palm_render_terrain_half_extent * 2.0f) /
         (float)(active_resolution - 1);
}

static void palm_render_get_terrain_origin_from_camera(
    const CameraState *camera, const RendererQualityProfile *quality,
    float *out_x, float *out_z) {
  const float terrain_step = palm_render_get_terrain_step_for_quality(quality);

  if (out_x != NULL) {
    *out_x = (camera != NULL) ? floorf(camera->x / terrain_step) * terrain_step
                              : 0.0f;
  }
  if (out_z != NULL) {
    *out_z = (camera != NULL) ? floorf(camera->z / terrain_step) * terrain_step
                              : 0.0f;
  }
}

static float
palm_render_sample_lowest_terrain_ring(float x, float z, float radius,
                                       const SceneSettings *settings) {
  float lowest_height = terrain_get_render_height(x, z, settings);
  int sample_index = 0;

  if (settings == NULL || radius <= 0.01f) {
    return lowest_height;
  }

  for (sample_index = 0; sample_index < 8; ++sample_index) {
    const float angle =
        ((float)sample_index / 8.0f) * (k_palm_render_pi * 2.0f);
    const float sample_x = x + cosf(angle) * radius;
    const float sample_z = z + sinf(angle) * radius;
    const float sample_height =
        terrain_get_render_height(sample_x, sample_z, settings);
    if (sample_height < lowest_height) {
      lowest_height = sample_height;
    }
  }

  return lowest_height;
}

static float palm_render_hash_unit(int x, int z, unsigned int seed) {
  unsigned int state = (unsigned int)(x * 374761393) ^
                       (unsigned int)(z * 668265263) ^ (seed * 2246822519U);
  state = (state ^ (state >> 13)) * 1274126177U;
  state ^= state >> 16;
  return (float)(state & 0x00FFFFFFU) / (float)0x01000000U;
}

static float palm_render_estimate_slope(float x, float z,
                                        const SceneSettings *settings) {
  const float sample_offset = 3.0f;
  const float x0 = terrain_get_render_height(x - sample_offset, z, settings);
  const float x1 = terrain_get_render_height(x + sample_offset, z, settings);
  const float z0 = terrain_get_render_height(x, z - sample_offset, settings);
  const float z1 = terrain_get_render_height(x, z + sample_offset, settings);
  const float dx = fabsf(x1 - x0) / (sample_offset * 2.0f);
  const float dz = fabsf(z1 - z0) / (sample_offset * 2.0f);

  return dx + dz;
}

static void palm_render_build_instance_transform(PalmInstanceData *instance,
                                                 float x, float y, float z,
                                                 float scale, float yaw_radians,
                                                 PalmColor tint) {
  const float c = cosf(yaw_radians) * scale;
  const float s = sinf(yaw_radians) * scale;

  if (instance == NULL) {
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
