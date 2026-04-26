#include "../atmosphere/sky_foundation.glsl"

mat3 build_shading_basis(vec3 normal)
{
  vec3 tangent = normalize(
    (abs(normal.y) < 0.999) ? cross(vec3(0.0, 1.0, 0.0), normal) : cross(vec3(1.0, 0.0, 0.0), normal)
  );
  vec3 bitangent = normalize(cross(normal, tangent));
  return mat3(tangent, bitangent, normal);
}

vec3 sample_pathtrace_sky(vec3 direction, vec3 light_dir, vec3 sun_color, float daylight)
{
  vec3 radiance = sky_foundation_atmosphere(direction, light_dir, daylight);
  float distance_factor = 1.0;
  float sun_disk = sky_foundation_sun_disk(direction, light_dir, distance_factor);
  float sun_halo = sky_foundation_sun_halo(direction, light_dir, distance_factor);
  float sun_visibility = sky_foundation_sun_visibility(light_dir);
  radiance += sun_color * sun_halo * sun_visibility * 0.0009;
  radiance = mix(radiance, radiance + sun_color * 0.10, sun_disk * sun_visibility * 0.08);
  return radiance;
}

float pathtrace_heightfield_visibility(vec3 origin, vec3 direction, float max_distance)
{
  vec2 dir_xz = direction.xz;
  float xz_length = length(dir_xz);
  float occlusion = 0.0;

  if (direction.y <= -0.25)
  {
    return 0.0;
  }

  if (xz_length < 0.0001)
  {
    return 1.0;
  }

  for (int i = 0; i < 4; ++i)
  {
    float distance = max_distance * (0.18 + 0.18 * float(i));
    vec2 probe_xz = origin.xz + dir_xz / xz_length * distance;
    float ray_height = origin.y + direction.y / xz_length * distance;
    float terrain_y = terrain_base_height(probe_xz);
    occlusion = max(occlusion, smoothstep(ray_height - 2.0, ray_height + 4.5, terrain_y));
  }

  return 1.0 - clamp(occlusion, 0.0, 1.0);
}

float pathtrace_ambient_occlusion(vec3 world_pos_value, vec3 normal)
{
  const vec3 local_samples[4] = vec3[4](
    vec3(0.00, 0.92, 0.38),
    vec3(0.62, 0.72, 0.31),
    vec3(-0.58, 0.74, 0.33),
    vec3(0.12, 0.68, -0.72)
  );
  mat3 basis = build_shading_basis(normal);
  float visibility = 0.0;

  for (int i = 0; i < 4; ++i)
  {
    vec3 direction = normalize(basis * local_samples[i]);
    visibility += pathtrace_heightfield_visibility(world_pos_value + normal * 1.2, direction, 110.0);
  }

  return visibility / 4.0;
}

vec3 pathtrace_indirect_bounce(vec3 world_pos_value, vec3 normal, vec3 albedo, vec3 light_dir, vec3 sun_color, float daylight)
{
  const vec3 local_samples[4] = vec3[4](
    vec3(0.00, 0.88, 0.48),
    vec3(0.55, 0.76, 0.35),
    vec3(-0.52, 0.78, 0.34),
    vec3(0.18, 0.70, -0.69)
  );
  mat3 basis = build_shading_basis(normal);
  vec3 bounce = vec3(0.0);

  for (int i = 0; i < 4; ++i)
  {
    vec3 direction = normalize(basis * local_samples[i]);
    float visibility = pathtrace_heightfield_visibility(world_pos_value + normal * 1.4, direction, 140.0);
    float cosine_weight = max(dot(normal, direction), 0.0);
    vec3 sky_radiance = sample_pathtrace_sky(direction, light_dir, sun_color, daylight);
    vec3 terrain_bounce = mix(vec3(0.02, 0.02, 0.015), albedo * sun_color * 0.22, daylight);
    bounce += (sky_radiance + terrain_bounce) * visibility * cosine_weight;
  }

  return bounce * 0.30;
}
