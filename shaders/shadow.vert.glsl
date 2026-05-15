#version 330 core

layout(location = 0) in vec2 position;

uniform mat4 light_vp;
uniform vec2 terrain_origin;
uniform vec4 terrain_shape;

#include "lighting/terrain_surface.glsl"

void main()
{
  vec2 world_xz = terrain_origin + position;
  gl_Position = light_vp * vec4(world_xz.x, terrain_height(world_xz), world_xz.y, 1.0);
}
