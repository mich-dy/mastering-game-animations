#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec4 lineColor;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
};

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
};

layout (std430, set = 0, binding = 1) readonly buffer WorldPosMatrices {
  mat4 worldPosMat[];
};

void main() {
  mat4 modelMat = worldPosMat[gl_InstanceIndex + worldPosOffset];

  gl_Position = projectionMat * viewMat * modelMat * vec4(aPos, 1.0);

  lineColor = vec4(aColor, 1.0);
}
