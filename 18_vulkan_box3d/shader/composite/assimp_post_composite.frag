#version 460 core
layout (location = 0) in vec3 color;
layout (location = 1) in vec2 texCoord;
layout (location = 2) in flat uint inInstance;

layout (location = 0) out vec4 FragColor;

layout (set = 0, binding = 0) uniform sampler2D tex;

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

layout (std430, set = 1, binding = 3) readonly restrict buffer DynamicLights {
  dynamicLight lights[];
};

void main() {
  // we must increment instance id by 1 for the null light
  FragColor = texture(tex, texCoord) * vec4(color, 1.0) * lights[inInstance].color;
}
