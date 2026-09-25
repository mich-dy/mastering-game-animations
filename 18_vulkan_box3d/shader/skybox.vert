#version 460 core
layout (location = 0) in vec4 aPos;

layout (location = 0) out vec3 texCoord;

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invProjectionMat;
};

void main() {
  /* remove translation part from inverse view matrix */
  texCoord = mat3(invViewMat) * (invProjectionMat * aPos).xyz;

  /* set z to 1.0 to force drawing on the far z plane */
  gl_Position = aPos.xyww;
}
