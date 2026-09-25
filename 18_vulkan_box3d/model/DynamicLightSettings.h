#pragma once

#include <string>
#include <glm/glm.hpp>

#include <Enums.h>

struct DynamicLightSettings {
  glm::vec3 dlsWorldPosition = glm::vec3(0.0f);
  glm::vec3 dlsWorldRotation = glm::vec3(0.0f);

  int dlsIndexPosition = -1;

  bool dlsEnabled = true;
  bool dlsShadowEnabled = false;

  dynamicLightType dlsType = dynamicLightType::point;
  glm::vec3 dlsDiffuseColor = glm::vec3(1.0f);

  float dlsDistance = 75.0f;
  float dlsMaxDistance = 100.0f;

  float dlsSpotCutOffDegrees = 20.0f;
  float dlsSpotOuterCutOffDegrees = 30.0f;

  static constexpr float MIN_LIGHT_DIST = 0.0f;
  static constexpr float MAX_LIGHT_DIST = 500.0f;
};
