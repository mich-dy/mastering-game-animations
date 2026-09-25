#version 460 core
// Source:
// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/

layout(location = 0) in vec3 nearPoint;
layout(location = 1) in vec3 farPoint;

layout (location = 0) out vec4 FragColor;

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

vec4 grid(vec3 fragPos3D, float scale, bool drawAxis) {
  vec2 coord = fragPos3D.xz * scale;
  vec2 derivative = fwidth(coord);
  vec2 grid = abs(fract(coord - 0.5) - 0.5) / derivative;

  float line = min(grid.x, grid.y);
  float minimumz = min(derivative.y, 1);
  float minimumx = min(derivative.x, 1);

  vec4 color = vec4(0.2, 0.2, 0.2, 1.0 - min(line, 1.0));
  // z axis
  if(fragPos3D.x > -0.1 * minimumx && fragPos3D.x < 1.0 * minimumx) {
    color.x = 0.0;
    color.y = 0.0;
    color.z = 1.0;
  }
  // x axis
  if(fragPos3D.z > -0.1 * minimumz && fragPos3D.z < 1.0 * minimumz) {
    color.x = 1.0;
    color.y = 0.0;
    color.z = 0.0;
  }
  return color;
}

float computeDepth(vec3 pos) {
  vec4 clipSpacePos = projectionMat * viewMat * vec4(pos.xyz, 1.0);
  return (clipSpacePos.z / clipSpacePos.w);
}

float computeLinearDepth(vec3 pos) {
  vec4 clipSpacePos = projectionMat * viewMat * vec4(pos.xyz, 1.0);
  float clipSpaceDepth = (clipSpacePos.z / clipSpacePos.w);
  float linearDepth = (nearPlane * farPlane) / (farPlane + nearPlane - clipSpaceDepth * (farPlane - nearPlane));
  // normalize
  return linearDepth / farPlane;
}

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
  float t = -nearPoint.y / (farPoint.y - nearPoint.y);
  vec3 fragPos3D = nearPoint + t * (farPoint - nearPoint);

  float depth = computeDepth(fragPos3D);
  gl_FragDepth = depth;

  float fading = 1.0;
  if (farPlane == 0.0) {
    fading = 1.0 - abs(2.0 * fract(depth) - 1.0);
  }
  else {
    float linearDepth = computeLinearDepth(fragPos3D);
    fading = max(0.0, (0.5 - linearDepth));
  }

  FragColor = (grid(fragPos3D, 1.0, true) + grid(fragPos3D, 0.1, true) + grid(fragPos3D, 0.01, true))* float(t > 0); // adding multiple resolution for the grid
  FragColor.a *= fading;
}
