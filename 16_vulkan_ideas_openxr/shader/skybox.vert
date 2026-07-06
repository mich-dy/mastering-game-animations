#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec4 aPos;

layout (location = 0) out vec3 texCoord;

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
  mat4 invViewMat[2];
  mat4 invProjectionMat[2];
};

void main() {
  /* remove translation part from inverse view matrix */
  texCoord = mat3(invViewMat[XR_VIEW_INDEX]) * (invProjectionMat[XR_VIEW_INDEX] * aPos).xyz;

  /* set z to 1.0 to force drawing on the far z plane */
  gl_Position = aPos.xyww;
}
