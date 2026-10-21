#version 460 core

layout (set = 0, binding = 2) uniform sampler2D inputColor;
layout (set = 0, binding = 3) uniform sampler2D inputDepth;
layout (set = 0, binding = 4) uniform sampler2D inputNormal;

layout (set = 0, binding = 6) uniform sampler2D lightSpheres;
layout (set = 0, binding = 7) uniform sampler2D ssao;
layout (set = 0, binding = 8) uniform sampler2D ssaoBlur;
layout (set = 0, binding = 9) uniform sampler2DArray shadowMapDepth;
layout (set = 0, binding = 10) uniform sampler2D shadowMapCombinedDepth;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

layout (constant_id = 0) const int SHADOW_MAP_CASCADE_COUNT = 4;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invProjectionMat;
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
  int shadowMapEnabled;
  int shadowMapPCFEnabled;
  int shadowMapPCFRange;
  int colorCascadeDebugEnabled;
  int numDynamicLights;
};

struct ShadowMapCascadeData {
  mat4 shadowMapMat;
  // vec4 to avoid padding problems
  vec4 shadowMapSplits;
};

layout (std430, set = 0, binding = 1) readonly restrict buffer ShadowMapCascadeParameters {
  ShadowMapCascadeData shadowMapData[];
};

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
};

layout (std430, set = 0, binding = 11) readonly restrict buffer DynamicLights {
  dynamicLight lights[];
};

float toSRGB(float x) {
  if (x <= 0.0031308)
    return 12.92 * x;
  else
    return 1.055 * pow(x, (1.0/2.4)) - 0.055;
}

vec3 sRGB(vec3 c) {
  return vec3(toSRGB(c.x), toSRGB(c.y), toSRGB(c.z));
}

float linearDepth(float depth) {
  return 2.0 * nearPlane / (farPlane + nearPlane - depth * (farPlane - nearPlane));
}

float unlinearizeDepth(float depth) {
  return -(2.0 * nearPlane / depth - farPlane - nearPlane) / (farPlane - nearPlane);
}

vec3 getWorldPosFromDepth(vec2 uv) {
  float depth = 0.0;
  if (farPlane == 0.0) {
    depth = texture(inputDepth, uv).r;
  } else {
    depth = unlinearizeDepth(texture(inputDepth, uv).r);
  }
  vec2 xy = uv * 2.0 - 1.0;
  vec4 pos = vec4(xy, depth, 1.0);
  pos = invProjectionMat * pos;
  pos.xyz /= pos.w;

  return pos.xyz;
}

const mat4 biasMat = mat4(
  0.5, 0.0, 0.0, 0.0,
  0.0, 0.5, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.5, 0.5, 0.0, 1.0);

const float ambientStrength = 0.1;

float calculateShadowFactor(vec4 shadowCoord, vec2 off, uint cascadeIndex) {
  float shadowFactor = 1.0;

  if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0 ) {
    float dist = texture(shadowMapDepth, vec3(shadowCoord.st + off, cascadeIndex)).r;

    if (shadowCoord.w > 0.0 && dist < shadowCoord.z) {
      shadowFactor = ambientStrength;
    }
  }

  return shadowFactor;
}

float calculateShadowFactorPCF(vec4 shadowCoord, uint cascadeIndex) {
  ivec3 texDim = textureSize(shadowMapDepth, 0);
  float dx = shadowMapPCFScale * 1.0 / float(texDim.x);
  float dy = shadowMapPCFScale * 1.0 / float(texDim.y);

  float shadowFactor = 0.0;
  int count = 0;
  int range = 1;

  for (int x = -shadowMapPCFRange; x <= shadowMapPCFRange; x++) {
    for (int y = -shadowMapPCFRange; y <= shadowMapPCFRange; y++) {
      shadowFactor += calculateShadowFactor(shadowCoord, vec2(dx * x, dy * y), cascadeIndex);
      count++;
    }
  }

  return shadowFactor / count;
}

void main() {
  /* Read G-Buffer values from previous sub pass */
  vec3 viewPos = getWorldPosFromDepth(inUV);
  vec3 worldPos = vec3(invViewMat * vec4(getWorldPosFromDepth(inUV), 1.0));
  float fragDepth = viewPos.z;
  vec3 normal = normalize(texture(inputNormal, inUV).rgb * 2.0 - 1.0);
  vec3 albedo = texture(inputColor, inUV).rgb;

  float ao = texture(ssao, inUV).r;
  float aoBlur = texture(ssaoBlur, inUV).r;

  float ssaoValue = (ssaoBlurEnabled == 1) ? aoBlur : ao;

  vec3 ambient = ambientStrength * max(vec3(lightColor), vec3(0.05, 0.05, 0.05)) * albedo.rgb;

  vec3 lightDir = normalize(vec3(lightPos));
  float diff = max(dot(normalize(normal), normalize(vec3(lightDir))), 0.0);
  vec3 diffuse = diff * vec3(lightColor) * albedo.rgb;

  float fogAmount;
  /* Fog makes no sense in orthographic projection */
  if (farPlane == 0.0) {
    fogAmount = 0.0;
  } else {
    fogAmount = 1.0 - clamp(exp(-pow(fogDensity * fragDepth, 2.0)), 0.0, 1.0);
  }
  vec4 fogColor = 0.25 * vec4(vec3(lightColor), 1.0);

  switch (compositeDebug) {
    case 0:
      /* find cascade index of current fragment */
      uint cascadeIndex = 0;
      for(uint i = 0; i < SHADOW_MAP_CASCADE_COUNT - 1; ++i) {
        if(fragDepth < shadowMapData[i].shadowMapSplits.x) {
          cascadeIndex = i + 1;
        }
      }

      vec4 shadowMapPos = biasMat * shadowMapData[cascadeIndex].shadowMapMat * vec4(worldPos, 1.0);
      shadowMapPos /= shadowMapPos.w;

      float shadowFactor = 1.0;
      if (shadowMapEnabled == 1) {
        if (shadowMapPCFEnabled == 1) {
          shadowFactor = calculateShadowFactorPCF(shadowMapPos, cascadeIndex);
        } else {
          shadowFactor = calculateShadowFactor(shadowMapPos, vec2(0.0, 0.0), cascadeIndex);
        }
      }

      vec3 dynamicDiffuse = texture(lightSpheres, inUV).rgb * albedo;

      outColor = mix(vec4(clamp(ambient + diffuse * ssaoValue * shadowFactor + dynamicDiffuse, 0.0, 1.0), 1.0), fogColor, fogAmount);

      if (colorCascadeDebugEnabled == 1) {
        switch (cascadeIndex) {
          case 0:
            outColor.rgb *= vec3(1.0, 0.25, 0.25);
            break;
          case 1:
            outColor.rgb *= vec3(0.25, 1.0, 0.25);
            break;
          case 2:
            outColor.rgb *= vec3(0.25, 0.25, 1.0);
            break;
          case 3:
            outColor.rgb *= vec3(1.0, 1.0, 0.25);
            break;
        }
      }
      outColor.rgb = sRGB(outColor.rgb);

      break;
    case 1:
      outColor = vec4(albedo, 1.0);
      break;
    case 2:
      outColor = vec4(vec3(texture(inputDepth, inUV).r), 1.0);
      break;
    case 3:
      outColor = vec4(normal * 0.5 + 0.5, 1.0);
      break;
    case 4:
      outColor = vec4(worldPos * 0.5 + 0.5, 1.0);
      break;
    case 5:
      outColor = vec4(vec3(ao), 1.0);
      break;
    case 6:
      outColor = vec4(vec3(aoBlur), 1.0);
      break;
    case 7:
      outColor = vec4(texture(lightSpheres, inUV).rgb, 1.0);
      break;
    case 8:
      outColor = vec4(vec3(texture(shadowMapCombinedDepth, inUV).r), 1.0);
      break;
  }
}
