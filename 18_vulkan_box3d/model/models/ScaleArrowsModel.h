// scale arrows
#pragma once
#include <vector>
#include <glm/glm.hpp>

#include <VkRenderData.h>

class ScaleArrowsModel {
  public:
    VkSimpleMesh getVertexData();

  private:
    void init();
    VkSimpleMesh mVertexData{};
};
