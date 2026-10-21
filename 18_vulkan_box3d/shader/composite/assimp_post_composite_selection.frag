#version 460 core
layout (location = 0) in vec3 color;
layout (location = 1) in vec2 texCoord;
layout (location = 2) flat in float selectInfo;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out float SelectedInstance;

layout (set = 0, binding = 0) uniform sampler2D tex;

void main() {
  FragColor = texture(tex, texCoord) * vec4(color, 1.0);

  SelectedInstance = selectInfo;
}
