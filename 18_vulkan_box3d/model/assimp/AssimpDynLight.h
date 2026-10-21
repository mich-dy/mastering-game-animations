#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include <AssimpModel.h>
#include <DynamicLightSettings.h>

class AssimpDynLight {
  public:
    AssimpDynLight(std::shared_ptr<AssimpModel> model, glm::vec3 position = glm::vec3(0.0f), glm::vec3 rotation = glm::vec3(0.0f));

    std::shared_ptr<AssimpModel> getModel();
    glm::vec3 getWorldPosition();
    glm::mat4 getWorldTransformMatrix();
    void updateModelRootMatrix();

    void setWorldPosition(glm::vec3 position);
    void setRotation(glm::vec3 rotation);

    glm::vec3 getRotation();
    glm::vec3 getRotationRadians();

    glm::vec3 getLightColor();
    float getLightingDistance();
    void setLightingDistance(float value);
    float getMaxLightingDistance();
    float getPointLightCutOffAngle();
    float getPointLightOuterCutOffAngle();

    dynamicLightType getLightType();

    bool getLightEnabled();
    bool getShadowEnabled();

    void rotateLight(float angle);
    void rotateLight(glm::vec3 angles);

    void setDynLightSettings(DynamicLightSettings settings);
    DynamicLightSettings getDynLightSettings();

    int getDynLightIndexPosition();

private:
    std::shared_ptr<AssimpModel> mAssimpModel = nullptr;

    DynamicLightSettings mDynLightSettings{};

    glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);

    glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

    glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
    glm::mat4 mModelRootMatrix = glm::mat4(1.0f);
};
