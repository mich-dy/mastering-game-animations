#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec4 lineColor;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
};

void main() {
  gl_Position = projectionMat[XR_VIEW_INDEX] * viewMat[XR_VIEW_INDEX] * vec4(aPos, 1.0);
  lineColor = vec4(aColor, 1.0);
}
