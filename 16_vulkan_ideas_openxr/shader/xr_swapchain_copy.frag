#version 460 core
#include "xr_view.glsl"

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2DArray inputColor;

// SteamVR or OpenXR adds another sRGB conversion on top, so we convert to linear colors here
vec3 sRGBToLinear(vec3 rgb) {
  // See https://gamedev.stackexchange.com/questions/92015/optimized-linear-to-srgb-glsl
  return mix(pow((rgb + 0.055) * (1.0 / 1.055), vec3(2.4)),
             rgb * (1.0/12.92),
             lessThanEqual(rgb, vec3(0.04045)));
}

void main() {
  vec3 albedo = texture(inputColor, vec3(inUV, XR_VIEW_INDEX)).rgb;
  albedo = sRGBToLinear(albedo);
  outColor = vec4(albedo, 1.0);
}
