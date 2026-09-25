#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include <VkRenderData.h>
#include <YamlParserTypes.h>
#include <ModelInstanceCamData.h>
#include <InstanceSettings.h>
#include <CameraSettings.h>
#include <ModelSettings.h>
#include <BehaviorData.h>
#include <LevelSettings.h>
#include <DynamicLightSettings.h>
#include <Enums.h>

class YamlParser {
  public:
    // loading
    bool loadYamlFile(std::string fileName);
    std::string getFileName();
    std::string getFileVersion();

    std::vector<ModelSettings> getModelConfigs();
    std::vector<ExtendedInstanceSettings> getInstanceConfigs();
    std::vector<CameraSettings> getCameraConfigs();
    std::vector<ExtendedBehaviorData> getBehaviorData();
    std::vector<LevelSettings> getLevelConfigs();
    std::vector<DynamicLightSettings> getDynLightConfigs();

    int getSelectedModelNum();
    int getSelectedInstanceNum();
    int getSelectedCameraNum();
    int getSelectedLevelNum();
    int getSelectedDynLightNum();
    bool getHighlightActivated();

    float getNearPlane();
    float getFarPlane();
    float getOrthoNearFarPlane();

    glm::vec3 getCameraPosition();
    float getCameraElevation();
    float getCameraAzimuth();

    collisionChecks getCollisionChecksEnabled();
    bool getInteractionEnabled();
    float getInteractionFOV();
    float getInteractionMinRange();
    float getInteractionMaxRange();

    bool getGravityEnabled();
    float getMaxGroundSlopeAngle();
    float getMaxStairStepHeight();
    bool getIKEnabled();
    int getIKNumIterations();
    bool getNavEnabled();
    bool getSkyboxEnabled();
    float getFogDensity();
    float getLightSourceAngleEastWest();
    float getLightSourceAngleNorthSouth();
    float getLightSouceIntensity();
    glm::vec3 getLightSourceColor();
    bool getTimeOfDayEnabled();
    float getTimeOfDayScaleFactor();
    timeOfDay getTimeOfDayPreset();

    bool getSSAOEnabled();
    float getSSAORadius();
    float getSSAOBias();
    int getSSAOExponent();
    bool getSSAOBlurEnabled();
    int getSSAOBlurRadius();

    bool getShadowMapEnabled();
    float getShadowMapSplitLambda();
    float getShadowMapConstantDepthBias();
    float getShadowMapSlopeDepthBias();
    bool getShadowMapPFCEnabled();
    float getShadowMapPCFScale();
    int getShadowMapPCFRange();

    // saving
    bool createConfigFile(VkRenderData &renderData, ModelInstanceCamData modInstCamData);
    bool writeYamlFile(std::string fileName);

    // misc
    bool hasKey(std::string key);
    bool getValue(std::string key, std::string& value);

  private:
    void createInstanceToCamMap(ModelInstanceCamData modInstCamData);
    std::unordered_map<int, std::vector<std::string>> mInstanceToCamMap;

    std::string mYamlFileName;
    YAML::Node mYamlNode{};
    YAML::Emitter mYamlEmit{};

    const std::string mYamlConfigFileVersion = "13.0";
    std::string mYamlFileVersion;
};
