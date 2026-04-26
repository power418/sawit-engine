#version 330

in vec3 world_pos;
in vec3 world_normal;
in vec4 light_pos;
in vec3 base_color;
in vec2 base_texcoord;

out vec4 color;

uniform vec3 sun_direction;
uniform float sun_distance_mkm;
uniform vec3 camera_position;
uniform sampler2D shadow_map;
uniform sampler2D diffuse_map;
uniform vec4 terrain_shape;
uniform vec4 environment_settings;

#include "lighting/lighting_quality.glsl"
#include "lighting/lumen_proxy.glsl"
#include "lighting/terrain_surface.glsl"
#include "lighting/raytrace_lighting.glsl"

void main()
{
  vec3 normal = normalize(world_normal);
  vec3 light_dir = normalize(sun_direction);
  float sun_height = light_dir.y;
  float daylight = smoothstep(-0.16, 0.22, sun_height);
  float sun_visibility = smoothstep(-0.06, 0.12, sun_height);
  float moonlight = smoothstep(-0.40, 0.08, -sun_height) * 0.10;
  float direct = max(dot(normal, light_dir), 0.0) * sun_visibility;
  float raytrace_factor = lighting_raytrace_factor();
  float tracer_weight = lighting_trace_weight(distance(camera_position, world_pos));
  float shadow = (direct > 0.0) ? sample_shadow_map_pcf(light_pos, normal, light_dir, shadow_map) : 1.0;
  float distance_factor = clamp(149.6 / max(sun_distance_mkm, 40.0), 0.55, 1.35);
  float sun_energy = mix(0.80, 1.08, distance_factor);
  float distance_to_camera = length(camera_position - world_pos);
  float fog_density = clamp(environment_settings.x, 0.0, 1.0);
  float haze = 1.0 - exp(-distance_to_camera * mix(0.00035, 0.0038, fog_density));
  float far_blend = smoothstep(mix(3800.0, 1700.0, fog_density), mix(6200.0, 3000.0, fog_density), distance_to_camera);
  vec3 sun_color = mix(vec3(1.0, 0.42, 0.20), vec3(1.0, 0.93, 0.84), daylight);
  vec3 night_ambient = vec3(0.008, 0.012, 0.020) + vec3(0.02, 0.03, 0.05) * moonlight;
  vec3 day_ambient = vec3(0.21, 0.25, 0.28);
  vec3 ambient = mix(night_ambient, day_ambient, daylight);
  vec3 haze_color = mix(vec3(0.02, 0.04, 0.08), vec3(0.56, 0.68, 0.82), daylight);
  vec3 view_dir = normalize(camera_position - world_pos);
  float fresnel = pow(clamp(1.0 - max(dot(view_dir, normal), 0.0), 0.0, 1.0), 2.0);
  vec3 rim = mix(vec3(0.01, 0.02, 0.04), vec3(0.05, 0.08, 0.06), daylight) * fresnel * (0.16 + 0.24 * daylight);
  vec3 albedo = base_color * texture(diffuse_map, base_texcoord).rgb;

  if (direct > 0.0 && raytrace_factor > 0.001)
  {
    float raytraced_visibility = raytrace_direct_visibility(light_pos, world_pos, normal, light_dir, shadow_map);
    shadow = mix(shadow, raytraced_visibility, tracer_weight * raytrace_factor);
  }

  float contact_visibility = smoothstep(0.05, 1.0, shadow);
  ambient *= mix(0.70, 1.0, contact_visibility);
  rim *= mix(0.80, 1.0, contact_visibility);

  vec3 lumen_proxy =
    lighting_lumen_skylight(normal, daylight, shadow) +
    lighting_lumen_ground_bounce(albedo, normal, sun_color, daylight) +
    lighting_lumen_sun_bounce(normal, light_dir, sun_color, daylight, shadow);
  lumen_proxy *= mix(0.76, 1.0, contact_visibility);
  vec3 lit = albedo * (ambient + rim + lumen_proxy + sun_color * direct * shadow * (0.09 + daylight * 0.40) * sun_energy);

  lit += albedo * sun_color * direct * (1.0 - shadow) * 0.020 * sun_energy;
  lit += albedo * vec3(0.06, 0.08, 0.12) * moonlight * (0.25 + 0.75 * max(normal.y, 0.0));
  lit = mix(lit, haze_color, clamp(haze * (0.03 + fog_density * 0.08) + far_blend * (0.02 + fog_density * 0.10), 0.0, 0.38));
  color = vec4(max(lit, vec3(0.0)), 1.0);
}
