#include "terrain.h"

#include <math.h>
#include <stddef.h>

static TerrainRenderSamplingConfig g_terrain_render_sampling = { 0 };

static float terrain_fract(float value);
static float terrain_hash2(float x, float z);
static float terrain_mix(float a, float b, float t);
static float terrain_smoothstep(float edge0, float edge1, float value);
static float terrain_noise2(float x, float z);
static float terrain_fbm2(float x, float z);

float terrain_get_height(float x, float z, const SceneSettings* settings)
{
  const SceneSettings fallback_settings = scene_settings_default();
  const SceneSettings* active_settings = (settings != NULL) ? settings : &fallback_settings;
  const float height_scale = (active_settings->terrain_height_scale < 0.05f) ? 0.05f : active_settings->terrain_height_scale;
  const float roughness = (active_settings->terrain_roughness < 0.1f) ? 0.1f : active_settings->terrain_roughness;
  const float ridge_strength = (active_settings->terrain_ridge_strength < 0.0f) ? 0.0f : active_settings->terrain_ridge_strength;
  const float warp_scale = 2.5f + roughness * 1.5f;
  const float detail_warp_scale = 2.5f + roughness;
  const float warp_x = terrain_fbm2(x * (0.0012f * roughness) + 1.7f, z * (0.0012f * roughness) + 9.2f);
  const float warp_z = terrain_fbm2(x * (0.0012f * roughness) - 8.3f, z * (0.0012f * roughness) + 2.8f);
  const float warped_x = x * (0.0028f * roughness) + warp_x * warp_scale;
  const float warped_z = z * (0.0028f * roughness) + warp_z * warp_scale;
  const float broad = terrain_fbm2(warped_x * 0.65f, warped_z * 0.65f);
  const float hills = terrain_fbm2(warped_x * 1.7f + warp_x * 0.7f, warped_z * 1.7f + warp_z * 0.7f);
  const float ridges =
    1.0f - fabsf(terrain_fbm2(warped_x * (2.2f + roughness * 0.3f) + 13.7f, warped_z * (2.2f + roughness * 0.3f) - 9.1f) * 2.0f - 1.0f);
  const float detail = terrain_fbm2(x * (0.012f * roughness) + warp_x * detail_warp_scale, z * (0.012f * roughness) + warp_z * detail_warp_scale);
  const float relief = broad * 34.0f + hills * 9.0f + ridges * (6.0f * ridge_strength) + detail * 1.8f;

  return active_settings->terrain_base_height + relief * height_scale;
}

void terrain_set_render_sampling(const TerrainRenderSamplingConfig* config)
{
  if (config == NULL || config->valid == 0 || config->mesh_step <= 0.0f || config->half_extent <= 0.0f)
  {
    g_terrain_render_sampling.origin_x = 0.0f;
    g_terrain_render_sampling.origin_z = 0.0f;
    g_terrain_render_sampling.mesh_step = 0.0f;
    g_terrain_render_sampling.half_extent = 0.0f;
    g_terrain_render_sampling.valid = 0;
    return;
  }

  g_terrain_render_sampling = *config;
}

float terrain_get_render_height(float x, float z, const SceneSettings* settings)
{
  const float total_extent = g_terrain_render_sampling.half_extent * 2.0f;
  const int cell_count =
    (g_terrain_render_sampling.mesh_step > 0.0f)
      ? (int)floorf(total_extent / g_terrain_render_sampling.mesh_step + 0.5f)
      : 0;
  float local_x = 0.0f;
  float local_z = 0.0f;
  int cell_x = 0;
  int cell_z = 0;
  float frac_x = 0.0f;
  float frac_z = 0.0f;
  float x0 = 0.0f;
  float x1 = 0.0f;
  float z0 = 0.0f;
  float z1 = 0.0f;
  float h00 = 0.0f;
  float h10 = 0.0f;
  float h01 = 0.0f;
  float h11 = 0.0f;

  if (g_terrain_render_sampling.valid == 0 || g_terrain_render_sampling.mesh_step <= 0.0f || cell_count <= 0)
  {
    return terrain_get_height(x, z, settings);
  }

  local_x = x - g_terrain_render_sampling.origin_x + g_terrain_render_sampling.half_extent;
  local_z = z - g_terrain_render_sampling.origin_z + g_terrain_render_sampling.half_extent;
  if (local_x < 0.0f || local_z < 0.0f || local_x > total_extent || local_z > total_extent)
  {
    return terrain_get_height(x, z, settings);
  }

  cell_x = (int)floorf(local_x / g_terrain_render_sampling.mesh_step);
  cell_z = (int)floorf(local_z / g_terrain_render_sampling.mesh_step);
  if (cell_x >= cell_count)
  {
    cell_x = cell_count - 1;
    frac_x = 1.0f;
  }
  else
  {
    frac_x = (local_x - (float)cell_x * g_terrain_render_sampling.mesh_step) / g_terrain_render_sampling.mesh_step;
  }
  if (cell_z >= cell_count)
  {
    cell_z = cell_count - 1;
    frac_z = 1.0f;
  }
  else
  {
    frac_z = (local_z - (float)cell_z * g_terrain_render_sampling.mesh_step) / g_terrain_render_sampling.mesh_step;
  }

  x0 = g_terrain_render_sampling.origin_x - g_terrain_render_sampling.half_extent + (float)cell_x * g_terrain_render_sampling.mesh_step;
  x1 = x0 + g_terrain_render_sampling.mesh_step;
  z0 = g_terrain_render_sampling.origin_z - g_terrain_render_sampling.half_extent + (float)cell_z * g_terrain_render_sampling.mesh_step;
  z1 = z0 + g_terrain_render_sampling.mesh_step;
  h00 = terrain_get_height(x0, z0, settings);
  h10 = terrain_get_height(x1, z0, settings);
  h01 = terrain_get_height(x0, z1, settings);
  h11 = terrain_get_height(x1, z1, settings);

  if (frac_x + frac_z <= 1.0f)
  {
    return h00 + frac_x * (h10 - h00) + frac_z * (h01 - h00);
  }

  return h11 + (1.0f - frac_x) * (h01 - h11) + (1.0f - frac_z) * (h10 - h11);
}

static float terrain_fract(float value)
{
  return value - floorf(value);
}

static float terrain_hash2(float x, float z)
{
  return terrain_fract(sinf(x * 127.1f + z * 311.7f) * 43758.5453123f);
}

static float terrain_mix(float a, float b, float t)
{
  return a + (b - a) * t;
}

static float terrain_smoothstep(float edge0, float edge1, float value)
{
  float t = 0.0f;

  if (edge0 == edge1)
  {
    return (value < edge0) ? 0.0f : 1.0f;
  }

  t = (value - edge0) / (edge1 - edge0);
  if (t < 0.0f)
  {
    t = 0.0f;
  }
  else if (t > 1.0f)
  {
    t = 1.0f;
  }

  return t * t * (3.0f - 2.0f * t);
}

static float terrain_noise2(float x, float z)
{
  const float cell_x = floorf(x);
  const float cell_z = floorf(z);
  const float local_x = terrain_fract(x);
  const float local_z = terrain_fract(z);
  const float blend_x = terrain_smoothstep(0.0f, 1.0f, local_x);
  const float blend_z = terrain_smoothstep(0.0f, 1.0f, local_z);
  const float n00 = terrain_hash2(cell_x + 0.0f, cell_z + 0.0f);
  const float n10 = terrain_hash2(cell_x + 1.0f, cell_z + 0.0f);
  const float n01 = terrain_hash2(cell_x + 0.0f, cell_z + 1.0f);
  const float n11 = terrain_hash2(cell_x + 1.0f, cell_z + 1.0f);

  return terrain_mix(terrain_mix(n00, n10, blend_x), terrain_mix(n01, n11, blend_x), blend_z);
}

static float terrain_fbm2(float x, float z)
{
  float value = 0.0f;
  float amplitude = 0.5f;
  int i = 0;

  for (i = 0; i < 5; ++i)
  {
    const float next_x = 1.6f * x + 1.2f * z;
    const float next_z = -1.2f * x + 1.6f * z;
    value += amplitude * terrain_noise2(x, z);
    x = next_x;
    z = next_z;
    amplitude *= 0.5f;
  }

  return value;
}
