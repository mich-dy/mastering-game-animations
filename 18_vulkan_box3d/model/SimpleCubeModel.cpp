#include "SimpleCubeModel.h"
#include "Logger.h"

VkSimpleMesh SimpleCubeModel::getVertexData() {
  if (mVertexData.vertices.size() == 0) {
    init();
  }
  return mVertexData;
}

void SimpleCubeModel::init() {
  VkSimpleMesh vertexData;
  vertexData.vertices.resize(36);

  // front
  vertexData.vertices[0].position = glm::vec3(-1.0f, -1.0f,  1.0f);
  vertexData.vertices[1].position = glm::vec3( 1.0f,  1.0f,  1.0f);
  vertexData.vertices[2].position = glm::vec3(-1.0f,  1.0f,  1.0f);
  vertexData.vertices[3].position = glm::vec3(-1.0f, -1.0f,  1.0f);
  vertexData.vertices[4].position = glm::vec3( 1.0f, -1.0f,  1.0f);
  vertexData.vertices[5].position = glm::vec3( 1.0f,  1.0f,  1.0f);

  // back
  vertexData.vertices[6].position = glm::vec3(-1.0f, -1.0f,  -1.0f);
  vertexData.vertices[7].position = glm::vec3(-1.0f,  1.0f,  -1.0f);
  vertexData.vertices[8].position = glm::vec3( 1.0f,  1.0f,  -1.0f);
  vertexData.vertices[9].position = glm::vec3(-1.0f, -1.0f,  -1.0f);
  vertexData.vertices[10].position = glm::vec3( 1.0f,  1.0f,  -1.0f);
  vertexData.vertices[11].position = glm::vec3( 1.0f, -1.0f,  -1.0f);

  // left
  vertexData.vertices[12].position = glm::vec3(-1.0f, -1.0f,  1.0f);
  vertexData.vertices[13].position = glm::vec3(-1.0f,  1.0f,  1.0f);
  vertexData.vertices[14].position = glm::vec3(-1.0f,  1.0f,  -1.0f);
  vertexData.vertices[15].position = glm::vec3(-1.0f, -1.0f,  1.0f);
  vertexData.vertices[16].position = glm::vec3(-1.0f,  1.0f, -1.0f);
  vertexData.vertices[17].position = glm::vec3(-1.0f, -1.0f, -1.0f);

  // right
  vertexData.vertices[18].position = glm::vec3(1.0f, -1.0f,  1.0f);
  vertexData.vertices[19].position = glm::vec3(1.0f,  1.0f,  -1.0f);
  vertexData.vertices[20].position = glm::vec3(1.0f,  1.0f,  1.0f);
  vertexData.vertices[21].position = glm::vec3(1.0f, -1.0f,  1.0f);
  vertexData.vertices[22].position = glm::vec3(1.0f, -1.0f, -1.0f);
  vertexData.vertices[23].position = glm::vec3(1.0f,  1.0f, -1.0f);

  // top
  vertexData.vertices[24].position = glm::vec3( 1.0f,  1.0f,  1.0f);
  vertexData.vertices[25].position = glm::vec3(-1.0f,  1.0f,  -1.0f);
  vertexData.vertices[26].position = glm::vec3(-1.0f,  1.0f,  1.0f);
  vertexData.vertices[27].position = glm::vec3( 1.0f,  1.0f,  1.0f);
  vertexData.vertices[28].position = glm::vec3( 1.0f,  1.0f,  -1.0f);
  vertexData.vertices[29].position = glm::vec3(-1.0f,  1.0f,  -1.0f);

  // bottom
  vertexData.vertices[30].position = glm::vec3( 1.0f,  -1.0f,  1.0f);
  vertexData.vertices[31].position = glm::vec3(-1.0f,  -1.0f,  1.0f);
  vertexData.vertices[32].position = glm::vec3(-1.0f,  -1.0f,  -1.0f);
  vertexData.vertices[33].position = glm::vec3( 1.0f,  -1.0f,  1.0f);
  vertexData.vertices[34].position = glm::vec3(-1.0f,  -1.0f,  -1.0f);
  vertexData.vertices[35].position = glm::vec3( 1.0f,  -1.0f,  -1.0f);

  // create triangles from points
  for (int i = 0; i < vertexData.vertices.size(); i += 3) {
    VkSimpleVertex vert1 = vertexData.vertices.at(i);
    VkSimpleVertex vert2 = vertexData.vertices.at(i + 1);
    VkSimpleVertex vert3 = vertexData.vertices.at(i + 2);

    mVertexData.vertices.push_back(vert1);
    mVertexData.vertices.push_back(vert2);

    mVertexData.vertices.push_back(vert2);
    mVertexData.vertices.push_back(vert3);

    mVertexData.vertices.push_back(vert3);
    mVertexData.vertices.push_back(vert1);
  }

  Logger::log(1, "%s: SimpleCubeModel - loaded %d vertices\n",
    __FUNCTION__, mVertexData.vertices.size());
}
