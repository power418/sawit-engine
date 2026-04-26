const int TERRAIN_CONTACT_PATCH_CAPACITY = 16;

uniform int terrain_contact_count;
uniform vec4 terrain_contact_data[TERRAIN_CONTACT_PATCH_CAPACITY];
uniform vec4 terrain_contact_params[TERRAIN_CONTACT_PATCH_CAPACITY];

float terrain_hash(vec2 p)
{
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float terrain_noise(vec2 p)
{
  vec2 cell = floor(p);
  vec2 local = fract(p);
  vec2 blend = local * local * (3.0 - 2.0 * local);

  return mix(
    mix(terrain_hash(cell + vec2(0.0, 0.0)), terrain_hash(cell + vec2(1.0, 0.0)), blend.x),
    mix(terrain_hash(cell + vec2(0.0, 1.0)), terrain_hash(cell + vec2(1.0, 1.0)), blend.x),
    blend.y
  );
}

float terrain_fbm(vec2 p)
{
  float value = 0.0;
  float amplitude = 0.5;
  mat2 transform = mat2(1.6, 1.2, -1.2, 1.6);

  for (int i = 0; i < 5; ++i)
  {
    value += amplitude * terrain_noise(p);
    p = transform * p;
    amplitude *= 0.5;
  }

  return value;
}

float terrain_contact_height(vec2 world_xz, float base_height)
{
  float height = base_height;

  for (int i = 0; i < TERRAIN_CONTACT_PATCH_CAPACITY; ++i)
  {
    if (i >= terrain_contact_count)
    {
      break;
    }

    vec4 data = terrain_contact_data[i];
    vec4 params = terrain_contact_params[i];
    vec2 patch_delta = world_xz - data.xy;
    float distance_sq = dot(patch_delta, patch_delta);
    float inner_radius = max(data.w, 0.0);
    float outer_radius = max(params.x, inner_radius + 0.01);
    float strength = clamp(params.y, 0.0, 1.0);
    float outer_sq = outer_radius * outer_radius;
    float inner_sq = inner_radius * inner_radius;
    float blend = (1.0 - smoothstep(inner_sq, outer_sq, distance_sq)) * strength;

    height = mix(height, data.z, blend);
  }

  return height;
}

float terrain_base_height(vec2 world_xz)
{
  float height_scale = max(terrain_shape.y, 0.05);
  float roughness = max(terrain_shape.z, 0.1);
  float ridge_strength = max(terrain_shape.w, 0.0);
  float warp_scale = 2.5 + roughness * 1.5;
  float detail_warp_scale = 2.5 + roughness;
  vec2 warp = vec2(
    terrain_fbm(world_xz * (0.0012 * roughness) + vec2(1.7, 9.2)),
    terrain_fbm(world_xz * (0.0012 * roughness) + vec2(-8.3, 2.8))
  );
  vec2 warped = world_xz * (0.0028 * roughness) + warp * warp_scale;
  float broad = terrain_fbm(warped * 0.65);
  float hills = terrain_fbm(warped * 1.7 + warp * 0.7);
  float ridges = 1.0 - abs(terrain_fbm(warped * (2.2 + roughness * 0.3) + vec2(13.7, -9.1)) * 2.0 - 1.0);
  float detail = terrain_fbm(world_xz * (0.012 * roughness) + warp * detail_warp_scale);
  float relief = broad * 34.0 + hills * 9.0 + ridges * (6.0 * ridge_strength) + detail * 1.8;
  return terrain_shape.x + relief * height_scale;
}

float terrain_height(vec2 world_xz)
{
  return terrain_contact_height(world_xz, terrain_base_height(world_xz));
}

vec3 terrain_normal(vec2 world_xz)
{
  const float epsilon = 3.0;
  float left = terrain_height(world_xz - vec2(epsilon, 0.0));
  float right = terrain_height(world_xz + vec2(epsilon, 0.0));
  float back = terrain_height(world_xz - vec2(0.0, epsilon));
  float front = terrain_height(world_xz + vec2(0.0, epsilon));
  return normalize(vec3(left - right, epsilon * 2.0, back - front));
}
