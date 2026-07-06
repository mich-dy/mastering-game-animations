#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec2 outUV;
layout (location = 1) out flat uint outInstance;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat[2];
  mat4 projectionMat[2];
  mat4 invViewMat[2];
  mat4 invProjectionMat[2];
};

mat3 createScaleMatrix(float s) {
  return mat3 (
    s,  0.0, 0.0,
    0.0,  s, 0.0,
    0.0, 0.0,  s
  );
}

struct dynamicLight {
  vec4 position;
  vec4 rotation;
  vec4 color;
  float distance;
  float maxDistance;
  uint type;
  float cutOff;
  float outerCutOff;
  float constantAttFactor;
  float linearAttFactor;
  float quadraticAttFactor;
  float shadowMapOffset;
  float dummy[3];
};

layout (std430, set = 0, binding = 1) readonly restrict buffer DynamicLights {
  dynamicLight lights[];
};

void main() {
  vec3 sphereCenter = lights[gl_InstanceIndex].position.xyz;
  float radius = lights[gl_InstanceIndex].distance;

  mat3 scaleMat = createScaleMatrix(radius);

  vec4 clipPos = projectionMat[XR_VIEW_INDEX] * viewMat[XR_VIEW_INDEX] * vec4(scaleMat * aPos + sphereCenter, 1.0);
  gl_Position = clipPos;

  outUV = ((clipPos.xy)/clipPos.w) * 0.5 + 0.5;

  outInstance = gl_InstanceIndex;
}
