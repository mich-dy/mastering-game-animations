#version 460 core
// Source:
// https://asliceofrendering.com/scene%20helper/2020/01/05/InfiniteGrid/

layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;

layout (std140, set = 0, binding = 0) uniform Matrices {
  mat4 viewMat;
  mat4 projectionMat;
  mat4 invViewMat;
  mat4 invProjectionMat;
};

// Grid position are in clipped space
vec3 gridPlane[6] = vec3[] (
  vec3(1, 1, 0), vec3(-1, -1, 0), vec3(-1, 1, 0),
  vec3(-1, -1, 0), vec3(1, 1, 0), vec3(1, -1, 0)
);

vec3 UnprojectPoint(float x, float y, float z, mat4 viewMat, mat4 projectionMat) {
  vec4 unprojectedPoint =  invViewMat * invProjectionMat * vec4(x, y, z, 1.0);
  return unprojectedPoint.xyz / unprojectedPoint.w;
}

void main() {
  vec3 p = gridPlane[gl_VertexIndex].xyz;
  // unprojecting on the near plane
  nearPoint = UnprojectPoint(p.x, p.y, 0.0, viewMat, projectionMat).xyz;
  // unprojecting on the far plane
  farPoint = UnprojectPoint(p.x, p.y, 1.0, viewMat, projectionMat).xyz;
  // using directly the clipped coordinates
  gl_Position = vec4(p, 1.0);
}
