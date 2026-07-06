#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec4 lineColor;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
};

layout (std430, set = 0, binding = 1) readonly restrict buffer SphereData {
  vec4 spheres[];
};

mat3 createScaleMatrix(float s) {
  return mat3 (
    s,  0.0, 0.0,
    0.0,  s, 0.0,
    0.0, 0.0,  s
  );
}

void main() {
  vec3 boneCenter = spheres[gl_InstanceIndex].xyz;
  float radius = spheres[gl_InstanceIndex].w;

  mat3 scaleMat = createScaleMatrix(radius);

  gl_Position = projectionMat[XR_VIEW_INDEX] * viewMat[XR_VIEW_INDEX] * vec4(scaleMat * aPos + boneCenter, 1.0);
  lineColor = vec4(aColor, 1.0);
}
