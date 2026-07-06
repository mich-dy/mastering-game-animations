#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec4 color;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out float outDepth;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
  int shadowMapLayerIndex;
  uint virtMaskLayer;
};

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
  mat4 invViewMat[2];
  mat4 invProjectionMat[2];
  vec4 cameraPos;
  vec4 lightPos;
  vec4 lightColor;
  float nearPlane;
  float farPlane;
};

float linearDepth(float depth) {
  return 2.0 * nearPlane / (farPlane + nearPlane - depth * (farPlane - nearPlane));
}

void main() {
  if (XR_VIEW_INDEX != virtMaskLayer) {
    discard;
  }

  FragColor = color;

  gl_FragDepth = linearDepth(-1.0);
  outDepth = linearDepth(gl_FragCoord.z);
}
