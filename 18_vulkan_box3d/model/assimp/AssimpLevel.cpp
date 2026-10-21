#include <algorithm>
#include <filesystem>

#include <glm/gtx/quaternion.hpp>

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include <AssimpLevel.h>
#include <Tools.h>
#include <Logger.h>

bool AssimpLevel::loadLevel(VkRenderData &renderData, std::string levelFilename, unsigned int extraImportFlags) {
  Logger::log(1, "%s: loading level from file '%s'\n", __FUNCTION__, levelFilename.c_str());

  mLevelSettings.lsLevelFilenamePath = levelFilename;
  mLevelSettings.lsLevelFilename = std::filesystem::path(levelFilename).filename().generic_string();

  Assimp::Importer importer;
  // we need to flip texture coordinates for Vulkan
  const aiScene *scene = importer.ReadFile(levelFilename,
    aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_ValidateDataStructure | aiProcess_FlipUVs | extraImportFlags);

  if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
    Logger::log(1, "%s error: assimp error '%s' while loading file '%s'\n", __FUNCTION__, importer.GetErrorString(), levelFilename.c_str());
    return false;
  }

  unsigned int numMeshes = scene->mNumMeshes;
  Logger::log(1, "%s: found %i mesh%s\n", __FUNCTION__, numMeshes, numMeshes == 1 ? "" : "es");

  for (unsigned int i = 0; i < numMeshes; ++i) {
    unsigned int numVertices = scene->mMeshes[i]->mNumVertices;
    unsigned int numFaces = scene->mMeshes[i]->mNumFaces;

    mVertexCount += numVertices;
    mTriangleCount += numFaces;

    Logger::log(1, "%s: mesh %i contains %i vertices and %i faces\n", __FUNCTION__, i, numVertices, numFaces);
  }
  Logger::log(1, "%s: model contains %i vertices and %i faces\n", __FUNCTION__, mVertexCount, mTriangleCount);

  aiNode* rootNode = scene->mRootNode;

  if (scene->HasTextures()) {
    unsigned int numTextures = scene->mNumTextures;

    for (int i = 0; i < scene->mNumTextures; ++i) {
      std::string texName = scene->mTextures[i]->mFilename.C_Str();

      int height = scene->mTextures[i]->mHeight;
      int width = scene->mTextures[i]->mWidth;
      aiTexel* data = scene->mTextures[i]->pcData;

      VkTextureData newTex{};
      if (!Texture::loadTexture(renderData, newTex, texName, data, width, height)) {
        return false;
      }

      std::string internalTexName = "*" + std::to_string(i);
      Logger::log(1, "%s: - added internal texture '%s'\n", __FUNCTION__, internalTexName.c_str());
      mTextures.insert({internalTexName, newTex});
    }

    Logger::log(1, "%s: scene has %i embedded textures\n", __FUNCTION__, numTextures);
  }

  // add a placeholder texture in case there is no diffuse tex
  std::string placeholderTexName = "textures/missing_tex.png";
  if (!Texture::loadTexture(renderData, mPlaceholderTexture, placeholderTexName)) {
    Logger::log(1, "%s error: could not load placeholder texture '%s'\n", __FUNCTION__, placeholderTexName.c_str());
    return false;
  }

  // the textures are stored directly or relative to the model file
  std::string assetDirectory = levelFilename.substr(0, levelFilename.find_last_of('/'));

  // nodes
  Logger::log(1, "%s: ... processing nodes...\n", __FUNCTION__);

  std::string rootNodeName = rootNode->mName.C_Str();
  mRootNode = AssimpNode::createNode(rootNodeName);
  Logger::log(2, "%s: root node name: '%s'\n", __FUNCTION__, rootNodeName.c_str());

  // process all nodes in the level file
  processNode(renderData, mRootNode, rootNode, scene, assetDirectory);

  Logger::log(1, "%s: ... processing nodes finished...\n", __FUNCTION__);

  for (const auto& entry : mNodeList) {
    std::vector<std::shared_ptr<AssimpNode>> childNodes = entry->getChilds();

    std::string parentName = entry->getParentNodeName();
    Logger::log(1, "%s: --- found node %s in node list, it has %i children, parent is %s\n", __FUNCTION__, entry->getNodeName().c_str(), childNodes.size(), parentName.c_str());

    for (const auto& node : childNodes) {
      Logger::log(1, "%s: ---- child: %s\n", __FUNCTION__, node->getNodeName().c_str());
    }
  }

  // get root transformation matrix from model's root node
  mRootTransformMatrix = Tools::convertAiToGLM(rootNode->mTransformation);
  updateLevelRootMatrix();

  if (mLevelMeshes.size() > 0) {
    // create vertex buffers for the meshes
    for (const auto& mesh : mLevelMeshes) {
      VkVertexBufferData vertexBuffer;
      VertexBuffer::init(renderData, vertexBuffer, mesh.vertices.size() * sizeof(VkVertex));
      VertexBuffer::uploadData(renderData, vertexBuffer, mesh.vertices);
      mVertexBuffers.emplace_back(vertexBuffer);

      VkIndexBufferData indexBuffer;
      IndexBuffer::init(renderData, indexBuffer, mesh.indices.size() * sizeof(uint32_t));
      IndexBuffer::uploadData(renderData, indexBuffer, mesh);
      mIndexBuffers.emplace_back(indexBuffer);
    }

    // add Box3D data
    renderData.rdPhysicsTimerFunc.callExtFunctionLocked([&, this]() {
      createBox3dMesh(renderData);
    });
  }

  Logger::log(1, "%s: level '%s'  has a total of %i texture%s\n", __FUNCTION__,
    mLevelSettings.lsLevelFilename.c_str(), mTextures.size(), mTextures.size() == 1 ? "" : "s");

  Logger::log(1, "%s: successfully loaded level '%s' (%s)\n", __FUNCTION__, levelFilename.c_str(),
    mLevelSettings.lsLevelFilename.c_str());

  return true;
}

bool AssimpLevel::createBox3dMesh(VkRenderData& renderData) {
  if (b3Body_IsValid(mBox3dBodyId)) {
    Logger::log(1, "%s warning: removing level body index %i\n", __FUNCTION__,
      mBox3dBodyId.index1);
    b3DestroyBody(mBox3dBodyId);
    mBox3dBodyId = b3_nullBodyId;
  }

  b3BodyDef bodyDef = b3DefaultBodyDef();
  bodyDef.type = b3_staticBody;
  // position and rotation are applied on meshes, body remains at origin
  //bodyDef.position = Tools::glmToBox3d(mLevelSettings.lsWorldPosition);
  //bodyDef.rotation = Tools::glmToBox3d(glm::quat(mLocalRotationMatrix * mLocalSwapAxisMatrix * glm::vec4(0.0f, -1.0f, 0.0f, 1.0f)));
  //Tools::glmToBox3d(glm::quat(glm::radians(mLevelSettings.lsWorldRotation)));

  mBox3dBodyId = b3CreateBody(renderData.rdBox3DWorldId, &bodyDef);
  Logger::log(1, "%s: created level body index %i\n", __FUNCTION__,
    mBox3dBodyId.index1);

  int indexOffset = 0;
  for (const auto& mesh : mLevelMeshes) {
    std::transform(mesh.vertices.begin(), mesh.vertices.end(), std::back_inserter(mBox3dMeshVertices),
      // position.w is used to store UV.x coordinate, force to 1.0f
      [this](const VkVertex &a) { return Tools::glmToBox3d(glm::vec3(mLevelRootMatrix * glm::vec4(glm::vec3(a.position), 1.0f)));
      }
    );

    std::transform(mesh.indices.begin(), mesh.indices.end(), std::back_inserter(mBox3dMeshIndices),
      [&](const uint32_t &a) { return a + indexOffset; });

    indexOffset += mesh.vertices.size();
  }

  Logger::log(1, "%s: current mesh length is %i vertices and %i indices \n", __FUNCTION__,
    mBox3dMeshVertices.size(), mBox3dMeshIndices.size());

  // create mesh
  mBox3dMaterialIndices.resize(mBox3dMeshIndices.size() / 3.0f, 0);

  b3MeshDef meshDef{};
  meshDef.vertices = mBox3dMeshVertices.data();
  meshDef.vertexCount = static_cast<int>(mBox3dMeshVertices.size());
  meshDef.indices = mBox3dMeshIndices.data();
  meshDef.triangleCount = static_cast<int>(mBox3dMeshIndices.size() / 3.0f);
  meshDef.materialIndices = mBox3dMaterialIndices.data();

  // optional settings
  /*
  meshDef.useMedianSplit = true;
  meshDef.identifyEdges = true;
  meshDef.weldVertices = true;
  meshDef.weldTolerance = 0.001f * 1.5f;
  */

  mBox3dMeshData = b3CreateMesh(&meshDef, nullptr, 0);
  if (!mBox3dMeshData) {
    Logger::log(1, "%s error: mesh data is empty\n", __FUNCTION__);
    return false;
  }

  Logger::log(1, "%s: mesh is %i bytes long, has %i triangles\n", __FUNCTION__,
    mBox3dMeshData->byteCount, mBox3dMeshData->triangleCount);

  b3ShapeDef shapeDef = b3DefaultShapeDef();
  shapeDef.density = 1.0f;
  shapeDef.materialCount = 1;
  shapeDef.materials = &mBox3dTriangleMaterial;
  shapeDef.enableContactEvents = true;
  //shapeDef.userData = static_cast<void *>(&mLevelMeshes.at(meshNum));

  // scaling is done when transforming the the level vertices to b3Vec3
  b3Vec3 scale = b3Vec3{1.0f, 1.0f, 1.0f}; //Tools::glmToBox3d(glm::vec3(mLevelSettings.lsScale));

  mBox3dShapeId = b3CreateMeshShape(mBox3dBodyId,
    &shapeDef, mBox3dMeshData, scale);

  Logger::log(1, "%s: added shape %i\n", __FUNCTION__, mBox3dShapeId.index1);

  int numShapes = b3Body_GetShapeCount(mBox3dBodyId);
  Logger::log(1, "%s: level '%s' has %i shapes attached\n", __FUNCTION__,
    mLevelSettings.lsLevelFilename.c_str(), numShapes);

  return true;
}

void AssimpLevel::destroyBox3dMesh() {
  if (b3Shape_IsValid(mBox3dShapeId)) {
  Logger::log(1, "%s: removing shape index %i\n", __FUNCTION__,
    mBox3dShapeId.index1);
    b3DestroyShape(mBox3dShapeId, true);
    mBox3dShapeId = b3_nullShapeId;
  }

  if (mBox3dMeshData) {
    b3DestroyMesh(mBox3dMeshData);
    mBox3dMeshData = nullptr;
  }

  if (b3Body_IsValid(mBox3dBodyId)) {
    Logger::log(1, "%s: removing level body index %i\n", __FUNCTION__,
      mBox3dBodyId.index1);
    b3DestroyBody(mBox3dBodyId);
    mBox3dBodyId = b3_nullBodyId;
  }

  mBox3dMeshIndices.clear();
  mBox3dMeshVertices.clear();
  mBox3dMaterialIndices.clear();
  mBox3dTriangleMaterial = b3DefaultSurfaceMaterial();

  Logger::log(1, "%s: Box3D data for level '%s' removed\n", __FUNCTION__,
    mLevelSettings.lsLevelFilename.c_str());
}

bool AssimpLevel::recalculateBox3dMesh(VkRenderData &renderData) {
  renderData.rdPhysicsTimerFunc.callExtFunctionLocked([&, this]() {
    destroyBox3dMesh();
  });

  bool retval = true;
  renderData.rdPhysicsTimerFunc.callExtFunctionLocked([&, this]() {
    retval = createBox3dMesh(renderData);
  });
  return retval;
}

b3BodyId AssimpLevel::getBox3dBodyId() {
  return mBox3dBodyId;
}

b3ShapeId AssimpLevel::getBox3dShapeId() {
  return mBox3dShapeId;
}

std::optional<std::array<glm::vec3, 3>> AssimpLevel::getTriangleDateFromShapeAndIndex(int shapeId, int triangleIndex) {

  unsigned char* meshAddress = reinterpret_cast<unsigned char*>(mBox3dMeshData);

  // use triangle offset to get indices
  int triangleOffset = mBox3dMeshData->triangleOffset;
  int triangleCount = mBox3dMeshData->triangleCount;

  b3MeshTriangle* triangles = reinterpret_cast<b3MeshTriangle*>(meshAddress + triangleOffset);

  if (triangleIndex > triangleCount * sizeof(b3MeshTriangle)) {
    Logger::log(1, "%s error: attempt to access triangle index data beyound data\n", __FUNCTION__);
    return std::nullopt;
  }

  int32_t pt1Index = (triangles + triangleIndex)->index1;
  int32_t pt2Index = (triangles + triangleIndex)->index2;
  int32_t pt3Index = (triangles + triangleIndex)->index3;

  // use indices to get vertex data
  int vertexOffset = mBox3dMeshData->vertexOffset;
  int vertexCount = mBox3dMeshData->vertexCount;

  b3Vec3* vertices = reinterpret_cast<b3Vec3*>(meshAddress + vertexOffset);

  if (pt1Index > vertexCount * sizeof(b3Vec3)
      || pt2Index > vertexCount * sizeof(b3Vec3)
      || pt3Index > vertexCount * sizeof(b3Vec3)) {
    Logger::log(1, "%s error: attempt to access vertex data beyound data\n", __FUNCTION__);
    return std::nullopt;
  }

  glm::vec3 vertex1 = Tools::box3dToGlm(*(vertices + pt1Index));
  glm::vec3 vertex2 = Tools::box3dToGlm(*(vertices + pt2Index));
  glm::vec3 vertex3 = Tools::box3dToGlm(*(vertices + pt3Index));

  // offset points a bit in normal direction
  glm::vec3 side1 = vertex2 - vertex1;
  glm::vec3 side2 = vertex3 - vertex1;
  glm::vec3 normal = glm::normalize(glm::cross(side1, side2));

  const float normalOffsetFactor = 0.02f;
  glm::vec3 offsetNormal = normal * normalOffsetFactor;

  glm::vec3 pos1 = vertex1 + offsetNormal;
  glm::vec3 pos2 = vertex2 + offsetNormal;
  glm::vec3 pos3 = vertex3 + offsetNormal;

  return std::array<glm::vec3, 3>( { pos1, pos2, pos3 });
}

void AssimpLevel::processNode(VkRenderData &renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode,
    const aiScene* scene, std::string assetDirectory) {
  std::string nodeName = aNode->mName.C_Str();
  Logger::log(1, "%s: node name: '%s'\n", __FUNCTION__, nodeName.c_str());

  unsigned int numMeshes = aNode->mNumMeshes;
  if (numMeshes > 0) {
    Logger::log(1, "%s: - node has %i meshes\n", __FUNCTION__, numMeshes);
    for (unsigned int i = 0; i < numMeshes; ++i) {
      aiMesh* modelMesh = scene->mMeshes[aNode->mMeshes[i]];

      AssimpMesh mesh;
      mesh.processMesh(renderData, modelMesh, scene, assetDirectory, mTextures);
      VkMesh vertexMesh = mesh.getMesh();

      mLevelMeshes.emplace_back(vertexMesh);
    }
  }

  mNodeList.emplace_back(node);

  unsigned int numChildren = aNode->mNumChildren;
  Logger::log(1, "%s: - node has %i children \n", __FUNCTION__, numChildren);

  for (unsigned int i = 0; i < numChildren; ++i) {
    std::string childName = aNode->mChildren[i]->mName.C_Str();
    Logger::log(1, "%s: --- found child node '%s'\n", __FUNCTION__, childName.c_str());

    std::shared_ptr<AssimpNode> childNode = node->addChild(childName);
    processNode(renderData, childNode, aNode->mChildren[i], scene, assetDirectory);
  }
}

void AssimpLevel::draw(VkRenderData &renderData) {
  for (unsigned int i = 0; i < mLevelMeshes.size(); ++i) {
    VkMesh& mesh = mLevelMeshes.at(i);

    // find diffuse texture by name
    VkTextureData diffuseTex{};
    auto diffuseTexName = mesh.textures.find(aiTextureType_DIFFUSE);
    if (diffuseTexName != mesh.textures.end()) {
      auto diffuseTexture = mTextures.find(diffuseTexName->second);
      if (diffuseTexture != mTextures.end()) {
        diffuseTex = diffuseTexture->second;
      }
    }

    if (diffuseTex.image != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(renderData.rdCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        renderData.rdAssimpLevelPipelineLayout, 0, 1, &diffuseTex.descriptorSet, 0, nullptr);
    } else {
      vkCmdBindDescriptorSets(renderData.rdCommandBuffers[renderData.currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS,
        renderData.rdAssimpLevelPipelineLayout, 0, 1, &mPlaceholderTexture.descriptorSet, 0, nullptr);
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(renderData.rdCommandBuffers[renderData.currentFrame], 0, 1, &mVertexBuffers.at(i).buffer, &offset);
    vkCmdBindIndexBuffer(renderData.rdCommandBuffers[renderData.currentFrame], mIndexBuffers.at(i).buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(renderData.rdCommandBuffers[renderData.currentFrame], static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
  }
}

void AssimpLevel::updateLevelRootMatrix() {
  mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mLevelSettings.lsScale));

  if (mLevelSettings.lsSwapYZAxis) {
    glm::mat4 flipMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    mLocalSwapAxisMatrix = glm::rotate(flipMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  } else {
    mLocalSwapAxisMatrix = glm::mat4(1.0f);
  }

  mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mLevelSettings.lsWorldRotation)));

  mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mLevelSettings.lsWorldPosition);

  mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;

  mLevelRootMatrix = mLocalTransformMatrix * mRootTransformMatrix;

  // do NOT swap the axes for normals
  mNormalTransformMatrix = glm::transpose(glm::inverse(mLevelRootMatrix));
}

unsigned int AssimpLevel::getTriangleCount() {
  return mTriangleCount;
}

void AssimpLevel::cleanup(VkRenderData &renderData) {
  renderData.rdPhysicsTimerFunc.callExtFunctionLocked([this]() {
    destroyBox3dMesh();
  });

  for (auto buffer : mVertexBuffers) {
    VertexBuffer::cleanup(renderData, buffer);
  }
  for (auto buffer : mIndexBuffers) {
    IndexBuffer::cleanup(renderData, buffer);
  }

  for (auto& tex : mTextures) {
    Texture::cleanup(renderData, tex.second);
  }

  Texture::cleanup(renderData, mPlaceholderTexture);
}

glm::mat4 AssimpLevel::getWorldTransformMatrix() {
  return mLevelRootMatrix;
}

glm::mat4 AssimpLevel::getNormalTransformMatrix() {
  return mNormalTransformMatrix;
}

LevelSettings AssimpLevel::getLevelSettings() {
  return mLevelSettings;
}

void AssimpLevel::setLevelSettings(LevelSettings settings) {
  mLevelSettings = settings;
  updateLevelRootMatrix();
}

std::string AssimpLevel::getLevelFileName() {
  return mLevelSettings.lsLevelFilename;
}

std::string AssimpLevel::getLevelFileNamePath() {
  return mLevelSettings.lsLevelFilenamePath;
}

void AssimpLevel::generateAABB() {
  updateLevelRootMatrix();
  mLevelAABB.clear();
  for (const auto& mesh : mLevelMeshes) {
    for (const auto& vertex : mesh.vertices) {
      // we use position.w for UV coordinates, set to 1.0f
      mLevelAABB.addPoint(mLevelRootMatrix * glm::vec4(glm::vec3(vertex.position), 1.0f));
    }
  }
}

AABB AssimpLevel::getAABB() {
  return mLevelAABB;
}

std::vector<VkMesh>& AssimpLevel::getLevelMeshes() {
  return mLevelMeshes;
}

VkMesh& AssimpLevel::getLevelPhysicsMesh() {
  mLevelPhysicsMesh.vertices.clear();
  mLevelPhysicsMesh.indices.clear();

  std::transform(mBox3dMeshVertices.begin(), mBox3dMeshVertices.end(),
  std::back_inserter(mLevelPhysicsMesh.vertices),
    [](const b3Vec3 &a) {
      VkVertex v;
      v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
      v.position = glm::vec4(Tools::box3dToGlm(a), 1.0f);
      return v;
    }
  );

  std::transform(mBox3dMeshIndices.begin(), mBox3dMeshIndices.end(),
  std::back_inserter(mLevelPhysicsMesh.indices),
    [](const uint32_t &a) {
      return a;
    }
  );

  return mLevelPhysicsMesh;
}
