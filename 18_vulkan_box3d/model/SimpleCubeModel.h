#pragma once
#include <vector>
#include <glm/glm.hpp>

#include "VkRenderData.h"

class SimpleCubeModel {
  public:
    VkSimpleMesh getVertexData();

  private:
    void init();
    VkSimpleMesh mVertexData;
};
