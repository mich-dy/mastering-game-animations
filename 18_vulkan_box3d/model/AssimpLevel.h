// Assimp model, ready to draw
#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <glm/glm.hpp>

#include <box3d/box3d.h>

#include <Texture.h>
#include <AssimpMesh.h>
#include <AssimpNode.h>
#include <AssimpAnimClip.h>
#include <VertexBuffer.h>
#include <IndexBuffer.h>
#include <ShaderStorageBuffer.h>
#include <LevelSettings.h>
#include <AABB.h>

#include <VkRenderData.h>

class AssimpLevel {
  public:
    bool loadLevel(VkRenderData &renderData, std::string levelFilename, unsigned int extraImportFlags = 0);

    void draw(VkRenderData &renderData);
    unsigned int getTriangleCount();

    void updateLevelRootMatrix();
    glm::mat4 getWorldTransformMatrix();
    glm::mat4 getNormalTransformMatrix();

    std::string getLevelFileName();
    std::string getLevelFileNamePath();

    LevelSettings getLevelSettings();
    void setLevelSettings(LevelSettings settings);

    void generateAABB();
    AABB getAABB();

    std::vector<VkMesh>& getLevelMeshes();

    bool recalculateBox3dMesh(VkRenderData &renderData);
    bool createBox3dMesh(VkRenderData &renderData);
    void destroyBox3dMesh();

    b3BodyId getBox3dBodyId();
    std::unordered_set<int> getBox3dShapeIds();
    std::optional<std::array<glm::vec3, 3>> getTriangleDateFromShapeAndIndex(int shapeId, int triangleIndex);

    void cleanup(VkRenderData &renderData);

private:
    void processNode(VkRenderData &renderData, std::shared_ptr<AssimpNode> node, aiNode* aNode, const aiScene* scene, std::string assetDirectory);

    unsigned int mTriangleCount = 0;
    unsigned int mVertexCount = 0;

    glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);

    glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);
    glm::mat4 mNormalTransformMatrix = glm::mat4(1.0f);

    glm::mat4 mLevelRootMatrix = glm::mat4(1.0f);

    // store the root node for direct access
    std::shared_ptr<AssimpNode> mRootNode = nullptr;
    std::vector<std::shared_ptr<AssimpNode>> mNodeList{};

    glm::mat4 mRootTransformMatrix = glm::mat4(1.0f);
    LevelSettings mLevelSettings{};

    std::vector<VkMesh> mLevelMeshes{};
    std::vector<VkVertexBufferData> mVertexBuffers{};
    std::vector<VkIndexBufferData> mIndexBuffers{};

    // map textures to external or internal texture names
    std::unordered_map<std::string, VkTextureData> mTextures{};
    VkTextureData mPlaceholderTexture{};

    AABB mLevelAABB{};

    b3BodyId mBox3dBodyId = b3_nullBodyId;
    std::vector<b3ShapeId> mBox3dShapes{};
    std::unordered_set<int> mBox3dShapeIdSet{};

    std::unordered_map<int, b3MeshData*> mBox3dShapeToMeshMap{};

    std::vector<b3MeshData*> mBox3dMeshData{};
    std::vector<std::vector<int32_t>> mBox3dMeshIndices{};
    std::vector<std::vector<b3Vec3>> mBox3dMeshVertices{};
    std::vector<std::vector<uint8_t>> mBox3dMaterialIndices{};
    std::vector<b3SurfaceMaterial> mBox3dTriangleMaterials{};
};
