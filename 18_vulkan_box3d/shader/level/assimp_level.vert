#version 460 core
layout (location = 0) in vec4 aPos; // last float is uv.x
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum; // ignored
layout (location = 4) in vec4 aBoneWeight; // ignored

layout (location = 0) out vec3 color;
layout (location = 1) out vec3 normal;
layout (location = 2) out vec2 texCoord;

layout (push_constant) uniform Constants {
  uint modelStride;
  uint worldPosOffset;
  uint skinMatrixOffset;
};

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
};

layout (std430, set = 1, binding = 1) readonly restrict buffer WorldTransformMatrix {
  mat4 worldTransformMat[];
};

void main() {
  mat4 levelMat = worldTransformMat[worldPosOffset];

  gl_Position = projectionMat * viewMat * levelMat * vec4(aPos.xyz, 1.0);

  normal = transpose(inverse(mat3(levelMat))) * aNormal.xyz;

  color = aColor.rgb;
  texCoord = vec2(aPos.w, aNormal.w);
}
