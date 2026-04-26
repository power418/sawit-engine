#version 330

in vec2 UV;

out vec4 color;

uniform sampler2D tex[2];
uniform vec4 post_quality;

#include "post/fxaa.glsl"

void main()
{
  vec2 texel_size = 1.0 / vec2(textureSize(tex[0], 0));
  vec3 scene_color = (post_quality.y < 0.86)
    ? texture(tex[0], UV).rgb
    : post_apply_fxaa(UV, texel_size, post_quality.y);

  color = vec4(scene_color, 1.0);
  if (post_quality.x > 0.5)
  {
    float depth = texture(tex[1], UV).r;
    vec2 r = 3.0 / vec2(textureSize(tex[0], 0));
    float occlusion = 0.0;

    for (int i = -1; i <= 1; ++i)
    {
      for (int j = -1; j <= 1; ++j)
      {
        float sample_depth = texture(tex[1], UV + vec2(i, j) * r).r;
        float depth_delta = min(depth - sample_depth, 0.0);
        occlusion += 1.0 / (1.0 + pow(8.0 * depth_delta, 2.0));
      }
    }

    occlusion /= 9.0;
    color.rgb *= mix(1.0, occlusion, 0.22);
  }

  color.rgb = pow(1.0 - exp(-1.16 * color.rgb), vec3(0.96));
}
