#version 460 core
#extension GL_ARB_shader_viewport_layer_array : enable

layout (location = 0) in vec4 aPos; // last float is uv.x
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum; // ignored
layout (location = 4) in vec4 aBoneWeight; // ignored

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
  int shadowMapLayerIndex;
};

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invProjectionMat;
};

layout (std430, set = 1, binding = 1) readonly restrict buffer WorldTransformMatrix {
  mat4 worldTransformMat[];
};

struct ShadowMapCascadeData {
  mat4 shadowMapMat;
  // vec4 to avoid padding problems
  vec4 shadowMapSplits;
};

layout (std430, set = 1, binding = 2) readonly restrict buffer ShadowMapCascadeParameters {
  ShadowMapCascadeData shadowMapData[];
};

void main() {
  mat4 levelMat = worldTransformMat[worldPosOffset];
  gl_Position = shadowMapData[shadowMapLayerIndex].shadowMapMat * levelMat * vec4(aPos.xyz, 1.0);
  gl_Layer = shadowMapLayerIndex;
}
