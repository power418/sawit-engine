#version 330

in vec3 world_pos;
in vec3 world_normal;
in vec4 light_pos;

out vec4 color;

uniform vec3 sun_direction;
uniform float sun_distance_mkm;
uniform vec3 camera_position;
uniform sampler2D shadow_map;
uniform vec4 terrain_shape;
uniform vec4 environment_settings;

#include "lighting/lighting_quality.glsl"
#include "lighting/lumen_proxy.glsl"
#include "lighting/terrain_surface.glsl"
#include "lighting/raytrace_lighting.glsl"
#include "lighting/pathtrace_lighting.glsl"

void main()
{
  vec3 normal = normalize(world_normal);
  vec3 light_dir = normalize(sun_direction);
  float direct = max(dot(normal, light_dir), 0.0);
  float daylight = smoothstep(-0.18, 0.25, light_dir.y);
  float distance_factor = clamp(149.6 / max(sun_distance_mkm, 40.0), 0.55, 1.35);
  float sun_energy = mix(0.80, 1.08, distance_factor);
  float distance_to_camera = length(camera_position - world_pos);
  float raytrace_factor = lighting_raytrace_factor();
  float pathtrace_factor = lighting_pathtrace_factor();
  float tracer_weight = lighting_trace_weight(distance_to_camera);
  float shadow = (direct > 0.0) ? sample_shadow_map_pcf(light_pos, normal, light_dir, shadow_map) : 1.0;
  float combined_visibility = shadow;
  float pathtrace_ao = 1.0;
  float height_blend = clamp(world_pos.y * 0.08 + 0.5, 0.0, 1.0);
  float grass_mask = smoothstep(-2.5, 6.0, world_pos.y) * (0.45 + 0.55 * normal.y);
  float macro = 0.5 + 0.5 * sin(world_pos.x * 0.003) * cos(world_pos.z * 0.0037);
  vec3 soil = mix(vec3(0.18, 0.13, 0.10), vec3(0.42, 0.34, 0.23), height_blend);
  vec3 grass = mix(vec3(0.16, 0.24, 0.13), vec3(0.34, 0.46, 0.24), height_blend);
  vec3 albedo = mix(soil, grass, grass_mask) * mix(0.88, 1.14, macro);
  vec3 sun_color = mix(vec3(1.0, 0.46, 0.22), vec3(1.0, 0.93, 0.84), daylight);
  vec3 ambient = mix(vec3(0.05, 0.07, 0.11), vec3(0.28, 0.32, 0.36), daylight) * mix(0.62, 1.0, pathtrace_ao);
  vec3 rim = mix(vec3(0.02, 0.03, 0.05), vec3(0.09, 0.10, 0.08), daylight) * pow(max(1.0 - normal.y, 0.0), 1.8);
  float fog_density = clamp(environment_settings.x, 0.0, 1.0);
  float haze = 1.0 - exp(-distance_to_camera * mix(0.00035, 0.0038, fog_density));
  float far_blend = smoothstep(mix(3800.0, 1700.0, fog_density), mix(6200.0, 3000.0, fog_density), distance_to_camera);
  vec3 haze_color = mix(vec3(0.02, 0.04, 0.08), vec3(0.56, 0.68, 0.82), daylight);
  vec3 pathtrace_indirect = vec3(0.0);
  vec3 lumen_proxy = vec3(0.0);

  if (direct > 0.0 && raytrace_factor > 0.001)
  {
    float raytraced_visibility = raytrace_direct_visibility_from_shadow(shadow, world_pos, light_dir);
    combined_visibility = mix(shadow, raytraced_visibility, tracer_weight * raytrace_factor);
  }

  if (pathtrace_factor > 0.001)
  {
    pathtrace_ao = mix(1.0, pathtrace_ambient_occlusion(world_pos, normal), tracer_weight * pathtrace_factor);
    ambient = mix(vec3(0.05, 0.07, 0.11), vec3(0.28, 0.32, 0.36), daylight) * mix(0.62, 1.0, pathtrace_ao);
    pathtrace_indirect = pathtrace_indirect_bounce(world_pos, normal, albedo, light_dir, sun_color, daylight) * tracer_weight * pathtrace_factor;
  }

  float contact_visibility = smoothstep(0.05, 1.0, combined_visibility);
  ambient *= mix(0.66, 1.0, contact_visibility);
  rim *= mix(0.78, 1.0, contact_visibility);
  pathtrace_indirect *= mix(0.80, 1.0, contact_visibility);

  lumen_proxy =
    lighting_lumen_skylight(normal, daylight, pathtrace_ao) +
    lighting_lumen_ground_bounce(albedo, normal, sun_color, daylight) +
    lighting_lumen_sun_bounce(normal, light_dir, sun_color, daylight, combined_visibility);
  lumen_proxy *= mix(1.0, 0.58, tracer_weight * pathtrace_factor);
  lumen_proxy *= mix(0.72, 1.0, contact_visibility);

  vec3 lit = albedo * (ambient + rim + pathtrace_indirect + lumen_proxy + sun_color * direct * combined_visibility * (0.10 + daylight * 0.36) * sun_energy);

  lit += albedo * sun_color * direct * (1.0 - combined_visibility) * 0.020 * sun_energy;
  lit = mix(lit, haze_color, clamp(haze * (0.02 + fog_density * 0.08) + far_blend * (0.02 + fog_density * 0.10), 0.0, 0.36));
  color = vec4(max(lit, vec3(0.0)), 1.0);
}
