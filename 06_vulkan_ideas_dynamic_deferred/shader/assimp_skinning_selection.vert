#version 460 core
layout (location = 0) in vec4 aPos; // last float is uv.x :)
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum;
layout (location = 4) in vec4 aBoneWeight;

layout (location = 0) out vec4 color;
layout (location = 1) out vec3 position;
layout (location = 2) out vec4 normal;
layout (location = 3) out vec2 texCoord;
layout (location = 4) out float selectInfo;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
};

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 view;
  mat4 projection;
  vec4 lightPos;
  vec4 lightColor;
  float fogDensity;
};

layout (std430, set = 1,  binding = 1) readonly restrict buffer BoneMatrices {
  mat4 boneMat[];
};

layout (std430, set = 1, binding = 2) readonly restrict buffer WorldPosMatrices {
  mat4 worldPos[];
};

layout (std430, set = 1, binding = 3) readonly restrict buffer InstanceSelected {
  vec2 selected[];
};

void main() {
  uint skinMatOffset = gl_InstanceIndex * modelStride + skinMatrixOffset;

  mat4 skinMat =
  aBoneWeight.x * boneMat[aBoneNum.x + skinMatOffset] +
  aBoneWeight.y * boneMat[aBoneNum.y + skinMatOffset] +
  aBoneWeight.z * boneMat[aBoneNum.z + skinMatOffset] +
  aBoneWeight.w * boneMat[aBoneNum.w + skinMatOffset];

  mat4 worldPosSkinMat = worldPos[gl_InstanceIndex + worldPosOffset] * skinMat;

  position = vec3(mat4(1.0f) * aPos);
  gl_Position = projection * view * worldPosSkinMat * vec4(aPos.x, aPos.y, aPos.z, 1.0);

  color = aColor * selected[gl_InstanceIndex + worldPosOffset].x;
  /* draw the instance always on top when highlighted, helps to find it better */
  if (selected[gl_InstanceIndex + worldPosOffset].x != 1.0f) {
    gl_Position.z -= 1.0f;
  }

  normal = transpose(inverse(worldPosSkinMat)) * vec4(aNormal.x, aNormal.y, aNormal.z, 1.0);
  texCoord = vec2(aPos.w, aNormal.w);

  /* we need vertex id only (z -> y) */
  selectInfo = selected[gl_InstanceIndex + worldPosOffset].y;
}
