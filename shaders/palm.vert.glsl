#version 330

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color_in;
layout(location = 8) in vec2 texcoord_in;
layout(location = 3) in vec4 instance_col0;
layout(location = 4) in vec4 instance_col1;
layout(location = 5) in vec4 instance_col2;
layout(location = 6) in vec4 instance_col3;
layout(location = 7) in vec4 instance_tint;

out vec3 world_pos;
out vec3 world_normal;
out vec4 light_pos;
out vec3 base_color;
out vec2 base_texcoord;

uniform mat4 P;
uniform mat4 V;
uniform mat4 light_vp;

void main()
{
  mat4 model = mat4(instance_col0, instance_col1, instance_col2, instance_col3);
  vec4 world = model * vec4(position, 1.0);
  world_pos = world.xyz;
  world_normal = normalize(mat3(model) * normal);
  light_pos = light_vp * world;
  base_color = color_in * instance_tint.rgb;
  base_texcoord = texcoord_in;
  gl_Position = P * V * world;
}
