#version 460 core
layout (location = 0) in vec4 aPos; // last float is uv.x
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aNormal; // last float is uv.y
layout (location = 3) in uvec4 aBoneNum; // ignored
layout (location = 4) in vec4 aBoneWeight; // ignored

layout (location = 0) out vec4 color;
layout (location = 1) out vec3 position;
layout (location = 2) out vec4 normal;
layout (location = 3) out vec2 texCoord;

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

layout (std430, set = 1, binding = 1) readonly restrict buffer WorldTransformMatrix {
  mat4 worldTransformMat[];
};

void main() {
  position = vec3(mat4(1.0f) * aPos);

  mat4 levelMat = worldTransformMat[worldPosOffset];
  gl_Position = projection * view * levelMat * vec4(aPos.x, aPos.y, aPos.z, 1.0);
  color = aColor;
  normal = transpose(inverse(levelMat)) * vec4(aNormal.x, aNormal.y, aNormal.z, 1.0);
  texCoord = vec2(aPos.w, aNormal.w);
}
