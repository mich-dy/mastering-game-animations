#version 460 core
layout (location = 0) out vec2 outUV;

out gl_PerVertex
{
  vec4 gl_Position;
};

void main()
{
  outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

  gl_Position = vec4(outUV * 2.0f - 1.0f, 1.0f, 1.0f);
}
