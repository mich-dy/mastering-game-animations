#include <AssimpDynLight.h>

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Logger.h>

AssimpDynLight::AssimpDynLight(std::shared_ptr<AssimpModel> model, glm::vec3 position, glm::vec3 rotation) : mAssimpModel(model) {
  if (!model) {
    Logger::log(1, "%s error: invalid model given\n", __FUNCTION__);
    return;
  }
  mDynLightSettings.dlsWorldPosition = position;
  mDynLightSettings.dlsWorldRotation = rotation;

  // save model root matrix
  mModelRootMatrix = mAssimpModel->getRootTranformationMatrix();

  updateModelRootMatrix();
}

void AssimpDynLight::updateModelRootMatrix() {
  mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mDynLightSettings.dlsWorldRotation)));

  mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mDynLightSettings.dlsWorldPosition);

  mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix;
  mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
}

glm::vec3 AssimpDynLight::getWorldPosition() {
  return mDynLightSettings.dlsWorldPosition;
}

glm::mat4 AssimpDynLight::getWorldTransformMatrix() {
  return mInstanceRootMatrix;
}

void AssimpDynLight::setWorldPosition(glm::vec3 position) {
  mDynLightSettings.dlsWorldPosition = position;
  updateModelRootMatrix();
}

void AssimpDynLight::setRotation(glm::vec3 rotation) {
  mDynLightSettings.dlsWorldRotation = rotation;
  updateModelRootMatrix();
}

bool AssimpDynLight::getLightEnabled() {
  return mDynLightSettings.dlsEnabled;
}

bool AssimpDynLight::getShadowEnabled() {
  return mDynLightSettings.dlsShadowEnabled;
}

void AssimpDynLight::rotateLight(float angle) {
  mDynLightSettings.dlsWorldRotation.y -= angle;
  if (mDynLightSettings.dlsWorldRotation.y < -180.0f) {
    mDynLightSettings.dlsWorldRotation.y += 360.0f;
  }
  if (mDynLightSettings.dlsWorldRotation.y >= 180.0f) {
    mDynLightSettings.dlsWorldRotation.y -= 360.0f;
  }
  updateModelRootMatrix();
}

void AssimpDynLight::rotateLight(glm::vec3 angles) {
  // keep all angles between -180 and 180 degree
  if (angles.x < -180.0f) {
    angles.x += 360.0f;
  }
  if (angles.x >= 180.0f) {
    angles.x -= 360.0f;
  }

  if (angles.y < -180.0f) {
    angles.y += 360.0f;
  }
  if (angles.y >= 180.0f) {
    angles.y -= 360.0f;
  }

  if (angles.z < -180.0f) {
    angles.z += 360.0f;
  }
  if (angles.z >= 180.0f) {
    angles.z -= 360.0f;
  }

  mDynLightSettings.dlsWorldRotation = angles;
  updateModelRootMatrix();
}

glm::vec3 AssimpDynLight::getRotation() {
  return mDynLightSettings.dlsWorldRotation;
}

glm::vec3 AssimpDynLight::getRotationRadians() {
  glm::mat4 rotationMat = glm::mat4_cast(glm::quat(glm::radians(mDynLightSettings.dlsWorldRotation)));

  // rotate a world up vector
  return glm::vec3(rotationMat * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
}

glm::vec3 AssimpDynLight::getLightColor() {
  return mDynLightSettings.dlsDiffuseColor;
}

float AssimpDynLight::getLightingDistance() {
  return mDynLightSettings.dlsDistance;
}

void AssimpDynLight::setLightingDistance(float value) {
  mDynLightSettings.dlsDistance = value;
  if (mDynLightSettings.dlsDistance < DynamicLightSettings::MIN_LIGHT_DIST) {
    mDynLightSettings.dlsDistance = DynamicLightSettings::MIN_LIGHT_DIST;
  }
  if (mDynLightSettings.dlsDistance > DynamicLightSettings::MAX_LIGHT_DIST) {
    mDynLightSettings.dlsDistance = DynamicLightSettings::MAX_LIGHT_DIST;
  }
}

float AssimpDynLight::getMaxLightingDistance() {
  return mDynLightSettings.dlsMaxDistance;
}

float AssimpDynLight::getPointLightCutOffAngle() {
  return mDynLightSettings.dlsSpotCutOffDegrees;
}

float AssimpDynLight::getPointLightOuterCutOffAngle() {
  return mDynLightSettings.dlsSpotOuterCutOffDegrees;
}

dynamicLightType AssimpDynLight::getLightType() {
  return mDynLightSettings.dlsType;
}

void AssimpDynLight::setDynLightSettings(DynamicLightSettings settings) {
  mDynLightSettings = settings;
  updateModelRootMatrix();
}

DynamicLightSettings AssimpDynLight::getDynLightSettings() {
  return mDynLightSettings;
}

int AssimpDynLight::getDynLightIndexPosition() {
  return mDynLightSettings.dlsIndexPosition;
}
