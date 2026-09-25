// rotation arrows
#pragma once
#include <vector>
#include <glm/glm.hpp>

#include <VkRenderData.h>

class DynamicLightDebugModel {
  public:
    VkSimpleMesh getVertexData();

  private:
    void init();
    VkSimpleMesh mVertexData{};
};
