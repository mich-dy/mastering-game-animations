#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec4 color;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
};

void main() {
  gl_Position = projectionMat * viewMat * vec4(aPos.xyz, 1.0);
  color = vec4(aColor, 0.3);
}
