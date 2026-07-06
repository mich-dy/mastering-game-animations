#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec2 inUV;

layout (location = 0) out float outColor;

layout (set = 0, binding = 1) uniform sampler2DArray ssaoInputDepth;
layout (set = 0, binding = 2) uniform sampler2DArray ssaoInputNormal;
layout (set = 0, binding = 3) uniform sampler2D ssaoNoise;

layout (constant_id = 0) const int SSAO_KERNEL_SIZE = 64;

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
};

layout (std140, set = 0, binding = 4) uniform SSAOSamples {
  vec4 samples[SSAO_KERNEL_SIZE];
};

float unlinearizeDepth(float depth) {
  return -(2.0 * nearPlane / depth - farPlane + nearPlane)/  (farPlane - nearPlane);
}

vec3 getWorldPosFromDepth(vec2 uv) {
  float depth = 1.0;
  depth = unlinearizeDepth(texture(ssaoInputDepth, vec3(uv, float(XR_VIEW_INDEX))).r);

  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat[XR_VIEW_INDEX] * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

void main() {
  vec3 fragPos = getWorldPosFromDepth(inUV);
  // set w to zero to nullify translation
  vec3 normal = (viewMat[XR_VIEW_INDEX] * vec4(normalize(texture(ssaoInputNormal, vec3(inUV, float(XR_VIEW_INDEX))).rgb * 2.0 - 1.0), 0.0)).xyz;

  ivec2 texDim = textureSize(ssaoInputDepth, 0).xy;
  ivec2 noiseDim = textureSize(ssaoNoise, 0);
  const float scalingFactor = 0.5;
  const vec2 noiseUV = vec2(float(texDim.x)/float(noiseDim.x),
                            float(texDim.y)/(noiseDim.y)) * inUV * scalingFactor;
  vec3 randomVec = texture(ssaoNoise, noiseUV).xyz;

  vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
  vec3 bitangent = cross(tangent, normal);
  mat3 TBN = mat3(tangent, bitangent, normal);

  float occlusion = 0.0;
  for (int i = 0; i < SSAO_KERNEL_SIZE; i++) {
    vec3 samplePos = TBN * samples[i].xyz;
    samplePos = fragPos + samplePos * ssaoRadius;

    vec4 offset = vec4(samplePos, 1.0);
    offset = projectionMat[XR_VIEW_INDEX] * offset;
    offset.xy /= offset.w;
    offset.xy = offset.xy * 0.5 + 0.5;

    float sampleDepth = getWorldPosFromDepth(offset.xy).z;
    vec3 sampleNormal = (viewMat[XR_VIEW_INDEX] * vec4(normalize(texture(ssaoInputNormal, vec3(offset.xy, float(XR_VIEW_INDEX))).rgb * 2.0 - 1.0), 0.0)).xyz;

    if (dot(sampleNormal, normal) < 0.99) {
      float rangeCheck = smoothstep(0.0, 1.0, ssaoRadius / abs(fragPos.z - sampleDepth));
      occlusion += (sampleDepth >= samplePos.z + ssaoBias ? 1.0 : 0.0) * rangeCheck;
    }
  }

  occlusion = 1.0 - (occlusion / float(SSAO_KERNEL_SIZE));

  outColor = pow(occlusion, ssaoExponent);
}
