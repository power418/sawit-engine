#version 330 core

in vec3 pos;

out vec4 color;

uniform float cloud_time;
uniform vec3 sun_direction;
uniform float sun_distance_mkm;
uniform float cirrus;
uniform float cumulus;
uniform vec4 cloud_settings;
uniform float sky_quality;

float hash(float n)
{
  return fract(sin(n) * 43758.5453123);
}

float noise(vec3 x)
{
  vec3 f = fract(x);
  float n = dot(floor(x), vec3(1.0, 157.0, 113.0));
  return mix(
    mix(
      mix(hash(n + 0.0), hash(n + 1.0), f.x),
      mix(hash(n + 157.0), hash(n + 158.0), f.x),
      f.y
    ),
    mix(
      mix(hash(n + 113.0), hash(n + 114.0), f.x),
      mix(hash(n + 270.0), hash(n + 271.0), f.x),
      f.y
    ),
    f.z
  );
}

const mat3 m = mat3(0.0, 1.60, 1.20, -1.6, 0.72, -0.96, -1.2, -0.96, 1.28);

float fbm(vec3 p)
{
  float f = 0.0;
  f += noise(p) / 2.0;
  p = m * p * 1.1;
  f += noise(p) / 4.0;
  p = m * p * 1.2;
  f += noise(p) / 6.0;
  p = m * p * 1.3;
  f += noise(p) / 12.0;
  p = m * p * 1.4;
  f += noise(p) / 24.0;
  return f;
}

#include "atmosphere/sky_foundation.glsl"
#include "atmosphere/cloud_raymarch.glsl"

void main()
{
  vec3 view_dir = normalize(pos);
  vec3 fsun = normalize(sun_direction);
  float sky_daylight = clamp(fsun.y * 0.5 + 0.5, 0.0, 1.0);
  float below_horizon = clamp(-pos.y, 0.0, 1.0);
  float ground_horizon = exp(-below_horizon * 18.0);
  float pre_dawn = smoothstep(-0.35, -0.02, fsun.y) * (1.0 - smoothstep(-0.02, 0.10, fsun.y));

  if (pos.y < 0.0)
  {
    vec3 ground_base = mix(vec3(0.004, 0.006, 0.010), vec3(0.022, 0.024, 0.025), sky_daylight);
    vec3 horizon_color = mix(vec3(0.030, 0.045, 0.070), vec3(0.26, 0.31, 0.34), sky_daylight);
    horizon_color += vec3(0.18, 0.10, 0.06) * pre_dawn * 0.45;
    color.rgb = mix(ground_base, horizon_color, pow(ground_horizon, 0.8));
    color.a = 1.0;
    return;
  }

  float horizon = pow(max(1.0 - pos.y, 0.0), 3.0);
  color.rgb = sky_foundation_atmosphere(view_dir, fsun, sky_daylight);

  float dusk = exp(-abs(fsun.y) * 14.0);
  float fajr = smoothstep(-0.32, -0.02, fsun.y) * (1.0 - smoothstep(-0.02, 0.12, fsun.y));
  float distance_factor = clamp(149.6 / max(sun_distance_mkm, 40.0), 0.55, 1.35);
  float sun_disk = sky_foundation_sun_disk(view_dir, fsun, distance_factor);
  float sun_halo = sky_foundation_sun_halo(view_dir, fsun, distance_factor);
  float sun_visibility = sky_foundation_sun_visibility(fsun);
  vec3 dusk_tint = mix(vec3(1.0, 0.42, 0.12), vec3(1.0, 0.68, 0.35), clamp(fsun.y * 0.5 + 0.5, 0.0, 1.0));
  vec3 fajr_tint = mix(vec3(0.08, 0.16, 0.34), vec3(0.98, 0.52, 0.18), smoothstep(-0.2, 0.02, fsun.y));
  float clouds_enabled = cloud_settings.x;
  float cloud_amount = clamp(cloud_settings.y, 0.0, 1.0);
  float cloud_spacing = max(cloud_settings.z, 0.45);

  color.rgb += dusk_tint * horizon * dusk * 0.10;
  color.rgb += fajr_tint * horizon * fajr * 0.12;
  color.rgb += vec3(1.0, 0.92, 0.80) * sun_halo * sun_visibility * 0.0012;
  color.rgb = mix(
    color.rgb,
    vec3(0.98, 0.96, 0.90),
    sun_disk * sun_visibility * mix(0.006, 0.012, distance_factor));

  if (clouds_enabled > 0.5 && sky_quality > 0.5)
  {
    float cirrus_density = smoothstep(1.0 - cirrus, 1.0, fbm(pos.xyz / pos.y * 2.0 + cloud_time * 0.05)) * 0.3;
    color.rgb = mix(color.rgb, mix(vec3(0.10, 0.13, 0.19), vec3(0.78, 0.86, 0.94), sky_daylight), cirrus_density * max(pos.y, 0.0) * 0.46);
  }

  if (clouds_enabled > 0.5)
  {
    vec4 raymarched_clouds = raymarch_cloud_layer(
      view_dir,
      fsun,
      mix(vec3(1.0, 0.76, 0.62), vec3(1.0, 0.96, 0.90), sky_daylight),
      sky_daylight,
      cloud_time,
      cloud_amount,
      cloud_spacing,
      sky_quality
    );
    color.rgb = mix(color.rgb, color.rgb + raymarched_clouds.rgb, clamp(raymarched_clouds.a, 0.0, 0.72));
  }

  color.rgb += noise(pos * 1000.0) * mix(0.004, 0.01, sky_quality);
  color.rgb = min(color.rgb, vec3(0.94));
  color.rgb = max(color.rgb, vec3(0.0));
  color.a = 1.0;
}
