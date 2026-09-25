#pragma once

#include <VkRenderData.h>
#include <glm/glm.hpp>

class FullSphereModel {
  public:
    FullSphereModel(const float radius = 1.0f, const float verticalDiv = 10, const float horizontalDiv = 20, const glm::vec3 color = glm::vec3(1.0f)) :
      mRadius(radius), mVertDiv(verticalDiv), mHorDiv(horizontalDiv), mColor(color) {};

    VkSimpleMesh getVertexData();

  private:
    void init();

    VkSimpleMesh mVertexData {};

    float mRadius = 1.0f;
    unsigned int mVertDiv = 10;
    unsigned int mHorDiv = 20;
    glm::vec3 mColor = glm::vec3(1.0f);
};
