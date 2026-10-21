#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <box3d/box3d.h>

struct LevelSettings {
  std::string lsLevelFilenamePath;
  std::string lsLevelFilename;

  glm::vec3 lsWorldPosition = glm::vec3(0.0f);
  glm::vec3 lsWorldRotation = glm::vec3(0.0f);
  float lsScale = 1.0f;
  bool lsSwapYZAxis = false;
};
