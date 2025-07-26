#version 460 core
layout (location = 0) in vec4 color;
layout (location = 1) in vec3 position;
layout (location = 2) in vec4 normal;
layout (location = 3) in vec2 texCoord;
layout (location = 4) flat in float selectInfo;

layout (location = 1) out vec4 FragColor;
layout (location = 2) out vec4 outPosition;
layout (location = 3) out vec4 outNormal;
layout (location = 4) out float SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

layout (std140, set = 1, binding = 0) uniform Matrices {
  mat4 view;
  mat4 projection;
  vec4 lightPos;
  vec4 lightColor;
  float fogDensity;
};

layout (constant_id = 0) const float NEAR_PLANE = 0.01f;
layout (constant_id = 1) const float FAR_PLANE = 25.0f;

float linearDepth(float depth) {
  float z = depth * 2.0f - 1.0f;
  return (2.0f * NEAR_PLANE * FAR_PLANE) / (FAR_PLANE + NEAR_PLANE - z * (FAR_PLANE - NEAR_PLANE));
}

void main() {
  FragColor = texture(tex, texCoord) * color;

  outPosition.rgb = position;
  outPosition.a =  linearDepth(gl_FragCoord.z);
  outNormal = vec4(normalize(normal.rgb), 1.0);

  SelectedInstance = selectInfo;
}
