#include <DynamicLightDebugModel.h>
#include <Logger.h>

VkSimpleMesh DynamicLightDebugModel::getVertexData() {
  if (mVertexData.vertices.empty()) {
    init();
  }
  return mVertexData;
}

void DynamicLightDebugModel::init() {
  std::vector<glm::vec2> circleValues;
  for (float i = -180.0f; i <= 180.0f; i += 15.0f) {
    float xCoord = std::sin(glm::radians(i));
    float yCoord = std::cos(glm::radians(i));
    circleValues.emplace_back(glm::vec2(xCoord, yCoord));
  }

  int endIndex = circleValues.size();
  VkSimpleVertex startVert;
  VkSimpleVertex endVert;

  // X axis
  for (int i = 0; i < endIndex; ++i) {
    startVert.position = glm::vec3(0.0f, circleValues.at(i).x, circleValues.at(i).y);
    startVert.color = glm::vec3(1.0f, 1.0f, 1.0f);
    endVert.position = glm::vec3(0.0f, circleValues.at((i + 1) % endIndex ).x, circleValues.at((i + 1) % endIndex).y);
    endVert.color = glm::vec3(1.0f, 1.0f, 1.0f);

    mVertexData.vertices.emplace_back(startVert);
    mVertexData.vertices.emplace_back(endVert);
  }

  // Y axis
  for (int i = 0; i < endIndex; ++i) {
    startVert.position = glm::vec3(circleValues.at(i).x, 0.0f, circleValues.at(i).y);
    startVert.color = glm::vec3(1.0f, 1.0f, 1.0f);
    endVert.position = glm::vec3(circleValues.at((i + 1) % endIndex).x, 0.0f, circleValues.at((i + 1) % endIndex).y);
    endVert.color = glm::vec3(1.0f, 1.0f, 1.0f);

    mVertexData.vertices.emplace_back(startVert);
    mVertexData.vertices.emplace_back(endVert);
  }

  // Z axis
  for (int i = 0; i < endIndex; ++i) {
    startVert.position = glm::vec3(circleValues.at(i).x, circleValues.at(i).y, 0.0f);
    startVert.color = glm::vec3(1.0f, 1.0f, 1.0f);
    endVert.position = glm::vec3(circleValues.at((i + 1) % endIndex).x, circleValues.at((i + 1) % endIndex).y, 0.0f);
    endVert.color = glm::vec3(1.0f, 1.0f, 1.0f);

    mVertexData.vertices.emplace_back(startVert);
    mVertexData.vertices.emplace_back(endVert);
  }

  Logger::log(1, "%s: DynamicLightDebugModel - loaded %d vertices\n", __FUNCTION__, mVertexData.vertices.size());
}

