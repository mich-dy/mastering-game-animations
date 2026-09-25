// Tools functions
#pragma once
#include <string>
#include <optional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <box3d/box3d.h>

#include <assimp/matrix4x4.h>

#include <VkRenderData.h>

class Tools {
  public:
    static std::string getFilenameExt(std::string filename);
    static std::string loadFileToString(std::string fileName);

    static glm::mat4 convertAiToGLM(aiMatrix4x4 inMat);

    static std::optional<glm::vec3> rayTriangleIntersection(glm::vec3 rayOrigin, glm::vec3 rayDirection, MeshTriangle triangle);

    static glm::vec4 extractGlobalPosition(glm::mat4 nodeMatrix);
    static glm::quat extractGlobalRotation(glm::mat4 nodeMatrix);

    static std::vector<std::string> getDirectoryContent(std::string path, std::string extension);

    static b3Vec3 glmToBox3d(glm::vec3 value);
    static b3Quat glmToBox3d(glm::quat value);

    static glm::vec3 box3dToGlm(b3Vec3 value);
    static glm::quat box3dToGlm(b3Quat value);
};
