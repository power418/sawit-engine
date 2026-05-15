#version 330 core

layout(location = 0) in vec2 position;

out vec3 world_pos;
out vec3 world_normal;
out vec4 light_pos;

uniform mat4 P;
uniform mat4 V;
uniform mat4 light_vp;
uniform vec2 terrain_origin;
uniform vec4 terrain_shape;

#include "lighting/terrain_surface.glsl"

void main()
{
  vec2 world_xz = terrain_origin + position;
  vec4 world = vec4(world_xz.x, terrain_height(world_xz), world_xz.y, 1.0);
  world_pos = world.xyz;
  world_normal = terrain_normal(world_xz);
  light_pos = light_vp * world;
  gl_Position = P * V * world;
}
