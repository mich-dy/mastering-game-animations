#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

layout (set = 0, binding = 1) uniform sampler2DArray ssaoInputDepth;
layout (set = 0, binding = 2) uniform sampler2DArray ssaoInputNormal;
layout (set = 0, binding = 3) uniform sampler2DArray ssaoColor;

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
  float fogDensity;
  float ssaoRadius;
  float ssaoBias;
  float shadowMapPCFScale;
  int compositeDebug;
  int ssaoBlurEnabled;
  int ssaoExponent;
  int ssaoBlurRadius;
};

vec3 getWorldPosFromDepth(vec2 uv) {
  float depth = texture(ssaoInputDepth, vec3(uv, float(XR_VIEW_INDEX))).r;
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat[XR_VIEW_INDEX] * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

void main() {
  /* just copy the pixels */
  if (ssaoBlurEnabled == 0) {
    outColor = texture(ssaoColor, vec3(inUV, float(XR_VIEW_INDEX))).r;
    return;
  }

  vec2 texelSize = 1.0 / vec2(textureSize(ssaoColor, 0));

  float depth = getWorldPosFromDepth(inUV).z;
  vec3 normal = (viewMat[XR_VIEW_INDEX] * vec4(normalize(texture(ssaoInputNormal, vec3(inUV, float(XR_VIEW_INDEX))).rgb * 2.0 - 1.0), 0.0)).xyz;

  float result = 0.0;
  int sampleCount = 0;
  for (int x = -ssaoBlurRadius; x <= ssaoBlurRadius; x++) {
    for (int y = -ssaoBlurRadius; y <= ssaoBlurRadius; y++)  {
      vec2 offset = vec2(float(x), float(y)) * texelSize;

      float offsetDepth = getWorldPosFromDepth(inUV + offset).z;
      vec3 offsetNormal = (viewMat[XR_VIEW_INDEX] * vec4(normalize(texture(ssaoInputNormal, vec3(inUV + offset, float(XR_VIEW_INDEX))).rgb * 2.0 - 1.0), 0.0)).xyz;

      /* sharper edges */
      if (abs(depth - offsetDepth) < 0.2 && dot(normal, offsetNormal) > 0.85) {
        result += texture(ssaoColor, vec3(inUV + offset, float(XR_VIEW_INDEX))).r;
        ++sampleCount;
      }
    }
  }
  outColor = clamp(result / (float(sampleCount)), 0.0, 1.0);
}
