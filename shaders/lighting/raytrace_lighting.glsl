float sample_shadow_map_pcf(vec4 light_pos_value, vec3 normal, vec3 light_dir, sampler2D shadow_map_tex)
{
  const vec2 poisson_disk[16] = vec2[](
    vec2(-0.326, -0.406),
    vec2(-0.840, -0.074),
    vec2(-0.696, 0.457),
    vec2(-0.203, 0.621),
    vec2(0.962, -0.195),
    vec2(0.473, -0.480),
    vec2(0.519, 0.767),
    vec2(0.185, -0.893),
    vec2(0.507, 0.064),
    vec2(0.896, 0.412),
    vec2(-0.322, -0.933),
    vec2(-0.792, -0.598),
    vec2(-0.094, -0.182),
    vec2(0.211, 0.295),
    vec2(-0.443, 0.204),
    vec2(0.678, -0.742)
  );
  const float poisson_weight[16] = float[](1.0, 0.92, 0.88, 0.86, 1.0, 0.92, 0.84, 0.82, 0.78, 0.74, 0.70, 0.68, 0.96, 0.90, 0.76, 0.72);
  vec3 projected = light_pos_value.xyz / light_pos_value.w;
  projected = projected * 0.5 + 0.5;

  if (projected.z > 1.0 || projected.z < 0.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0)
  {
    return 1.0;
  }

  float visibility = 0.0;
  float total_weight = 0.0;
  float normal_light = max(dot(normal, light_dir), 0.0);
  float slope = 1.0 - normal_light;
  vec2 texel = 1.0 / vec2(textureSize(shadow_map_tex, 0));
  float bias = max(0.00022, 0.00092 * slope + 0.50 * max(texel.x, texel.y));
  float blocker_depth = 0.0;
  float blocker_count = 0.0;
  float blocker_search_radius = mix(1.2, 3.8, clamp(slope, 0.0, 1.0));
  float kernel_radius = 1.1;
  int tap_index = 0;

  for (tap_index = 0; tap_index < 8; ++tap_index)
  {
    vec2 sample_offset = poisson_disk[tap_index] * texel * blocker_search_radius;
    float sample_depth = texture(shadow_map_tex, projected.xy + sample_offset).r;
    if (sample_depth < projected.z - bias)
    {
      blocker_depth += sample_depth;
      blocker_count += 1.0;
    }
  }

  if (blocker_count > 0.5)
  {
    float average_blocker = blocker_depth / blocker_count;
    float depth_gap = clamp((projected.z - average_blocker) * 240.0, 0.0, 1.0);
    float blocker_ratio = blocker_count / 8.0;
    kernel_radius = mix(1.0, mix(1.4, 4.8, depth_gap), blocker_ratio);
    kernel_radius *= mix(1.0, 1.55, clamp(slope, 0.0, 1.0));
  }

  for (tap_index = 0; tap_index < 16; ++tap_index)
  {
    vec2 sample_offset = poisson_disk[tap_index] * texel * kernel_radius;
    float sample_depth = texture(shadow_map_tex, projected.xy + sample_offset).r;
    float weight = poisson_weight[tap_index];
    float depth_delta = sample_depth - (projected.z - bias);
    float depth_feather = max(0.00018, 0.35 * max(texel.x, texel.y) + kernel_radius * 0.000025);
    visibility += smoothstep(-depth_feather, depth_feather, depth_delta) * weight;
    total_weight += weight;
  }

  return clamp(visibility / max(total_weight, 0.0001), 0.0, 1.0);
}

float raytrace_heightfield_shadow(vec3 world_pos_value, vec3 light_dir)
{
  vec2 light_xz = light_dir.xz;
  float xz_length = length(light_xz);
  vec2 ray_xz_dir = vec2(0.0);
  float horizon_block = 0.0;
  float low_sun_softness = 1.0 - smoothstep(0.08, 0.62, light_dir.y);

  if (light_dir.y <= -0.02)
  {
    return 0.0;
  }

  if (xz_length < 0.0001)
  {
    return 1.0;
  }

  ray_xz_dir = light_xz / xz_length;

  for (int i = 0; i < 12; ++i)
  {
    float step_t = (float(i) + 0.5) / 12.0;
    float distance = mix(5.0, 260.0, step_t * step_t);
    vec2 probe_xz = world_pos_value.xz + ray_xz_dir * distance;
    float ray_height = world_pos_value.y + 1.25 + light_dir.y / xz_length * distance;
    float terrain_y = terrain_height(probe_xz);
    float clearance = ray_height - terrain_y;
    float soft_width = mix(0.9, 8.0, step_t) + low_sun_softness * 3.0;
    float blocker = 1.0 - smoothstep(0.0, soft_width, clearance);
    float distance_weight = 1.0 - smoothstep(170.0, 270.0, distance);
    horizon_block = max(horizon_block, blocker * distance_weight);
  }

  return 1.0 - clamp(horizon_block, 0.0, 1.0);
}

float raytrace_direct_visibility(vec4 light_pos_value, vec3 world_pos_value, vec3 normal, vec3 light_dir, sampler2D shadow_map_tex)
{
  float shadow_map_visibility = sample_shadow_map_pcf(light_pos_value, normal, light_dir, shadow_map_tex);
  float heightfield_visibility = raytrace_heightfield_shadow(world_pos_value, light_dir);
  return shadow_map_visibility * mix(0.55, 1.0, heightfield_visibility);
}
