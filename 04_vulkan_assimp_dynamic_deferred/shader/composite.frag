#version 460 core

layout (input_attachment_index = 0, binding = 0) uniform subpassInput inputColor;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput inputPosition;
layout (input_attachment_index = 2, binding = 2) uniform subpassInput inputNormal;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

vec3 lightPos = vec3(4.0, 3.0, 6.0);
vec3 lightColor = vec3(1.0, 1.0, 1.0);

float toSRGB(float x) {
  if (x <= 0.0031308)
    return 12.92 * x;
  else
    return 1.055 * pow(x, (1.0/2.4)) - 0.055;
}
vec3 sRGB(vec3 c) {
  return vec3(toSRGB(c.x), toSRGB(c.y), toSRGB(c.z));
}

void main() {
  // Read G-Buffer values from previous sub pass
  vec3 fragPos = subpassLoad(inputPosition).rgb;
  float fragDepth = subpassLoad(inputPosition).a;
  vec3 normal = subpassLoad(inputNormal).rgb;
  vec4 albedo = subpassLoad(inputColor);

  float ambientStrength = 0.1;
  vec3 ambient = ambientStrength * max(vec3(lightColor), vec3(0.05, 0.05, 0.05)) * albedo.rgb;

  float diff = max(dot(normal, normalize(lightPos)), 0.0);
  vec3 diffuse = diff * vec3(lightColor) * albedo.rgb;

  //outColor = vec4((normal + 1.0) / 2.0, 1.0);
  //outColor = vec4(fragPos, 1.0);
  //outColor = vec4(fragDepth / 16.0);

  outColor = vec4(ambient + diffuse, 1.0);
  outColor.rgb = sRGB(outColor.rgb);
}
