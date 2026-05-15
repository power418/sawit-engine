#version 330 core

layout(location = 0) in vec3 position;
layout(location = 3) in vec4 instance_col0;
layout(location = 4) in vec4 instance_col1;
layout(location = 5) in vec4 instance_col2;
layout(location = 6) in vec4 instance_col3;

uniform mat4 light_vp;

void main()
{
  mat4 model = mat4(instance_col0, instance_col1, instance_col2, instance_col3);
  gl_Position = light_vp * model * vec4(position, 1.0);
}
