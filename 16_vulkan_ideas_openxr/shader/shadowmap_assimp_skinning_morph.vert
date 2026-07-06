#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec4 aPos; // last float is uv.x :)
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum;
layout (location = 4) in vec4 aBoneWeight;

struct MorphVertex {
  vec4 position;
  vec4 normal;
};

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

layout (std430, set = 1, binding = 1) readonly restrict buffer BoneMatrices {
  mat4 boneMat[];
};

layout (std430, set = 1, binding = 2) readonly restrict buffer WorldPosMatrices {
  mat4 worldPosMat[];
};

layout (std430, set = 1, binding = 3) readonly restrict buffer InstanceSelected {
  vec2 selected[];
};

layout (std430, set = 1, binding = 4) readonly restrict buffer AnimMorphData {
  vec4 vertsPerMorphAnim[];
};

struct ShadowMapCascadeData {
  mat4 shadowMapMat;
  // vec4 to avoid padding problems
  vec4 shadowMapSplits;
};

layout (std430, set = 1, binding = 5) readonly restrict buffer ShadowMapCascadeParameters {
  ShadowMapCascadeData shadowMapData[];
};

layout (std430, set = 2, binding = 0) readonly restrict buffer AnimMorphBuffer {
  MorphVertex morphVertices[];
};

void main() {
  uint skinMatOffset = gl_InstanceIndex * modelStride + skinMatrixOffset;

  mat4 skinMat =
    aBoneWeight.x * boneMat[aBoneNum.x + skinMatOffset] +
    aBoneWeight.y * boneMat[aBoneNum.y + skinMatOffset] +
    aBoneWeight.z * boneMat[aBoneNum.z + skinMatOffset] +
    aBoneWeight.w * boneMat[aBoneNum.w + skinMatOffset];

  mat4 worldPosSkinMat = worldPosMat[gl_InstanceIndex + worldPosOffset] * skinMat;

  /* y and z data contain the offset into the morph anim buffer */
  int morphAnimIndex = int(vertsPerMorphAnim[gl_InstanceIndex + worldPosOffset].y *
    vertsPerMorphAnim[gl_InstanceIndex + worldPosOffset].z);

  vec4 origVertex = vec4(aPos.xyz, 1.0);
  vec4 morphVertex = vec4(morphVertices[gl_VertexIndex + morphAnimIndex].position.xyz, 1.0);

  gl_Position = shadowMapData[shadowMapLayerIndex + XR_VIEW_INDEX].shadowMapMat * worldPosSkinMat *
    mix(origVertex, morphVertex, vertsPerMorphAnim[gl_InstanceIndex + worldPosOffset].x);
}
