#version 330 core

out vec3 pos;

uniform mat4 P;
uniform mat4 V;

const vec2 data[4] = vec2[](
  vec2(-1.0, 1.0),
  vec2(-1.0, -1.0),
  vec2(1.0, 1.0),
  vec2(1.0, -1.0)
);

void main()
{
  gl_Position = vec4(data[gl_VertexID], 0.0, 1.0);
  pos = transpose(mat3(V)) * (inverse(P) * gl_Position).xyz;
}
