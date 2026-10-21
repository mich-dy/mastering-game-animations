#version 460 core
layout (location = 0) in vec2 inUV;
layout (location = 1) in flat uint inInstance;

layout (location = 0) out vec4 FragColor;

layout (set = 0, binding = 2) uniform sampler2D inputDepth;
layout (set = 0, binding = 3) uniform sampler2D inputNormal;

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
};

struct dynamicLight {
  vec4 position;
  vec4 rotation;
  vec4 color;
  float lightDistance;
  float maxDistance;
  uint type;
  float cutOff;
  float outerCutOff;
  float constantAttFactor;
  float linearAttFactor;
  float quadraticAttFactor;
};

layout (std430, set = 0, binding = 1) readonly restrict buffer DynamicLights {
  dynamicLight lights[];
};

const float ambientStrength = 0.1;

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

void main() {
  vec3 lightDynDiff = vec3(0.0);

  vec3 viewPos = getWorldPosFromDepth(inUV);
  vec3 worldPos = vec3(invViewMat * vec4(getWorldPosFromDepth(inUV), 1.0));
  vec3 normal = normalize(texture(inputNormal, inUV).rgb * 2.0 - 1.0);

  vec3 lightPos = lights[inInstance].position.xyz;
  vec3 lightDir = normalize(lightPos - worldPos);
  float diff = max(dot(normal, lightDir), 0.0);

  float dist = length(lightPos - worldPos);
  if (dist < lights[inInstance].maxDistance) {
    float attenuation = 1.0 / (lights[inInstance].constantAttFactor + lights[inInstance].linearAttFactor * dist + lights[inInstance].quadraticAttFactor * (dist * dist));

    lightDynDiff = vec3(lights[inInstance].color) * diff * attenuation;
    if (lights[inInstance].type == 1) {
      float theta = dot(lightDir, normalize(-vec3(lights[inInstance].rotation)));
      float epsilon = lights[inInstance].cutOff - lights[inInstance].outerCutOff;

      float intensity = clamp((theta - lights[inInstance].outerCutOff) / epsilon, 0.0, 1.0);

      lightDynDiff *= intensity;
    }
  }

  FragColor = vec4(lightDynDiff, 1.0);
}
