#if !defined(__ANDROID__)
#include <imgui_impl_glfw.h>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <set>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <VkRenderer.h>
#include <VkHelper.h>

#include <CommandPool.h>
#include <CommandBuffer.h>
#include <SyncObjects.h>
#include <Image.h>

#include <PipelineLayout.h>
#include <ModelLevelPipeline.h>
#include <ComputePipeline.h>
#include <LinePipeline.h>
#include <GridLinePipeline.h>
#include <SimpleVertexMeshPipeline.h>
#include <SkyboxPipeline.h>
#include <CompositePipeline.h>
#include <SSAOPipeline.h>
#include <ShadowMapPipeline.h>
#include <DynamicShadowMapPipeline.h>
#include <LightSpherePipeline.h>

#include <InstanceSettings.h>
#include <DynamicLightSettings.h>
#include <AssimpSettingsContainer.h>
#include <YamlParser.h>
#include <Tools.h>

#include <Logger.h>

#if defined(__ANDROID__)
bool VkRenderer::init(void *window, std::vector<std::string> &deviceExtForXR, std::vector<std::string> &instExtsForXR) {
  int width = 1024;
  int height = 768;
  mRenderData.rdWidth = width;
  mRenderData.rdHalfWidth = width / 2;
  mRenderData.rdHeight = height;
  mRenderData.rdWindowWidth = width;
  mRenderData.rdWindowHeight = height;

  unsigned int seed = mRandomDevice();
  mRandomEngine = std::default_random_engine(seed);

  // init app mode map first
  mRenderData.rdAppModeMap[appMode::edit] = "Edit";
  mRenderData.rdAppModeMap[appMode::view] = "View";

  // save orig window title, add current mode
  mOrigWindowTitle = mModelInstCamCallbacks.micGetWindowTitleFunction();
  setModeInWindowTitle();

  // image formata needs to be set before Vulkan init
  mRenderData.rdDepthBufferData.format = VK_FORMAT_D16_UNORM;
  mRenderData.rdSSAONoiseBufferData.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  mRenderData.rdShadowMapCombinedDepthBufferData.format = VK_FORMAT_D16_UNORM;

  // XR extensions
  std::vector<const char*> devExts;
  for (const auto& val : deviceExtForXR) {
    devExts.push_back(val.c_str());
    Logger::log(1, "%s : Activate device extension %s\n", __FUNCTION__, val.c_str());
  }
  mRenderData.rdXRDeviceExtensions = devExts;

  std::vector<const char*> instExts;
  for (const auto& val : instExtsForXR) {
    instExts.push_back(val.c_str());
    Logger::log(1, "%s : Activate instance extension %s\n", __FUNCTION__, val.c_str());
  }
  mRenderData.rdXRInstanceExtensions = instExts;

  if (!VkHelper::initVulkan(mRenderData)) {
    return false;
  }
#else
bool VkRenderer::init(GLFWwindow *window, std::vector<std::string> &deviceExtForXR, std::vector<std::string> &instExtsForXR) {
  mRenderData.rdWindow = window;

  int width = 1024;
  int height = 768;
  glfwGetWindowSize(window, &width, &height);
  mRenderData.rdWidth = width;
  mRenderData.rdHalfWidth = width / 2;
  mRenderData.rdHeight = height;
  mRenderData.rdWindowWidth = width;
  mRenderData.rdWindowHeight = height;

  if (const char* envVar = std::getenv("XDG_SESSION_TYPE")) {
    if (std::string(envVar) == "wayland") {
      mRenderData.rdWaylandFound = true;
    }
  }
  unsigned int seed = mRandomDevice();
  mRandomEngine = std::default_random_engine(seed);

  // init app mode map first
  mRenderData.rdAppModeMap[appMode::edit] = "Edit";
  mRenderData.rdAppModeMap[appMode::view] = "View";

  // save orig window title, add current mode
  mOrigWindowTitle = mModelInstCamCallbacks.micGetWindowTitleFunction();
  setModeInWindowTitle();

  // image formata needs to be set before Vulkan init
  mRenderData.rdDepthBufferData.format = VK_FORMAT_D16_UNORM;
  mRenderData.rdSSAONoiseBufferData.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  mRenderData.rdShadowMapCombinedDepthBufferData.format = VK_FORMAT_D16_UNORM;

  // XR extensions
  std::vector<const char*> devExts;
  for (const auto& val : deviceExtForXR) {
    devExts.push_back(val.c_str());
    Logger::log(1, "%s : Activate device extension %s\n", __FUNCTION__, val.c_str());
  }
  mRenderData.rdXRDeviceExtensions = devExts;

  std::vector<const char*> instExts;
  for (const auto& val : instExtsForXR) {
    instExts.push_back(val.c_str());
    Logger::log(1, "%s : Activate instance extension %s\n", __FUNCTION__, val.c_str());
  }
  mRenderData.rdXRInstanceExtensions = instExts;

  if (!VkHelper::initVulkan(mRenderData)) {
    return false;
  }
#endif

  if (!initUserInterface()) {
    return false;
  }

  mWorldBoundaries = std::make_shared<BoundingBox3D>(mRenderData.rdDefaultWorldStartPos, mRenderData.rdDefaultWorldSize);
  mRenderData.rdWorldStartPos = mWorldBoundaries->getFrontTopLeft();
  mRenderData.rdWorldSize = mWorldBoundaries->getSize();
  initOctree(mRenderData.rdOctreeThreshold, mRenderData.rdOctreeMaxDepth);
  Logger::log(1, "%s: octree initialized\n", __FUNCTION__);

  initTriangleOctree(mRenderData.rdLevelOctreeThreshold, mRenderData.rdLevelOctreeMaxDepth);
  Logger::log(1, "%s: triangle octree initialized\n", __FUNCTION__);

  mModelInstCamCallbacks.micOctreeFindAllIntersectionsCallbackFunction = [this]() { return mOctree->findAllIntersections(); };
  mModelInstCamCallbacks.micOctreeGetBoxesCallbackFunction = [this]() { return mOctree->getTreeBoxes(); };
  mModelInstCamCallbacks.micWorldGetBoundariesCallbackFunction = [this]() { return getWorldBoundaries(); };

  // register instance/model callbacks
  mModelInstCamCallbacks.micModelCheckCallbackFunction = [this](std::string fileName) { return hasModel(fileName); };
  mModelInstCamCallbacks.micModelAddCallbackFunction = [this](std::string fileName, bool initialInstance, bool withUndo) { return addModel(fileName, initialInstance, withUndo); };
  mModelInstCamCallbacks.micModelDeleteCallbackFunction = [this](std::string modelName, bool withUndo) { deleteModel(modelName, withUndo); };

  mModelInstCamCallbacks.micInstanceAddCallbackFunction = [this](std::shared_ptr<AssimpModel> model) { return addInstance(model); };
  mModelInstCamCallbacks.micInstanceAddManyCallbackFunction = [this](std::shared_ptr<AssimpModel> model, int numInstances) { addInstances(model, numInstances); };
  mModelInstCamCallbacks.micInstanceDeleteCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, bool withUndo) { deleteInstance(instance, withUndo) ;};
  mModelInstCamCallbacks.micInstanceCloneCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { cloneInstance(instance); };
  mModelInstCamCallbacks.micInstanceCloneManyCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, int numClones) { cloneInstances(instance, numClones); };

  mModelInstCamCallbacks.micInstanceCenterCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { centerInstance(instance); };

  mModelInstCamCallbacks.micDynLightAddCallbackFunction = [this]() { return addDynLight(); };
  mModelInstCamCallbacks.micDynLightDeleteCallbackFunction = [this](std::shared_ptr<AssimpDynLight> light) { deleteDynLight(light); };
  mModelInstCamCallbacks.micDynLightCloneCallbackFunction = [this](std::shared_ptr<AssimpDynLight> light) { cloneDynLight(light); };
  mModelInstCamCallbacks.micDynLightCenterCallbackFunction = [this](std::shared_ptr<AssimpDynLight> light) { centerDynLight(light); };
  mModelInstCamCallbacks.dynLightSphereShadowChangedCallbackFunction = [this]() { generateShaderLightData(); };

  mModelInstCamCallbacks.micUndoCallbackFunction = [this]() { undoLastOperation(); };
  mModelInstCamCallbacks.micRedoCallbackFunction = [this]() { redoLastOperation(); };

  mModelInstCamCallbacks.micLoadConfigCallbackFunction = [this](std::string configFileName) { return loadConfigFile(configFileName); };
  mModelInstCamCallbacks.micSaveConfigCallbackFunction = [this](std::string configFileName) { return saveConfigFile(configFileName); };
  mModelInstCamCallbacks.micNewConfigCallbackFunction = [this]() { createEmptyConfig(); };

  mModelInstCamCallbacks.micSetConfigDirtyCallbackFunction = [this](bool flag) { setConfigDirtyFlag(flag); };
  mModelInstCamCallbacks.micGetConfigDirtyCallbackFunction = [this]() { return getConfigDirtyFlag(); };

  mModelInstCamCallbacks.micCameraCloneCallbackFunction = [this]() { cloneCamera(); };
  mModelInstCamCallbacks.micCameraDeleteCallbackFunction = [this]() { deleteCamera(); };
  mModelInstCamCallbacks.micCameraNameCheckCallbackFunction = [this](std::string cameraName) { return checkCameraNameUsed(cameraName); };

  mModelInstCamCallbacks.micInstanceGetPositionsCallbackFunction = [this]() { return getPositionOfAllInstances(); };
  mModelInstCamCallbacks.micOctreeQueryBBoxCallbackFunction = [this](BoundingBox3D box) { return mOctree->query(box); };

  mModelInstCamCallbacks.micEditNodeGraphCallbackFunction = [this](std::string graphName) { editGraph(graphName); };
  mModelInstCamCallbacks.micCreateEmptyNodeGraphCallbackFunction= [this]() { return createEmptyGraph(); };

  mModelInstCamCallbacks.micInstanceAddBehaviorCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, std::shared_ptr<SingleInstanceBehavior> behavior) {
    addBehavior(instance, behavior);
  };
  mModelInstCamCallbacks.micInstanceDelBehaviorCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { delBehavior(instance); };
  mModelInstCamCallbacks.micModelAddBehaviorCallbackFunction = [this](std::string modelName, std::shared_ptr<SingleInstanceBehavior> behavior) {
    addModelBehavior(modelName, behavior);
  };
  mModelInstCamCallbacks.micModelDelBehaviorCallbackFunction = [this](std::string modelName) { delModelBehavior(modelName); };
  mModelInstCamCallbacks.micNodeEventCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, nodeEvent event) { addBehaviorEvent(instance, event); };
  mModelInstCamCallbacks.micPostNodeTreeDelBehaviorCallbackFunction = [this](std::string nodeTreeName) { postDelNodeTree(nodeTreeName); };

  mModelInstCamCallbacks.micLevelCheckCallbackFunction = [this](std::string levelFileName) { return hasLevel(levelFileName); };
  mModelInstCamCallbacks.micLevelAddCallbackFunction = [this](std::string levelFileName) { return addLevel(levelFileName); };
  mModelInstCamCallbacks.micLevelDeleteCallbackFunction = [this](std::string levelName) { deleteLevel(levelName); };
  mModelInstCamCallbacks.micLevelGenerateLevelDataCallbackFunction = [this]() { generateLevelVertexData(); };

  mModelInstCamCallbacks.micIkIterationsCallbackFunction = [this](int iterations) { mIKSolver.setNumIterations(iterations); };

  mModelInstCamCallbacks.micGetNavTargetsCallbackFunction = [this]() { return getNavTargets(); };

  mRenderData.rdAppExitCallbackFunction = [this]() { doExitApplication(); };
  mModelInstCamCallbacks.micSsetAppModeCallbackFunction = [this](appMode newMode) { setAppMode(newMode); };
  Logger::log(1, "%s: callbacks initialized\n", __FUNCTION__);

  // init camera strings
  mModelInstCamData.micCameraTypeMap[cameraType::free] = "Free";
  mModelInstCamData.micCameraTypeMap[cameraType::firstPerson] = "First Person";
  mModelInstCamData.micCameraTypeMap[cameraType::thirdPerson] = "Third Person";
  mModelInstCamData.micCameraTypeMap[cameraType::stationary] = "Stationary (fixed)";
  mModelInstCamData.micCameraTypeMap[cameraType::stationaryFollowing] = "Stationary (following target)";

  // init other maps
  mModelInstCamData.micMoveStateMap[moveState::idle] = "Idle";
  mModelInstCamData.micMoveStateMap[moveState::walk] = "Walk";
  mModelInstCamData.micMoveStateMap[moveState::run] = "Run";
  mModelInstCamData.micMoveStateMap[moveState::jump] = "Jump";
  mModelInstCamData.micMoveStateMap[moveState::hop] = "Hop";
  mModelInstCamData.micMoveStateMap[moveState::pick] = "Pick";
  mModelInstCamData.micMoveStateMap[moveState::punch] = "Punch";
  mModelInstCamData.micMoveStateMap[moveState::roll] = "Roll";
  mModelInstCamData.micMoveStateMap[moveState::kick] = "Kick";
  mModelInstCamData.micMoveStateMap[moveState::interact] = "Interact";
  mModelInstCamData.micMoveStateMap[moveState::wave] = "Wave";

  mModelInstCamData.micMoveDirectionMap[moveDirection::none] = "None";
  mModelInstCamData.micMoveDirectionMap[moveDirection::forward] = "Forward";
  mModelInstCamData.micMoveDirectionMap[moveDirection::back] = "Backward";
  mModelInstCamData.micMoveDirectionMap[moveDirection::left] = "Left";
  mModelInstCamData.micMoveDirectionMap[moveDirection::right] = "Right";
  mModelInstCamData.micMoveDirectionMap[moveDirection::any] = "Any";

  mModelInstCamData.micNodeUpdateMap[nodeEvent::none] = "None";
  mModelInstCamData.micNodeUpdateMap[nodeEvent::instanceToInstanceCollision] = "Inst to Inst collision";
  mModelInstCamData.micNodeUpdateMap[nodeEvent::instanceToEdgeCollision] = "Inst to Edge collision";
  mModelInstCamData.micNodeUpdateMap[nodeEvent::interaction] = "Interaction";
  mModelInstCamData.micNodeUpdateMap[nodeEvent::instanceToLevelCollision] = "Inst to Level collision";
  mModelInstCamData.micNodeUpdateMap[nodeEvent::navTargetReached] = "Nav Target Reached";

  mModelInstCamData.micFaceAnimationNameMap[faceAnimation::none] = "None";
  mModelInstCamData.micFaceAnimationNameMap[faceAnimation::angry] = "Angry";
  mModelInstCamData.micFaceAnimationNameMap[faceAnimation::worried] = "Worried";
  mModelInstCamData.micFaceAnimationNameMap[faceAnimation::surprised] = "Surprised";
  mModelInstCamData.micFaceAnimationNameMap[faceAnimation::happy] = "Happy";

  mModelInstCamData.micHeadMoveAnimationNameMap[headMoveDirection::left] = "Left";
  mModelInstCamData.micHeadMoveAnimationNameMap[headMoveDirection::right] = "Right";
  mModelInstCamData.micHeadMoveAnimationNameMap[headMoveDirection::up] = "Up";
  mModelInstCamData.micHeadMoveAnimationNameMap[headMoveDirection::down] = "Down";

  // simple color and intensity mapping to timestamps
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::midnight] =
  { 0.0f, 80.0f, 75.0f, 0.1f, glm::vec3(0.28f, 0.27f, 0.6f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::preMorning] =
  { 300.0f, 80.0f, 75.0f, 0.2f, glm::vec3(0.28f, 0.27f, 0.6f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::morning] =
  { 360.0f, 30.0f, 40.0f, 0.8f, glm::vec3(1.0f, 0.5f, 0.0f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::noon] =
  { 720.0f, 90.0f, 40.0f, 0.96f, glm::vec3(1.0f, 0.87f, 0.75f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::evening] =
  { 1080.0f, 150.0f, 40.0f, 0.75f, glm::vec3(0.93f, 0.36f, 0.43f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::postEvening] =
  { 1140.0f, 80.0f, 75.0f, 0.2f, glm::vec3(0.28f, 0.27f, 0.6f) };
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::preMidnight] =
  { 1440.0f, 80.0f, 75.0f, 0.1f, glm::vec3(0.28f, 0.27f, 0.6f) };
  // time is higher than wrap-around, never chosen
  mRenderData.rdTimeOfDayLightSettings[timeOfDay::fullLight] =
  { 2880.0f, 40.0f, 90.0f, 1.0f, glm::vec3(1.0f) };

  Logger::log(1, "%s: enum to string maps initialized\n", __FUNCTION__);

  // valid, but empty line meshes
  mLineMesh = std::make_shared<VkSimpleMesh>();
  mAABBMesh = std::make_shared<VkSimpleMesh>();
  mLevelAABBMesh = std::make_shared<VkSimpleMesh>();
  mLevelOctreeMesh = std::make_shared<VkSimpleMesh>();
  mLevelWireframeMesh = std::make_shared<VkSimpleMesh>();
  mLevelCollidingTriangleMesh = std::make_shared<VkSimpleMesh>();
  mIKFootPointMesh = std::make_shared<VkSimpleMesh>();
  mRenderData.rdLevelWireframeMiniMapMesh = std::make_shared<VkSimpleMesh>();
  mLevelGroundNeighborsMesh = std::make_shared<VkSimpleMesh>();
  mInstancePathMesh = std::make_shared<VkSimpleMesh>();
  Logger::log(1, "%s: line mesh storages initialized\n", __FUNCTION__);

  mSphereModel = SimpleSphereModel(1.0, 5, 8, glm::vec3(1.0f, 1.0f, 1.0f));
  mSphereMesh = mSphereModel.getVertexData();
  Logger::log(1, "%s: Sphere line mesh storage initialized\n", __FUNCTION__);

  mCollidingSphereModel = SimpleSphereModel(1.0, 5, 8, glm::vec3(1.0f, 0.0f, 0.0f));
  mCollidingSphereMesh = mCollidingSphereModel.getVertexData();
  Logger::log(1, "%s: Colliding sphere line mesh storage initialized\n", __FUNCTION__);

  mFullSphereModel = FullSphereModel(1.0, 50, 100, glm::vec3(1.0f, 1.0f, 1.0f));
  mFullSphereMesh = mFullSphereModel.getVertexData();
  for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
    VertexBuffer::uploadData(mRenderData, mRenderData.rdLightSphereVertexBuffers.at(i), mFullSphereMesh.vertices);
  }

  mFullSphereDebugModel = SimpleSphereModel(1.0, 50, 100, glm::vec3(1.0f, 1.0f, 1.0f));
  mFullSphereDebugMesh = mFullSphereDebugModel.getVertexData();
  for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
    VertexBuffer::uploadData(mRenderData, mRenderData.rdLightSphereDebugVertexBuffers.at(i), mFullSphereDebugMesh.vertices);
  }

  Logger::log(1, "%s: Light sphere line mesh storage initialized\n", __FUNCTION__);

  mSkyboxModel.init();
  VkSkyboxMesh skyboxMesh = mSkyboxModel.getVertexData();
  for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
    VertexBuffer::uploadData(mRenderData, mRenderData.rdSkyboxBuffers.at(i), skyboxMesh.vertices);
  }

  const std::string texName = "textures/skybox.jpg";
  if (!Texture::loadCubemapTexture(mRenderData, mRenderData.rdSkyboxTexture, texName, false)) {
    Logger::log(1, "%s error: could not load skybox texture '%s'\n", __FUNCTION__, texName.c_str());
    return false;
  }

  Logger::log(1, "%s: skybox successfully loaded\n", __FUNCTION__);

  VkSimpleMesh lightDebugMesh = mDynLightModel.getVertexData();
  for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
    VertexBuffer::uploadData(mRenderData, mRenderData.rdDynamicLightDebugVertexBuffers.at(i), lightDebugMesh.vertices);
  }
  Logger::log(1, "%s: Dynamic light debug line mesh storage initialized\n", __FUNCTION__);

  mBehaviorManager = std::make_shared<BehaviorManager>();
  mInstanceNodeActionCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, graphNodeType nodeType,
      instanceUpdateType updateType, nodeCallbackVariant data, bool extraSetting) {
    updateInstanceSettings(instance, nodeType, updateType, data, extraSetting);
  };
  mBehaviorManager->setNodeActionCallback(mInstanceNodeActionCallbackFunction);
  Logger::log(1, "%s: behavior data initialized\n", __FUNCTION__);

  mGraphEditor = std::make_shared<GraphEditor>();
  Logger::log(1, "%s: graph editor initialized\n", __FUNCTION__);

  // create glTF model for lights
  mLightModel = std::make_shared<AssimpModel>();
  std::string lightModelName = "assets/lightbulb/lightbulb.glb";
  if (!mLightModel->loadModel(mRenderData, lightModelName)) {
    Logger::log(1, "%s error: could not load light model '%s'\n", __FUNCTION__, lightModelName.c_str());
    return false;
  }
  Logger::log(1, "%s: light model '%s' loaded\n", __FUNCTION__, lightModelName.c_str());

  // create glTF model for VR controllers
  mRHandVRControllerModel = std::make_shared<AssimpModel>();
  std::string rHandControllerModelName = "assets/vr-controller/rhand.glb";
  if (!mRHandVRControllerModel->loadModel(mRenderData, rHandControllerModelName)) {
    Logger::log(1, "%s error: could not load VR Controller right hand model '%s'\n", __FUNCTION__, rHandControllerModelName.c_str());
    return false;
  }
  Logger::log(1, "%s: VR Controller right hand model '%s' loaded\n", __FUNCTION__, rHandControllerModelName.c_str());

  mLHandVRControllerModel = std::make_shared<AssimpModel>();
  std::string lHandControllerModelName = "assets/vr-controller/lhand.glb";
  if (!mLHandVRControllerModel->loadModel(mRenderData, lHandControllerModelName)) {
    Logger::log(1, "%s error: could not load VR Controller left hand model '%s'\n", __FUNCTION__, lHandControllerModelName.c_str());
    return false;
  }
  Logger::log(1, "%s: VR Controller left hand model '%s' loaded\n", __FUNCTION__, lHandControllerModelName.c_str());

  mVRHandWorldPosMatrices.resize(2);
  Logger::log(1, "%s: VR Controller mode data initialized\n", __FUNCTION__);

  // try to load the default configuration file
  if (loadConfigFile(mDefaultConfigFileName)) {
    Logger::log(1, "%s: loaded default config file '%s'\n", __FUNCTION__, mDefaultConfigFileName.c_str());
  } else {
    Logger::log(1, "%s: could not load default config file '%s'\n", __FUNCTION__, mDefaultConfigFileName.c_str());
    // clear everything and add null model/instance/settings container
    createEmptyConfig();
  }

  // We must flip Y axis to match Vulkan viewport
  mVulkanViewCorrectionMatrix = glm::mat4(1.0f);
  mVulkanViewCorrectionMatrix[1][1] = -1.0f;

  // Cascaded Shadow map init
  mRenderData.rdShadowMapCascadeData.cascades.resize(mRenderData.SHADOW_MAP_LAYERS);

  // dynamic light shadow map init
  mRenderData.rdDynamicLightShadowMapData.cascades.resize(6);

  // shadow maps need camera, so do after config creation
  updateShadowMapCascades();

  mRenderData.rdFrameTimer.start();

  Logger::log(1, "%s: Vulkan renderer initialized to %ix%i\n", __FUNCTION__, width, height);

  mApplicationRunning = true;
  return true;
}

VkDevice VkRenderer::getDevice() {
  return mRenderData.rdVkbDevice.device;
}

VkPhysicalDevice VkRenderer::getPhysicalDevice() {
  return mRenderData.rdVkbPhysicalDevice.physical_device;
}

void VkRenderer::setPhysicalDevice(VkPhysicalDevice physDevice) {
  mRenderData.rdVkbPhysicalDevice.physical_device = physDevice;
}

void VkRenderer::setRendererMICCallbacks(ModelInstanceCamCallbacks &callbacks) {
  mModelInstCamCallbacks = callbacks;
}

VkInstance VkRenderer::getInstance() {
  return mRenderData.rdVkbInstance.instance;
}

std::pair<uint32_t, uint32_t> VkRenderer::getQueueFamilyAndIndex() {
  uint32_t queueFamilyIndex = 0;
  uint32_t queueIndex = 0;

  std::vector<VkQueueFamilyProperties> queueFamilies = mRenderData.rdVkbPhysicalDevice.get_queue_families();

  int i = 0;
  for (const auto& queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      queueFamilyIndex = i;
      break;
    }

    ++i;
  }

  auto queueIndexRet = mRenderData.rdVkbDevice.get_queue_index(vkb::QueueType::graphics);
  if (queueIndexRet.has_value()) {
    queueIndex = queueIndexRet.value();
  }

  Logger::log(1, "%s: Found graphics queue familiy at index %i, graphics queue at index %i\n", __FUNCTION__, queueFamilyIndex, queueIndex);

  return std::make_pair(queueFamilyIndex, queueIndex);
}

bool VkRenderer::createXRPipeline(VkFormat format) {
  return VkHelper::createXRPipeline(mRenderData, format);
}

void VkRenderer::destroyXRPipeline() {
  return VkHelper::destroyXRPipeline(mRenderData);
}

std::pair<float, float> VkRenderer::getNearAndFarPlane() {
  return std::make_pair(mRenderData.rdNearPlane, mRenderData.rdFarPlane);
}

ModelInstanceCamData& VkRenderer::getModInstCamData() {
  return mModelInstCamData;
}

bool VkRenderer::loadConfigFile(std::string configFileName) {
  YamlParser parser;
  if (!parser.loadYamlFile(configFileName)) {
    return false;
  }

  std::string yamlFileVersion = parser.getFileVersion();
  if (yamlFileVersion.empty()) {
    Logger::log(1, "%s error: could not check file version of YAML config file '%s'\n", __FUNCTION__, parser.getFileName().c_str());
    return false;
  }

  // we delete all models and instances at this point, the requesting dialog has been confirmed
  removeAllModelsAndInstances();

  // reset octree display
  resetLevelData();

  // load level data
  std::vector<LevelSettings> savedLevelSettings = parser.getLevelConfigs();
  if (savedLevelSettings.empty()) {
    Logger::log(1, "%s warning: no level in file '%s', skipping\n", __FUNCTION__, parser.getFileName().c_str());
  } else {
    for (auto& levelSetting : savedLevelSettings) {
      // skip level data generation here, will be done after all levels are loaded
      if (!addLevel(levelSetting.lsLevelFilenamePath, false)) {
        return false;
      }

      std::shared_ptr<AssimpLevel> level = getLevel(levelSetting.lsLevelFilenamePath);
      if (!level) {
        return false;
      }

      level->setLevelSettings(levelSetting);
    }

    // restore level settings before generating the level data 
    mRenderData.rdEnableSimpleGravity = parser.getGravityEnabled();
    mRenderData.rdMaxLevelGroundSlopeAngle = parser.getMaxGroundSlopeAngle();
    mRenderData.rdMaxStairstepHeight = parser.getMaxStairStepHeight();

    // regenerate vertex data
    generateLevelVertexData();

    // restore selected level num
    int selectedLevel = parser.getSelectedLevelNum();
    if (selectedLevel < mModelInstCamData.micLevels.size()) {
      mModelInstCamData.micSelectedLevel = selectedLevel;
    } else {
      mModelInstCamData.micSelectedLevel = 0;
    }
  }

  // get models
  std::vector<ModelSettings> savedModelSettings = parser.getModelConfigs();
  if (savedModelSettings.empty()) {
    Logger::log(1, "%s error: no model files in file '%s'\n", __FUNCTION__, parser.getFileName().c_str());
    return false;
  }

  for (auto& modSetting : savedModelSettings) {
    if (!addModel(modSetting.msModelFilenamePath, false, false)) {
      return false;
    }
    std::shared_ptr<AssimpModel> model = getModel(modSetting.msModelFilenamePath);
    if (!model) {
      return false;
    }

    // migration config version 3.0 to 4.0+ 
    if (yamlFileVersion == "3.0") {
      Logger::log(1, "%s: adding empty bounding sphere adjustment vector\n", __FUNCTION__);
      std::vector<glm::vec4> boundingSphereAdjustments = model->getModelSettings().msBoundingSphereAdjustments;
      modSetting.msBoundingSphereAdjustments = boundingSphereAdjustments;
    }

    model->setModelSettings(modSetting);
  }

  // restore selected model number
  int selectedModel = parser.getSelectedModelNum();
  if (selectedModel < mModelInstCamData.micModelList.size()) {
    mModelInstCamData.micSelectedModel = selectedModel;
  } else {
    mModelInstCamData.micSelectedModel = 0;
  }

  // get node trees for behavior, needed to be set (copied) in instances
  std::vector<ExtendedBehaviorData> behaviorData = parser.getBehaviorData();
  if (behaviorData.empty()) {
    Logger::log(1, "%s error: no behaviors in file '%s', skipping\n", __FUNCTION__, parser.getFileName().c_str());
  } else {
    for (const auto& behavior : behaviorData) {
      Logger::log(1, "%s: found behavior '%s'\n", __FUNCTION__, behavior.bdName.c_str());

      std::shared_ptr<SingleInstanceBehavior> newBehavior = std::make_shared<SingleInstanceBehavior>();
      std::shared_ptr<GraphNodeFactory> factory = std::make_shared<GraphNodeFactory>([&](int nodeId) { newBehavior->updateNodeStatus(nodeId); });

      std::shared_ptr<BehaviorData> data = newBehavior->getBehaviorData();
      for (const auto& link : behavior.bdGraphLinks) {
        Logger::log(1, "%s: found link %i from out pin %i to in pin %i\n", __FUNCTION__, link.first, link.second.first, link.second.second);
      }
      data->bdGraphLinks = behavior.bdGraphLinks;

      for (const auto& nodeData : behavior.nodeImportData) {
        data->bdGraphNodes.emplace_back(factory->makeNode(nodeData.nodeType, nodeData.nodeId));
        Logger::log(1, "%s: created new node %i with type %i\n", __FUNCTION__, nodeData.nodeId, nodeData.nodeType);

        int newNodeId = nodeData.nodeId;
        const auto iter = std::find_if(data->bdGraphNodes.begin(), data->bdGraphNodes.end(), [newNodeId](const auto& existingNode) {
          return existingNode->getNodeId() == newNodeId;
        });

        for (const auto& prop : nodeData.nodeProperties) {
          Logger::log(1, "%s: %s has prop %s\n", __FUNCTION__, prop.first.c_str(), prop.second.c_str());
        }
        if (iter != data->bdGraphNodes.end()) {
          (*iter)->importData(nodeData.nodeProperties);
        }
      }

      data->bdEditorSettings = behavior.bdEditorSettings;
      data->bdName = behavior.bdName;

      mModelInstCamData.micBehaviorData.emplace(behavior.bdName, std::move(newBehavior));
    }
  }

  // load instances
  std::vector<ExtendedInstanceSettings> savedInstanceSettings = parser.getInstanceConfigs();
  if (savedInstanceSettings.empty()) {
    Logger::log(1, "%s warning: no instance in file '%s'\n", __FUNCTION__, parser.getFileName().c_str());
    return false;
  }

  for (const auto& instSettings : savedInstanceSettings) {
    std::shared_ptr<AssimpInstance> newInstance = addInstance(getModel(instSettings.isModelFile), false);
    newInstance->setInstanceSettings(instSettings);
  }

  assignInstanceIndices();

  // restore selected instance num
  int selectedInstance = parser.getSelectedInstanceNum();
  if (selectedInstance < mModelInstCamData.micAssimpInstances.size()) {
    mModelInstCamData.micSelectedInstance = selectedInstance;
  } else {
    mModelInstCamData.micSelectedInstance = 0;
  }

  // restore behavior data after IDs are restored
  for (auto& instance : mModelInstCamData.micAssimpInstances) {
    InstanceSettings instSettings = instance->getInstanceSettings();
    if (!instSettings.isNodeTreeName.empty()) {
      addBehavior(instance, mModelInstCamData.micBehaviorData.at(instSettings.isNodeTreeName));
    }
  }

  // make sure we have the default cam
  loadDefaultFreeCam();

  // load cameras
  std::vector<CameraSettings> savedCamSettings = parser.getCameraConfigs();
  if (savedCamSettings.empty()) {
    Logger::log(1, "%s warning: no cameras in file '%s', fallback to default\n", __FUNCTION__, parser.getFileName().c_str());
  } else {
    for (const auto& setting : savedCamSettings) {
      // camera instance zero is always available, just import settings
      if (setting.csCamName == "FreeCam") {
        Logger::log(1, "%s: restore FreeCam\n", __FUNCTION__);
        mModelInstCamData.micCameras.at(0)->setCameraSettings(setting);
      } else {
        Logger::log(1, "%s: restore camera %s\n", __FUNCTION__, setting.csCamName.c_str());
        std::shared_ptr<Camera> newCam = std::make_shared<Camera>();
        newCam->setCameraSettings(setting);
        mModelInstCamData.micCameras.emplace_back(newCam);
      }
    }

    // now try to set the camera targets back to the chosen instances
    for (int i = 0; i < savedInstanceSettings.size(); ++i) {
      if (!savedInstanceSettings.at(i).eisCameraNames.empty()) {
        for (const auto& camName : savedInstanceSettings.at(i).eisCameraNames) {
          // skip over null instance
          int instanceId = i + 1;

          // double check
          if (instanceId < mModelInstCamData.micAssimpInstances.size()) {
            Logger::log(1, "%s: restore camera instance settings for instance %i (cam: %s)\n", __FUNCTION__, instanceId, camName.c_str());
            std::shared_ptr<AssimpInstance> instanceToFollow = mModelInstCamData.micAssimpInstances.at(instanceId);

            auto iter = std::find_if(mModelInstCamData.micCameras.begin(), mModelInstCamData.micCameras.end(), [camName](std::shared_ptr<Camera> cam) {
              return cam->getCameraSettings().csCamName == camName;
            });
            if (iter != mModelInstCamData.micCameras.end()) {
              (*iter)->setInstanceToFollow(instanceToFollow);
            }
          }
        }
      }
    }

    // restore selected camera num
    int selectedCamera = parser.getSelectedCameraNum();
    if (selectedCamera < mModelInstCamData.micCameras.size()) {
      mModelInstCamData.micSelectedCamera = selectedCamera;
    } else {
      mModelInstCamData.micSelectedCamera = 0;
    }
  }

  // reset light data
  resetLightData();

  // load light data
  std::vector<DynamicLightSettings> savedLightSettings = parser.getDynLightConfigs();
  if (savedLevelSettings.empty()) {
    Logger::log(1, "%s info: no light data in file '%s', skipping\n", __FUNCTION__, parser.getFileName().c_str());
  } else {
    for (auto& lightSetting : savedLightSettings) {
      std::shared_ptr<AssimpDynLight> light = addDynLight();
      if (!light) {
        return false;
      }

      light->setDynLightSettings(lightSetting);
    }

    // re-index after all ligths were added
    assignLightIndices();

    // regenerate light data
    generateShaderLightData();

    // restore selected level num
    int selectedLight = parser.getSelectedDynLightNum();
    if (selectedLight < mModelInstCamData.micDynLights.size()) {
      mModelInstCamData.micSelectedDynLight = selectedLight;
    } else {
      mModelInstCamData.micSelectedDynLight = 0;
    }
  }


  // restore hightlight status, set default edit mode
  mRenderData.rdHighlightSelectedInstance = parser.getHighlightActivated();
  mRenderData.rdInstanceEditMode = instanceEditMode::move;

  // restore collision and interaction settings
  mRenderData.rdCheckCollisions = parser.getCollisionChecksEnabled();
  mRenderData.rdInteraction = parser.getInteractionEnabled();
  mRenderData.rdInteractionMinRange = parser.getInteractionMinRange();
  mRenderData.rdInteractionMaxRange = parser.getInteractionMaxRange();
  mRenderData.rdInteractionFOV = parser.getInteractionFOV();
  mRenderData.rdEnableFeetIK = parser.getIKEnabled();
  mRenderData.rdNumberOfIkIteratons = parser.getIKNumIterations();
  mRenderData.rdEnableNavigation = parser.getNavEnabled();
  mRenderData.rdDrawSkybox = parser.getSkyboxEnabled();
  mRenderData.rdFogDensity = parser.getFogDensity();
  mRenderData.rdLightSourceAngleEastWest = parser.getLightSourceAngleEastWest();
  mRenderData.rdLightSourceAngleNorthSouth = parser.getLightSourceAngleNorthSouth();
  mRenderData.rdLightSourceIntensity = parser.getLightSouceIntensity();
  mRenderData.rdLightSourceColor = parser.getLightSourceColor();
  mRenderData.rdEnableTimeOfDay = parser.getTimeOfDayEnabled();
  mRenderData.rdTimeScaleFactor = parser.getTimeOfDayScaleFactor();
  mRenderData.rdTimeOfDayPreset = parser.getTimeOfDayPreset();
  mRenderData.rdEnableSSAO = parser.getSSAOEnabled();
  mRenderData.rdSSAORadius = parser.getSSAORadius();
  mRenderData.rdSSAOBias = parser.getSSAOBias();
  mRenderData.rdSSAOExponent = parser.getSSAOExponent();
  mRenderData.rdEnableSSAOBlur = parser.getSSAOBlurEnabled();
  mRenderData.rdSSAOBlurRadius = parser.getSSAOBlurRadius();
  mRenderData.rdEnableShadowMap = parser.getShadowMapEnabled();
  mRenderData.rdShadowMapSplitLambda = parser.getShadowMapSplitLambda();
  mRenderData.rdShadowMapConstantDepthBias = parser.getShadowMapConstantDepthBias();
  mRenderData.rdShadowMapSlopeDepthBias = parser.getShadowMapSlopeDepthBias();
  mRenderData.rdEnableShadowMapPCF = parser.getShadowMapPFCEnabled();
  mRenderData.rdShadowMapPCFScale = parser.getShadowMapPCFScale();
  mRenderData.rdShadowMapPCFRange = parser.getShadowMapPCFRange();
  mRenderData.rdDynLightShadowMapConstantDepthBias = parser.getDynLightShadowMapConstantDepthBias();
  mRenderData.rdDynLightShadowMapSlopeDepthBias = parser.getDynLightShadowMapSlopeDepthBias();
  mRenderData.rdNearPlane = parser.getNearPlane();
  mRenderData.rdFarPlane = parser.getFarPlane();

  return true;
}

bool VkRenderer::saveConfigFile(std::string configFileName) {
  if (mModelInstCamData.micAssimpInstancesPerModel.size() == 1) {
    Logger::log(1, "%s error: nothing to save (no models)\n", __FUNCTION__);
    return false;
  }

  YamlParser parser;
  if (!parser.createConfigFile(mRenderData, mModelInstCamData)) {
    Logger::log(1, "%s error: could not create YAML config file!\n", __FUNCTION__);
    return false;
  }

  return parser.writeYamlFile(configFileName);
}

void VkRenderer::createEmptyConfig() {
  removeAllModelsAndInstances();
  resetLevelData();
  loadDefaultFreeCam();
  resetLightData();
}

void VkRenderer::requestExitApplication() {
  // set app mode back to edit to show windows
  mRenderData.rdApplicationMode = appMode::edit;
  mRenderData.rdRequestApplicationExit = true;
}

void VkRenderer::doExitApplication() {
  mApplicationRunning = false;
}

void VkRenderer::undoLastOperation() {
  if (mModelInstCamData.micSettingsContainer->getUndoSize() == 0) {
    return;
  }

  mModelInstCamData.micSettingsContainer->undo();
  /* we need to update the index numbers in case instances were deleted,
   * and the settings files still contain the old index number */
  assignInstanceIndices();

  int selectedInstance = mModelInstCamData.micSettingsContainer->getCurrentInstance();
  if (selectedInstance < mModelInstCamData.micAssimpInstances.size()) {
    mModelInstCamData.micSelectedInstance = mModelInstCamData.micSettingsContainer->getCurrentInstance();
  } else {
    mModelInstCamData.micSelectedInstance = 0;
  }

  // if we made all changes undone, the config is no longer dirty
  if (mModelInstCamData.micSettingsContainer->getUndoSize() == 0) {
    setConfigDirtyFlag(false);
  }
}

void VkRenderer::redoLastOperation() {
  if (mModelInstCamData.micSettingsContainer->getRedoSize() == 0) {
    return;
  }

  mModelInstCamData.micSettingsContainer->redo();
  assignInstanceIndices();

  int selectedInstance = mModelInstCamData.micSettingsContainer->getCurrentInstance();
  if (selectedInstance < mModelInstCamData.micAssimpInstances.size()) {
    mModelInstCamData.micSelectedInstance = mModelInstCamData.micSettingsContainer->getCurrentInstance();
  } else {
    mModelInstCamData.micSelectedInstance = 0;
  }

  // if any changes have been re-done, the config is dirty
  if (mModelInstCamData.micSettingsContainer->getUndoSize() > 0) {
    setConfigDirtyFlag(true);
  }
}

void VkRenderer::addNullModelAndInstance() {
  // create an empty null model and an instance from it
  std::shared_ptr<AssimpModel> nullModel = std::make_shared<AssimpModel>();
  mModelInstCamData.micModelList.emplace_back(nullModel);

  std::shared_ptr<AssimpInstance> nullInstance = std::make_shared<AssimpInstance>(nullModel);
  mModelInstCamData.micAssimpInstancesPerModel[nullModel->getModelFileName()].emplace_back(nullInstance);
  mModelInstCamData.micAssimpInstances.emplace_back(nullInstance);
  assignInstanceIndices();

  // init the central settings container
  mModelInstCamData.micSettingsContainer.reset();
  mModelInstCamData.micSettingsContainer = std::make_shared<AssimpSettingsContainer>(nullInstance);
}

void VkRenderer::createSettingsContainerCallbacks() {
  mModelInstCamData.micSettingsContainer->getSelectedModelCallbackFunction = [this]() {return mModelInstCamData.micSelectedModel; };
  mModelInstCamData.micSettingsContainer->setSelectedModelCallbackFunction = [this](int modelId) { mModelInstCamData.micSelectedModel = modelId; };

  mModelInstCamData.micSettingsContainer->modelDeleteCallbackFunction = [this](std::string modelFileName, bool withUndo) { deleteModel(modelFileName, withUndo); };
  mModelInstCamData.micSettingsContainer->modelAddCallbackFunction = [this](std::string modelFileName, bool initialInstance, bool withUndo) { return addModel(modelFileName, initialInstance, withUndo); };
  mModelInstCamData.micSettingsContainer->modelAddExistingCallbackFunction = [this](std::shared_ptr<AssimpModel> model, int indexPos) { addExistingModel(model, indexPos); };


  mModelInstCamData.micSettingsContainer->getSelectedInstanceCallbackFunction = [this]() { return mModelInstCamData.micSelectedInstance; };
  mModelInstCamData.micSettingsContainer->setSelectedInstanceCallbackFunction = [this](int instanceId) { mModelInstCamData.micSelectedInstance = instanceId; };

  mModelInstCamData.micSettingsContainer->getInstanceEditModeCallbackFunction = [this]() { return mRenderData.rdInstanceEditMode; };
  mModelInstCamData.micSettingsContainer->setInstanceEditModeCallbackFunction = [this](instanceEditMode mode) { mRenderData.rdInstanceEditMode = mode; };

  mModelInstCamData.micSettingsContainer->instanceGetModelCallbackFunction = [this](std::string fileName) { return getModel(fileName); };
  mModelInstCamData.micSettingsContainer->instanceAddCallbackFunction = [this](std::shared_ptr<AssimpModel> model) { return addInstance(model); };
  mModelInstCamData.micSettingsContainer->instanceAddExistingCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, int indexPos, int indexPerModelPos)
    { addExistingInstance(instance, indexPos, indexPerModelPos); };
  mModelInstCamData.micSettingsContainer->instanceDeleteCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, bool withUndo) { deleteInstance(instance, withUndo) ;};
}

void VkRenderer::clearUndoRedoStacks() {
  mModelInstCamData.micSettingsContainer->removeStacks();
}

void VkRenderer::removeAllModelsAndInstances() {
  mModelInstCamData.micSelectedInstance = 0;
  mModelInstCamData.micSelectedModel = 0;
  mModelInstCamData.micSelectedLevel = 0;

  mModelInstCamData.micAssimpInstances.erase(mModelInstCamData.micAssimpInstances.begin(),
    mModelInstCamData.micAssimpInstances.end());
  mModelInstCamData.micAssimpInstancesPerModel.clear();

  // add models to pending delete list
  for (const auto& model : mModelInstCamData.micModelList) {
    if (model && (model->getTriangleCount() > 0)) {
      mModelInstCamData.micPendingDeleteAssimpModels.insert(model);
    }
  }

  mModelInstCamData.micModelList.erase(mModelInstCamData.micModelList.begin(), mModelInstCamData.micModelList.end());

  // reset behavior data and graphEditor
  mBehaviorManager->clear();
  mModelInstCamData.micBehaviorData.clear();
  mGraphEditor = std::make_shared<GraphEditor>();

  // no instances, no dirty flag (catches 'load' and 'new')
  setConfigDirtyFlag(false);

  // re-add null model and instance
  addNullModelAndInstance();

  // add callbacks
  createSettingsContainerCallbacks();

  // kill undo and redo stacks too
  clearUndoRedoStacks();

  // reset collision settings
  resetCollisionData();

  // reset light data
  resetLightData();

  updateTriangleCount();
  updateLevelTriangleCount();
}

void VkRenderer::resetCollisionData() {
  mModelInstCamData.micInstanceCollisions.clear();

  mRenderData.rdNumberOfCollisions = 0;
  mRenderData.rdCheckCollisions = collisionChecks::none;
  mRenderData.rdDrawCollisionAABBs = collisionDebugDraw::none;
  mRenderData.rdDrawBoundingSpheres = collisionDebugDraw::none;
}

void VkRenderer::loadDefaultFreeCam() {
  mModelInstCamData.micCameras.clear();

  std::shared_ptr<Camera> freeCam = std::make_shared<Camera>();
  CameraSettings freeCamSettings{};
  freeCamSettings.csCamName = "FreeCam";
  freeCamSettings.csWorldPosition = glm::vec3(5.0f);
  freeCamSettings.csViewAzimuth = 0.0f;
  freeCamSettings.csViewElevation = 0.0f;

  freeCam->setCameraSettings(freeCamSettings);
  mModelInstCamData.micCameras.emplace_back(freeCam);

  mModelInstCamData.micSelectedCamera = 0;
}

bool VkRenderer::recreateSwapchain() {
  bool result = VkHelper::recreateSwapchain(mRenderData);
  mUserInterface.updateDescriptorSets(mRenderData);

  return result;
}

bool VkRenderer::initUserInterface() {
  if (!mUserInterface.init(mRenderData)) {
    Logger::log(1, "%s error: could not init ImGui\n", __FUNCTION__);
    return false;
  }
  return true;
}

bool VkRenderer::hasModel(std::string modelFileName) {
  auto modelIter =  std::find_if(mModelInstCamData.micModelList.begin(), mModelInstCamData.micModelList.end(),
    [modelFileName](const auto& model) {
      return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
    });
  return modelIter != mModelInstCamData.micModelList.end();
}

std::shared_ptr<AssimpModel> VkRenderer::getModel(std::string modelFileName) {
  auto modelIter =  std::find_if(mModelInstCamData.micModelList.begin(), mModelInstCamData.micModelList.end(),
    [modelFileName](const auto& model) {
      return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
    });
  if (modelIter != mModelInstCamData.micModelList.end()) {
    return *modelIter;
  }
  return nullptr;
}

bool VkRenderer::addModel(std::string modelFileName, bool addInitialInstance, bool withUndo) {
  if (hasModel(modelFileName)) {
    Logger::log(1, "%s warning: model '%s' already existed, skipping\n", __FUNCTION__, modelFileName.c_str());
    return false;
  }

  std::shared_ptr<AssimpModel> model = std::make_shared<AssimpModel>();
  if (!model->loadModel(mRenderData, modelFileName)) {
    Logger::log(1, "%s error: could not load model file '%s'\n", __FUNCTION__, modelFileName.c_str());
    return false;
  }

  mModelInstCamData.micModelList.emplace_back(model);

  int prevSelectedModelId = mModelInstCamData.micSelectedModel;
  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  std::shared_ptr<AssimpInstance> firstInstance;
  if (addInitialInstance) {
    // also add a new instance here to see the model, but skip undo recording the new instance
    firstInstance = addInstance(model, false);
    if (!firstInstance) {
      Logger::log(1, "%s error: could not add initial instance for model '%s'\n", __FUNCTION__, modelFileName.c_str());
      return false;
    }

    // center the first real model instance
    if (mModelInstCamData.micAssimpInstances.size() == 2) {
      centerInstance(firstInstance);
    }
  }

  // select new model and new instance
  mModelInstCamData.micSelectedModel = mModelInstCamData.micModelList.size() - 1;
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;

  if (withUndo) {
    mModelInstCamData.micSettingsContainer->applyLoadModel(model, mModelInstCamData.micSelectedModel, firstInstance,
      mModelInstCamData.micSelectedModel, prevSelectedModelId,
      mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);
  }

  // create AABBs for the model
  if (!createAABBLookup(model)) {
    return false;
  }

  return true;
}

void VkRenderer::addExistingModel(std::shared_ptr<AssimpModel> model, int indexPos) {
  Logger::log(2, "%s: inserting model %s on pos %i\n", __FUNCTION__, model->getModelFileName().c_str(), indexPos);
  mModelInstCamData.micModelList.insert(mModelInstCamData.micModelList.begin() + indexPos, model);
}

void VkRenderer::deleteModel(std::string modelFileName, bool withUndo) {
  std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();

  int prevSelectedModelId = mModelInstCamData.micSelectedModel;
  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  mModelInstCamData.micAssimpInstances.erase(
    std::remove_if(
      mModelInstCamData.micAssimpInstances.begin(),
      mModelInstCamData.micAssimpInstances.end(),
      [shortModelFileName](std::shared_ptr<AssimpInstance> instance) { return instance->getModel()->getModelFileName() == shortModelFileName; }
    ), mModelInstCamData.micAssimpInstances.end()
  );

  std::vector<std::shared_ptr<AssimpInstance>> deletedInstances;
  std::shared_ptr<AssimpModel> model = getModel(modelFileName);

  auto modelIndex = std::find_if(mModelInstCamData.micModelList.begin(), mModelInstCamData.micModelList.end(),
    [modelFileName](std::shared_ptr<AssimpModel> model) { return model->getModelFileName() == modelFileName; }
  );

  size_t indexPos = mModelInstCamData.micModelList.size() - 1;
  if (modelIndex != mModelInstCamData.micModelList.end()) {
    indexPos = modelIndex - mModelInstCamData.micModelList.begin();
  }

  if (mModelInstCamData.micAssimpInstancesPerModel.count(shortModelFileName) > 0) {
    deletedInstances.swap(mModelInstCamData.micAssimpInstancesPerModel[shortModelFileName]);
  }

  if (model && (model->getTriangleCount() > 0)) {
    mModelInstCamData.micPendingDeleteAssimpModels.insert(model);
  }

  mModelInstCamData.micModelList.erase(
    std::remove_if(
      mModelInstCamData.micModelList.begin(),
      mModelInstCamData.micModelList.end(),
      [modelFileName](std::shared_ptr<AssimpModel> model) {
        return model->getModelFileName() == modelFileName; }
    )
  );

  // decrement selected model index to point to model that is in list before the deleted one
  if (mModelInstCamData.micSelectedModel > 1) {
    mModelInstCamData.micSelectedModel -= 1;
  }

  // reset model instance to first instance
  if (mModelInstCamData.micAssimpInstances.size() > 1) {
    mModelInstCamData.micSelectedInstance = 1;
  }

  // if we have only the null instance left, disable selection
  if (mModelInstCamData.micAssimpInstances.size() == 1) {
    mModelInstCamData.micSelectedInstance = 0;
    mRenderData.rdHighlightSelectedInstance = false;
  }

  if (withUndo) {
    mModelInstCamData.micSettingsContainer->applyDeleteModel(model, indexPos, deletedInstances,
      mModelInstCamData.micSelectedModel, prevSelectedModelId,
      mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);
  }

  assignInstanceIndices();
  updateTriangleCount();
}

std::shared_ptr<AssimpInstance> VkRenderer::getInstanceById(int instanceId) {
  if (instanceId < mModelInstCamData.micAssimpInstances.size()) {
    return mModelInstCamData.micAssimpInstances.at(instanceId);
  } else {
    Logger::log(1, "%s error: instance id %i out of range, we only have %i instances\n", __FUNCTION__, instanceId,  mModelInstCamData.micAssimpInstances.size());
    return mModelInstCamData.micAssimpInstances.at(0);
  }
}

std::shared_ptr<AssimpInstance> VkRenderer::addInstance(std::shared_ptr<AssimpModel> model, bool withUndo) {
  std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
  mModelInstCamData.micAssimpInstances.emplace_back(newInstance);
  mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);

  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  // select new instance
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;
  if (withUndo) {
    mModelInstCamData.micSettingsContainer->applyNewInstance(newInstance,
      mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);
  }

  assignInstanceIndices();
  updateTriangleCount();

  // select new instance
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;

  // deselect light
  mModelInstCamData.micSelectedDynLight = 0;

  return newInstance;
}

void VkRenderer::addExistingInstance(std::shared_ptr<AssimpInstance> instance, int indexPos, int indexPerModelPos) {
  Logger::log(2, "%s: inserting instance on pos %i\n", __FUNCTION__, indexPos);
  mModelInstCamData.micAssimpInstances.insert(mModelInstCamData.micAssimpInstances.begin() + indexPos, instance);
  mModelInstCamData.micAssimpInstancesPerModel[instance->getModel()->getModelFileName()].insert(
    mModelInstCamData.micAssimpInstancesPerModel[instance->getModel()->getModelFileName()].begin() +
    indexPerModelPos, instance);

  assignInstanceIndices();
  updateTriangleCount();
}

void VkRenderer::addInstances(std::shared_ptr<AssimpModel> model, int numInstances) {
  size_t animClipNum = model->getAnimClips().size();
  std::vector<std::shared_ptr<AssimpInstance>> newInstances;

  std::uniform_int_distribution<int> randomPosInts(-125, 125);
  std::uniform_int_distribution<int> randomRotInts(-180, 180);
  std::uniform_int_distribution<int> randomAnimInts(0, animClipNum - 1);
  std::uniform_real_distribution<float> randomSpeedFloats(0.5f, 1.25f);

  for (int i = 0; i < numInstances; ++i) {
    int xPos = randomPosInts(mRandomEngine);
    int zPos = randomPosInts(mRandomEngine);
    int rotation = randomRotInts(mRandomEngine);
    int clipNr = randomAnimInts(mRandomEngine);
    float animSpeed = randomSpeedFloats(mRandomEngine);

    std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
    if (animClipNum > 0) {
      InstanceSettings instSettings = newInstance->getInstanceSettings();
      instSettings.isFirstAnimClipNr = clipNr;
      instSettings.isSecondAnimClipNr = clipNr;
      instSettings.isAnimSpeedFactor = animSpeed;
      instSettings.isAnimBlendFactor = 0.0f;
      newInstance->setInstanceSettings(instSettings);
    }
    newInstances.emplace_back(newInstance);
    mModelInstCamData.micAssimpInstances.emplace_back(newInstance);
    mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
  }

  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  // select new instance
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;
  mModelInstCamData.micSettingsContainer->applyNewMultiInstance(newInstances, mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);

  // deselect light
  mModelInstCamData.micSelectedDynLight = 0;

  assignInstanceIndices();
  updateTriangleCount();
}

void VkRenderer::deleteInstance(std::shared_ptr<AssimpInstance> instance, bool withUndo) {
  std::shared_ptr<AssimpModel> currentModel = instance->getModel();
  std::string currentModelName = currentModel->getModelFileName();

  mModelInstCamData.micAssimpInstances.erase(
    std::remove_if(
      mModelInstCamData.micAssimpInstances.begin(),
      mModelInstCamData.micAssimpInstances.end(),
      [instance](std::shared_ptr<AssimpInstance> inst) { return inst == instance; }),
      mModelInstCamData.micAssimpInstances.end());

  mModelInstCamData.micAssimpInstancesPerModel[currentModelName].erase(
    std::remove_if(
      mModelInstCamData.micAssimpInstancesPerModel[currentModelName].begin(),
      mModelInstCamData.micAssimpInstancesPerModel[currentModelName].end(),
      [instance](std::shared_ptr<AssimpInstance> inst) { return inst == instance; }),
      mModelInstCamData.micAssimpInstancesPerModel[currentModelName].end());

  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  // reset to previous instance
  if (mModelInstCamData.micSelectedInstance > 0) {
    mModelInstCamData.micSelectedInstance -= 1;
    // deselect light
    mModelInstCamData.micSelectedDynLight = 0;
  }

  if (withUndo) {
    mModelInstCamData.micSettingsContainer->applyDeleteInstance(instance, mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);
  }

  assignInstanceIndices();
  updateTriangleCount();
}

void VkRenderer::cloneInstance(std::shared_ptr<AssimpInstance> instance) {
  std::shared_ptr<AssimpModel> currentModel = instance->getModel();
  std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(currentModel);
  InstanceSettings newInstanceSettings = instance->getInstanceSettings();

  // slight offset to see new instance
  newInstanceSettings.isWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
  newInstance->setInstanceSettings(newInstanceSettings);

  mModelInstCamData.micAssimpInstances.emplace_back(newInstance);
  mModelInstCamData.micAssimpInstancesPerModel[currentModel->getModelFileName()].emplace_back(newInstance);

  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  // select new instance
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;
  mModelInstCamData.micSettingsContainer->applyNewInstance(newInstance, mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);

  // deselect light
  mModelInstCamData.micSelectedDynLight = 0;

  assignInstanceIndices();

  // add behavior tree after new id was set
  newInstanceSettings = newInstance->getInstanceSettings();
  if (!newInstanceSettings.isNodeTreeName.empty()) {
    addBehavior(newInstance, mModelInstCamData.micBehaviorData.at(newInstanceSettings.isNodeTreeName));
  }

  updateTriangleCount();
}

// keep scaling and axis flipping
void VkRenderer::cloneInstances(std::shared_ptr<AssimpInstance> instance, int numClones) {
  std::shared_ptr<AssimpModel> model = instance->getModel();
  std::vector<std::shared_ptr<AssimpInstance>> newInstances;

  std::uniform_int_distribution<int> randomPosInts(-125, 125);
  std::uniform_int_distribution<int> randomRotInts(-180, 180);

  for (int i = 0; i < numClones; ++i) {
    int xPos = randomPosInts(mRandomEngine);
    int zPos = randomPosInts(mRandomEngine);
    int rotation = randomRotInts(mRandomEngine);

    std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
    InstanceSettings instSettings = instance->getInstanceSettings();
    instSettings.isWorldPosition = glm::vec3(xPos, instSettings.isWorldPosition.y, zPos);
    instSettings.isWorldRotation = glm::vec3(0.0f, rotation, 0.0f);

    newInstance->setInstanceSettings(instSettings);

    newInstances.emplace_back(newInstance);
    mModelInstCamData.micAssimpInstances.emplace_back(newInstance);
    mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
  }

  assignInstanceIndices();

  // add behavior tree after new id was set
  for (int i = 0; i < numClones; ++i) {
    InstanceSettings newInstanceSettings = newInstances.at(i)->getInstanceSettings();
    if (!newInstanceSettings.isNodeTreeName.empty()) {
      addBehavior(newInstances.at(i), mModelInstCamData.micBehaviorData.at(newInstanceSettings.isNodeTreeName));
    }
  }

  int prevSelectedInstanceId = mModelInstCamData.micSelectedInstance;

  // select new instance
  mModelInstCamData.micSelectedInstance = mModelInstCamData.micAssimpInstances.size() - 1;
  mModelInstCamData.micSettingsContainer->applyNewMultiInstance(newInstances, mModelInstCamData.micSelectedInstance, prevSelectedInstanceId);

  // deselect light
  mModelInstCamData.micSelectedDynLight = 0;

  updateTriangleCount();
}

void VkRenderer::centerInstance(std::shared_ptr<AssimpInstance> instance) {
  InstanceSettings instSettings = instance->getInstanceSettings();
  mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera)->moveCameraTo(instSettings.isWorldPosition + glm::vec3(5.0f));
}

std::vector<glm::vec3> VkRenderer::getPositionOfAllInstances() {
  std::vector<glm::vec3> positions;

  // skip null instance
  for (int i = 1; i < mModelInstCamData.micAssimpInstances.size(); ++i) {
    glm::vec3 modelPos = mModelInstCamData.micAssimpInstances.at(i)->getWorldPosition();
    positions.emplace_back(modelPos);
  }

  return positions;
}

void VkRenderer::editGraph(std::string graphName) {
  if (mModelInstCamData.micBehaviorData.count(graphName) > 0) {
    mGraphEditor->loadData(mModelInstCamData.micBehaviorData.at(graphName)->getBehaviorData());
  } else {
    Logger::log(1, "%s error: graph '%s' not found\n", __FUNCTION__, graphName.c_str());
  }
}

std::shared_ptr<SingleInstanceBehavior> VkRenderer::createEmptyGraph() {
  mGraphEditor->createEmptyGraph();
  return mGraphEditor->getData();
}

void VkRenderer::initOctree(int thresholdPerBox, int maxDepth) {
  mOctree = std::make_shared<Octree>(mWorldBoundaries, thresholdPerBox, maxDepth);

  // octree needs to get bounding box of the instances
  mOctree->mInstanceGetBoundingBoxCallbackFunction = [this](int instanceId) {
    return mModelInstCamData.micAssimpInstances.at(instanceId)->getBoundingBox();
  };
}

std::shared_ptr<BoundingBox3D> VkRenderer::getWorldBoundaries() {
  return mWorldBoundaries;
}

void VkRenderer::initTriangleOctree(int thresholdPerBox, int maxDepth) {
  mTriangleOctree = std::make_shared<TriangleOctree>(mWorldBoundaries, thresholdPerBox, maxDepth);
}

void VkRenderer::addBehavior(std::shared_ptr<AssimpInstance> instance, std::shared_ptr<SingleInstanceBehavior> behavior) {
  mRenderData.rdBehviorTimer.start();
  mBehaviorManager->addInstance(instance, behavior);
  mRenderData.rdBehaviorTime += mRenderData.rdBehviorTimer.stop();
  Logger::log(1, "%s: added behavior %s to instance %i\n", __FUNCTION__, behavior->getBehaviorData()->bdName.c_str(), instance->getInstanceIndexPosition());
}

void VkRenderer::delBehavior(std::shared_ptr<AssimpInstance> instance) {
  mRenderData.rdBehviorTimer.start();
  mBehaviorManager->removeInstance(instance);
  mRenderData.rdBehaviorTime += mRenderData.rdBehviorTimer.stop();

  Logger::log(1, "%s: removed behavior from instance %i\n", __FUNCTION__, instance->getInstanceIndexPosition());
}

void VkRenderer::addModelBehavior(std::string modelName, std::shared_ptr<SingleInstanceBehavior> behavior) {
  std::shared_ptr<AssimpModel> model = getModel(modelName);
  if (!model) {
    Logger::log(1, "%s error: model %s not found\n", __FUNCTION__, modelName.c_str());
    return;
  }

  for (auto& instance : mModelInstCamData.micAssimpInstancesPerModel[modelName]) {
    InstanceSettings settings = instance->getInstanceSettings();
    mBehaviorManager->addInstance(instance, behavior);
    settings.isNodeTreeName = behavior->getBehaviorData()->bdName;
    instance->setInstanceSettings(settings);
  }

  Logger::log(1, "%s: added behavior %s to all instances of model %s\n", __FUNCTION__, behavior->getBehaviorData()->bdName.c_str(), modelName.c_str());
}

void VkRenderer::delModelBehavior(std::string modelName) {
  std::shared_ptr<AssimpModel> model = getModel(modelName);
  if (!model) {
    Logger::log(1, "%s error: model %s not found\n", __FUNCTION__, modelName.c_str());
    return;
  }

  for (auto& instance : mModelInstCamData.micAssimpInstancesPerModel[modelName]) {
    InstanceSettings settings = instance->getInstanceSettings();
    mBehaviorManager->removeInstance(instance);
    settings.isNodeTreeName.clear();
    instance->setInstanceSettings(settings);

    // works here because we don't edit instances
    instance->stopInstance();
  }

  Logger::log(1, "%s: removed behavior from all instances of model %s\n", __FUNCTION__, modelName.c_str());
}

void VkRenderer::updateInstanceSettings(std::shared_ptr<AssimpInstance> instance, graphNodeType nodeType,
    instanceUpdateType updateType, nodeCallbackVariant data, bool extraSetting) {
  InstanceSettings settings = instance->getInstanceSettings();
  moveDirection dir = settings.isMoveDirection;
  moveState state = settings.isMoveState;

  switch (nodeType) {
    case graphNodeType::instanceMovement:
      switch (updateType) {
        case instanceUpdateType::moveDirection:
          dir = std::get<moveDirection>(data);
          instance->updateInstanceState(state, dir);
          break;
        case instanceUpdateType::moveState:
          state = std::get<moveState>(data);
          instance->updateInstanceState(state, dir);
          break;
        case instanceUpdateType::speed:
          instance->setForwardSpeed(std::get<float>(data));
          break;
        case instanceUpdateType::rotation:
          // true if relative rotation
          if (extraSetting) {
            instance->rotateInstance(std::get<float>(data));
          } else {
            glm::vec3 currentRotation = instance->getRotation();
            instance->setRotation(glm::vec3(currentRotation.x, std::get<float>(data), currentRotation.z));
          }
          break;
        case instanceUpdateType::position:
          instance->setWorldPosition(std::get<glm::vec3>(data));
          break;
        default:
          // do nothing
          break;
      }
      break;
    case graphNodeType::action:
      if (updateType == instanceUpdateType::moveState) {
        state = std::get<moveState>(data);
        instance->setNextInstanceState(state);
      }
      break;
    case graphNodeType::faceAnim:
      switch (updateType) {
        case instanceUpdateType::faceAnimIndex:
          instance->setFaceAnim(std::get<faceAnimation>(data));
          break;
        case instanceUpdateType::faceAnimWeight:
          instance->setFaceAnimWeight(std::get<float>(data));
          break;
        default:
          // do nothing
          break;
      }
      break;
    case graphNodeType::headAmin:
      switch (updateType) {
        case instanceUpdateType::headAnim:
          instance->setHeadAnim(std::get<glm::vec2>(data));
          break;
        default:
          // do nothing
          break;
      }
      break;
    case graphNodeType::randomNavigation:
      {
        std::vector<int> allNavTargets = getNavTargets();

        // use a random target as an example
        if (!allNavTargets.empty() && settings.isPathTargetInstance == -1) {
          std::shuffle(allNavTargets.begin(), allNavTargets.end(), mRandomEngine);
          instance->setPathTargetInstanceId(allNavTargets.at(0));
          instance->setNavigationEnabled(true);
        }
      }
      break;
    default:
      // do nothing
      break;
  }
}

void VkRenderer::addBehaviorEvent(std::shared_ptr<AssimpInstance> instance, nodeEvent event) {
  InstanceSettings instSettings = instance->getInstanceSettings();
  // add event only if instance has a node tree template to react
  if (!instSettings.isNodeTreeName.empty()) {
    mBehaviorManager->addEvent(instance, event);
  }
}

void VkRenderer::postDelNodeTree(std::string nodeTreeName) {
  for (auto& instance : mModelInstCamData.micAssimpInstances) {
    InstanceSettings settings = instance->getInstanceSettings();
    if (settings.isNodeTreeName == nodeTreeName) {
      mBehaviorManager->removeInstance(instance);
      settings.isNodeTreeName.clear();
    }
    instance->setInstanceSettings(settings);

    instance->stopInstance();
  }

  if (mGraphEditor->getCurrentEditedTreeName() == nodeTreeName) {
    mGraphEditor->closeEditor();
  }
}

bool VkRenderer::hasLevel(std::string levelFileName) {
  auto levelIter =  std::find_if(mModelInstCamData.micLevels.begin(), mModelInstCamData.micLevels.end(),
    [levelFileName](const auto& level) {
      return level->getLevelFileNamePath() == levelFileName || level->getLevelFileName() == levelFileName;
    });
  return levelIter != mModelInstCamData.micLevels.end();
}

std::shared_ptr<AssimpLevel> VkRenderer::getLevel(std::string levelFileName) {
  auto levelIter =  std::find_if(mModelInstCamData.micLevels.begin(), mModelInstCamData.micLevels.end(),
    [levelFileName](const auto& level) {
      return level->getLevelFileNamePath() == levelFileName || level->getLevelFileName() == levelFileName;
    });
  if (levelIter != mModelInstCamData.micLevels.end()) {
    return *levelIter;
  }
  return nullptr;
}

bool VkRenderer::addLevel(std::string levelFileName, bool updateVertexData) {
  if (hasLevel(levelFileName)) {
    Logger::log(1, "%s warning: level '%s' already existed, skipping\n", __FUNCTION__, levelFileName.c_str());
    return false;
  }

  std::shared_ptr<AssimpLevel> level = std::make_shared<AssimpLevel>();
  if (!level->loadLevel(mRenderData, levelFileName)) {
    Logger::log(1, "%s error: could not load level file '%s'\n", __FUNCTION__, levelFileName.c_str());
    return false;
  }

  mModelInstCamData.micLevels.emplace_back(level);

  // select new level
  mModelInstCamData.micSelectedLevel = mModelInstCamData.micLevels.size() - 1;

  // update vertex data
  if (updateVertexData) {
    generateLevelVertexData();
  }

  return true;

}

void VkRenderer::deleteLevel(std::string levelFileName) {
  std::shared_ptr<AssimpLevel> level = getLevel(levelFileName);

  // save level in separate pending deletion list before purging from model list
  if (level && (level->getTriangleCount() > 0)) {
    Logger::log(1, "%s: -- adding level %s to pending\n", __FUNCTION__, level->getLevelFileName().c_str());
    mModelInstCamData.micPendingDeleteAssimpLevels.insert(level);
  }

  mModelInstCamData.micLevels.erase(
    std::remove_if(
      mModelInstCamData.micLevels.begin(),
      mModelInstCamData.micLevels.end(),
      [levelFileName](std::shared_ptr<AssimpLevel> level) {
        return level->getLevelFileName() == levelFileName;
      }
    )
  );

  // decrement selected model index to point to model that is in list before the deleted one
  if (mModelInstCamData.micSelectedLevel > 1) {
    mModelInstCamData.micSelectedLevel -= 1;
  }

  // reload default level configuration if only default level is left
  if (mModelInstCamData.micLevels.size() == 1) {
    resetLevelData();
  }

  generateLevelVertexData();
  updateLevelTriangleCount();
}

std::shared_ptr<AssimpDynLight> VkRenderer::addDynLight() {
  std::shared_ptr<AssimpDynLight> newLight = std::make_shared<AssimpDynLight>(mLightModel);
  mModelInstCamData.micDynLights.emplace_back(newLight);

  assignLightIndices();
  generateShaderLightData();

  // select new instance
  mModelInstCamData.micSelectedDynLight = mModelInstCamData.micDynLights.size() - 1;

  // deselect instance
  mModelInstCamData.micSelectedInstance = 0;

  return newLight;
}

void VkRenderer::deleteDynLight(std::shared_ptr<AssimpDynLight> light) {
  mModelInstCamData.micDynLights.erase(
    std::remove_if(
      mModelInstCamData.micDynLights.begin(),
      mModelInstCamData.micDynLights.end(),
      [light](std::shared_ptr<AssimpDynLight> l) { return l == light; }),
      mModelInstCamData.micDynLights.end()
  );

  if (mModelInstCamData.micSelectedDynLight > 0) {
    mModelInstCamData.micSelectedDynLight -= 1;
    // deselect instance
    mModelInstCamData.micSelectedInstance = 0;
  }

  assignLightIndices();
  generateShaderLightData();
}

void VkRenderer::cloneDynLight(std::shared_ptr<AssimpDynLight> light) {
  std::shared_ptr<AssimpDynLight> newLight = std::make_shared<AssimpDynLight>(mLightModel);
  DynamicLightSettings newLightSettings = light->getDynLightSettings();

  // slight offset to see new light
  newLightSettings.dlsWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
  newLight->setDynLightSettings(newLightSettings);

  mModelInstCamData.micDynLights.emplace_back(newLight);

  // select new light
  mModelInstCamData.micSelectedDynLight = mModelInstCamData.micDynLights.size() - 1;

  // deselect instance
  mModelInstCamData.micSelectedInstance = 0;

  assignLightIndices();
  generateShaderLightData();
}

void VkRenderer::centerDynLight(std::shared_ptr<AssimpDynLight> light) {
  DynamicLightSettings lightSettings = light->getDynLightSettings();
  mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera)->moveCameraTo(lightSettings.dlsWorldPosition + glm::vec3(5.0f, 0.0f, 5.0f));
}

void VkRenderer::generateLevelVertexData() {
  generateLevelAABB();
  generateLevelOctree();
  generateLevelWireframe();
  generateGroundTriangleData();

  updateLevelTriangleCount();
}

void VkRenderer::generateGroundTriangleData() {
  mPathFinder.generateGroundTriangles(mRenderData, mTriangleOctree, *getWorldBoundaries());

  std::shared_ptr<VkSimpleMesh> groundMesh = mPathFinder.getGroundLevelMesh();
  mGroundMeshVertexCount = groundMesh->vertices.size();

  mRenderData.rdUploadToVBOTimer.start();
  for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
    VertexBuffer::uploadData(mRenderData, mRenderData.rdGroundMeshVertexBuffers.at(i), groundMesh->vertices);
  }
  mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
}

void VkRenderer::generateLevelAABB() {
  if (mModelInstCamData.micLevels.size() == 1) {
    return;
  }

  mAllLevelAABB.clear();

  for (const auto& level : mModelInstCamData.micLevels) {
    if (level->getTriangleCount() == 0) {
      continue;
    }

    level->generateAABB();
    mAllLevelAABB.addPoint(level->getAABB().getMinPos());
    mAllLevelAABB.addPoint(level->getAABB().getMaxPos());
  }

  // update Octree too
  mWorldBoundaries = std::make_shared<BoundingBox3D>(mAllLevelAABB.getMinPos(), mAllLevelAABB.getMaxPos() - mAllLevelAABB.getMinPos());
  mRenderData.rdWorldStartPos = mWorldBoundaries->getFrontTopLeft();
  mRenderData.rdWorldSize = mWorldBoundaries->getSize();
  initOctree(mRenderData.rdOctreeThreshold, mRenderData.rdOctreeMaxDepth);
  initTriangleOctree(mRenderData.rdLevelOctreeThreshold, mRenderData.rdLevelOctreeMaxDepth);

  glm::vec4 levelAABBColor = glm::vec4(0.0f, 1.0f, 0.5, 1.0f);
  mLevelAABBMesh = mAllLevelAABB.getAABBLines(levelAABBColor);

  if (!mLevelAABBMesh->vertices.empty()) {
    mRenderData.rdUploadToVBOTimer.start();
    for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
      VertexBuffer::uploadData(mRenderData, mRenderData.rdLevelAABBVertexBuffers.at(i), mLevelAABBMesh->vertices);
    }
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }
}

void VkRenderer::generateLevelOctree() {
  mTriangleOctree->clear();

  int index = 0;
  for (const auto& level : mModelInstCamData.micLevels) {
    if (level->getTriangleCount() == 0) {
      continue;
    }
    Logger::log(1, "%s: generating octree data for level '%s'\n", __FUNCTION__, level->getLevelFileName().c_str());
    std::vector<VkMesh> levelMeshes = level->getLevelMeshes();
    glm::mat4 transformMat = level->getWorldTransformMatrix();
    glm::mat3 normalMat = level->getNormalTransformMatrix();

    for (const auto& mesh : levelMeshes) {
      for (int i = 0; i < mesh.indices.size(); i += 3) {
        MeshTriangle tri{};
        // fix w component of position
        tri.points.at(0) = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i)).position), 1.0f);
        tri.points.at(1) = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i + 1)).position), 1.0f);
        tri.points.at(2) = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i + 2)).position), 1.0f);

        // precalculate edges
        tri.edges.at(0) = tri.points.at(1) - tri.points.at(0);
        tri.edges.at(1) = tri.points.at(2) - tri.points.at(1);
        tri.edges.at(2) = tri.points.at(0) - tri.points.at(2);

        tri.edgeLengths.at(0) = glm::length(tri.edges.at(0));
        tri.edgeLengths.at(1) = glm::length(tri.edges.at(1));
        tri.edgeLengths.at(2) = glm::length(tri.edges.at(2));

        AABB triangleAABB;
        triangleAABB.clear();
        triangleAABB.addPoint(tri.points.at(0));
        triangleAABB.addPoint(tri.points.at(1));
        triangleAABB.addPoint(tri.points.at(2));

        // add a (very) small offset to the size since complete planar triangles may be ignored
        tri.boundingBox = BoundingBox3D(triangleAABB.getMinPos() - glm::vec3(0.0001f),
                                        triangleAABB.getMaxPos() - triangleAABB.getMinPos() + glm::vec3(0.0002f));

        tri.normal = glm::normalize(normalMat * glm::vec3(mesh.vertices.at(mesh.indices.at(i)).normal));

        tri.index = index++;
        mTriangleOctree->add(tri);
      }
    }
  }

  mLevelOctreeMesh->vertices.clear();

  glm::vec4 octreeColor = glm::vec4(1.0f, 1.0f, 1.0, 1.0f);
  const std::vector<BoundingBox3D> treeBoxes = mTriangleOctree->getTreeBoxes();
  for (const auto& box : treeBoxes) {
    AABB boxAABB{};
    boxAABB.create(box.getFrontTopLeft());
    boxAABB.addPoint(box.getFrontTopLeft() + box.getSize());

    std::shared_ptr<VkSimpleMesh> instanceLines = boxAABB.getAABBLines(octreeColor);
    mLevelOctreeMesh->vertices.insert(mLevelOctreeMesh->vertices.end(), instanceLines->vertices.begin(), instanceLines->vertices.end());
  }

  if (!mLevelOctreeMesh->vertices.empty()) {
    mRenderData.rdUploadToVBOTimer.start();
    for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
      VertexBuffer::uploadData(mRenderData, mRenderData.rdLevelOctreeVertexBuffers.at(i), mLevelOctreeMesh->vertices);
    }
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }
}

void VkRenderer::generateLevelWireframe() {
  mLevelWireframeMesh->vertices.clear();
  mRenderData.rdLevelWireframeMiniMapMesh->vertices.clear();

  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;

  const float wireframeOffset = 0.005f;

  for (const auto& level : mModelInstCamData.micLevels) {
    if (level->getTriangleCount() == 0) {
      continue;
    }
    Logger::log(1, "%s: generating wireframe data for level '%s'\n", __FUNCTION__, level->getLevelFileName().c_str());
    std::vector<VkMesh> levelMeshes = level->getLevelMeshes();
    glm::mat4 transformMat = level->getWorldTransformMatrix();
    glm::mat3 normalMat = level->getNormalTransformMatrix();

    for (const auto& mesh : levelMeshes) {
      VkSimpleVertex vert;
      VkSimpleVertex normalVert;

      // generate different colors per mesh
      r = std::fmod(r + 0.66f, 1.0f);
      g = std::fmod(g + 0.81f, 1.0f);
      b = std::fmod(b + 0.75f, 1.0f);
      vert.color = glm::vec3(r, g, b);
      normalVert.color = glm::vec3(0.6, 0.0f, 0.6f);

      for (int i = 0; i < mesh.indices.size(); i += 3) {
        // move wireframe overdraw a bit above the planes
        glm::vec3 point0 = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i)).position), 1.0f);
        glm::vec3 point1 = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i + 1)).position), 1.0f);
        glm::vec3 point2 = transformMat * glm::vec4(glm::vec3(mesh.vertices.at(mesh.indices.at(i + 2)).position), 1.0f);

        glm::vec3 normal0 = glm::normalize(normalMat * glm::vec3(mesh.vertices.at(mesh.indices.at(i)).normal));
        glm::vec3 normal1 = glm::normalize(normalMat * glm::vec3(mesh.vertices.at(mesh.indices.at(i + 1)).normal));
        glm::vec3 normal2 = glm::normalize(normalMat * glm::vec3(mesh.vertices.at(mesh.indices.at(i + 2)).normal));

        // move vertices in direction of normal 
        vert.position = point0 + normal0 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);
        vert.position = point1 + normal1 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);

        vert.position = point1 + normal1 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);
        vert.position = point2 + normal2 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);

        vert.position = point2 + normal2 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);
        vert.position = point0 + normal0 * wireframeOffset;
        mLevelWireframeMesh->vertices.emplace_back(vert);
        mRenderData.rdLevelWireframeMiniMapMesh->vertices.emplace_back(vert);

        // draw normal vector in the middle of the triangle
        glm::vec3 normalPos = (point0 + point1 + point2) / 3.0f;
        normalVert.position = normalPos;
        mLevelWireframeMesh->vertices.emplace_back(normalVert);
        normalVert.position = normalPos + normal0;
        mLevelWireframeMesh->vertices.emplace_back(normalVert);
      }
    }
  }

  if (!mLevelWireframeMesh->vertices.empty()) {
    mRenderData.rdUploadToVBOTimer.start();
    for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
      VertexBuffer::uploadData(mRenderData, mRenderData.rdLevelWireframeVertexBuffers.at(i), mLevelWireframeMesh->vertices);
    }
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }

  // adjust color for mini map
  std::transform(mRenderData.rdLevelWireframeMiniMapMesh->vertices.begin(),
    mRenderData.rdLevelWireframeMiniMapMesh->vertices.end(),
    mRenderData.rdLevelWireframeMiniMapMesh->vertices.begin(),
    [](VkSimpleVertex& v) {
      v.color = glm::vec3(0.0f, 1.0f, 1.0f);
      return v;
    }
  );
}

std::vector<int> VkRenderer::getNavTargets() {
  std::vector<int> targets;
  for (const auto& model : mModelInstCamData.micModelList) {
    if (!model->isNavigationTarget()) {
      continue;
    }
    std::string modelName = model->getModelFileName();
    for (auto& instance : mModelInstCamData.micAssimpInstancesPerModel[modelName]) {
      InstanceSettings settings = instance->getInstanceSettings();
      targets.emplace_back(settings.isInstanceIndexPosition);
    }
  }

  return targets;
}

void VkRenderer::updateTriangleCount() {
  mRenderData.rdTriangleCount = 0;
  for (const auto& instance : mModelInstCamData.micAssimpInstances) {
    mRenderData.rdTriangleCount += instance->getModel()->getTriangleCount();
  }
}

void VkRenderer::updateLevelTriangleCount() {
  mRenderData.rdLevelTriangleCount = 0;
  for (const auto& level : mModelInstCamData.micLevels) {
    mRenderData.rdLevelTriangleCount += level->getTriangleCount();
  }
}

void VkRenderer::assignInstanceIndices() {
  for (size_t i = 0; i < mModelInstCamData.micAssimpInstances.size(); ++i) {
    InstanceSettings instSettings = mModelInstCamData.micAssimpInstances.at(i)->getInstanceSettings();
    instSettings.isInstanceIndexPosition = i;
    mModelInstCamData.micAssimpInstances.at(i)->setInstanceSettings(instSettings);
  }

  for (const auto& modelType : mModelInstCamData.micAssimpInstancesPerModel) {
    for (size_t i = 0; i < modelType.second.size(); ++i) {
      InstanceSettings instSettings = modelType.second.at(i)->getInstanceSettings();
      instSettings.isInstancePerModelIndexPosition = i;
      modelType.second.at(i)->setInstanceSettings(instSettings);
    }
  }

  // update also when number of instances has changed
  mOctree->clear();
  // skip null instance
  for (size_t i = 1; i < mModelInstCamData.micAssimpInstances.size(); ++i) {
    mOctree->add(mModelInstCamData.micAssimpInstances.at(i)->getInstanceIndexPosition());
  }
}

void VkRenderer::assignLightIndices() {
  for (size_t i = 0; i < mModelInstCamData.micDynLights.size(); ++i) {
    DynamicLightSettings lightSettings = mModelInstCamData.micDynLights.at(i)->getDynLightSettings();
    lightSettings.dlsIndexPosition = i;
    mModelInstCamData.micDynLights.at(i)->setDynLightSettings(lightSettings);
  }

}

void VkRenderer::generateShaderLightData() {
  mRenderData.rdLightData.clear();
  mRenderData.rdLightData.resize(mModelInstCamData.micDynLights.size());
  mRenderData.rdLightDebugData.clear();
  mRenderData.rdLightDebugData.resize(mModelInstCamData.micDynLights.size());

  int lightsWithShadows = 0;
  for (size_t i = 1; i < mRenderData.rdLightData.size(); ++i) {
    if (mModelInstCamData.micDynLights.at(i)->getShadowEnabled()) {
      ++lightsWithShadows;
    }
  }

  mRenderData.rdNumDynamicLights = mModelInstCamData.micDynLights.size() - 1;
  mRenderData.rdNumDynamicLightsWithShadow = lightsWithShadows;

  // put lights with shadows in front, null light still first
  mRenderData.rdLightIndices.clear();
  mRenderData.rdLightIndices.push_back(0);

  std::vector<int> shadowLights{};
  std::vector<int> nonShadowLights{};
  for (int i = 1; i < mModelInstCamData.micDynLights.size(); ++i) {
    int index = mModelInstCamData.micDynLights.at(i)->getDynLightIndexPosition();
    if (mModelInstCamData.micDynLights.at(i)->getShadowEnabled()) {
      shadowLights.push_back(index);
    }
    else {
      nonShadowLights.push_back(index);
    }
  }

  mRenderData.rdLightIndices.insert(mRenderData.rdLightIndices.end(), shadowLights.begin(), shadowLights.end());
  mRenderData.rdLightIndices.insert(mRenderData.rdLightIndices.end(), nonShadowLights.begin(), nonShadowLights.end());

  for (auto i : mRenderData.rdLightIndices) {
    Logger::log(1, "%s: light index is %i\n", __FUNCTION__, i);
  }

  Logger::log(1, "%s: update dynamic light shadow data\n", __FUNCTION__);
  if (lightsWithShadows > 0) {
    mRenderData.rdDynamicLightShadowMapData.cascades.resize(mRenderData.rdNumDynamicLightsWithShadow * 6);

    size_t shadowMapCascadeSize = sizeof(ShadowMapCascades) * std::max(static_cast<size_t>(mRenderData.rdNumDynamicLightsWithShadow * 6), static_cast<size_t>(4));
    bool bufferResized = ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShadowMapCascadeDataBuffers.at(mRenderData.currentFrame), shadowMapCascadeSize);

    if (bufferResized) {
      for (int i = 0; i < mRenderData.rdNumFramesInFlight; ++i) {
        // resize all SSBOs
        ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShadowMapCascadeDataBuffers.at(i), shadowMapCascadeSize);
      }

      VkHelper::updateDescriptorSets(mRenderData);
      VkHelper::updateImageDescriptorSets(mRenderData);
      VkHelper::updateLevelDescriptorSets(mRenderData);
    }

    VkHelper::cleanupDepthBufferCubeMap(mRenderData, mRenderData.rdDynamicLightShadowData);
    if (!VkHelper::createDepthBufferCubeMap(mRenderData, mRenderData.rdDynamicLightShadowData, mRenderData.rdNumDynamicLightsWithShadow)) {
      Logger::log(1, "%s error: could not create dynamic light shadow map\n", __FUNCTION__);
    }

    VkHelper::updateImageDescriptorSets(mRenderData);
  }

  updateShaderLightData();
}

void VkRenderer::updateShaderLightData() {
  if (mRenderData.rdNumDynamicLights == 0) {
    return;
  }

  // Skip null light, light data can be default values
  for (unsigned int i = 1; i < mRenderData.rdNumDynamicLights + 1; ++i) {
    int lightIndex = mRenderData.rdLightIndices.at(i);

    float lightDistance = mModelInstCamData.micDynLights.at(lightIndex)->getLightingDistance();
    float maxLightDistance = mModelInstCamData.micDynLights.at(lightIndex)->getMaxLightingDistance();

    float constantFactor = 1.0f;
    float linearFactor = 1.0f / (lightDistance / 45.0f);
    float quadraticFactor = 1.0f / ((lightDistance * lightDistance) / 750.0f);

    glm::vec3 lightColor = mModelInstCamData.micDynLights.at(lightIndex)->getLightColor();
    float maxLightColor = glm::max(glm::max(lightColor.r, lightColor.g), lightColor.b);

    float lightSphereRadius = lightDistance * maxLightColor;

    // debug sphere radius is min of attenuation light and max light distance
    mRenderData.rdLightDebugData.at(i) = glm::vec4(mModelInstCamData.micDynLights.at(lightIndex)->getWorldPosition(), glm::min(lightSphereRadius, maxLightDistance));

    mRenderData.rdLightData.at(i).type = static_cast<uint32_t>(mModelInstCamData.micDynLights.at(lightIndex)->getLightType());
    mRenderData.rdLightData.at(i).position = glm::vec4(mModelInstCamData.micDynLights.at(lightIndex)->getWorldPosition(), 1.0f);
    mRenderData.rdLightData.at(i).rotation = glm::vec4(mModelInstCamData.micDynLights.at(lightIndex)->getRotationRadians(), 1.0f);
    mRenderData.rdLightData.at(i).color = glm::vec4(lightColor, 1.0f);
    if (mModelInstCamData.micDynLights.at(lightIndex)->getLightEnabled()) {
      mRenderData.rdLightData.at(i).distance = lightDistance;
      mRenderData.rdLightData.at(i).maxDistance = maxLightDistance;
    } else {
      mRenderData.rdLightData.at(i).distance = 0.0f;
      mRenderData.rdLightData.at(i).maxDistance = 0.0f;
    }
    mRenderData.rdLightData.at(i).cutoff = glm::cos(glm::radians(mModelInstCamData.micDynLights.at(lightIndex)->getPointLightCutOffAngle()));
    mRenderData.rdLightData.at(i).outerCutoff = glm::cos(glm::radians(mModelInstCamData.micDynLights.at(lightIndex)->getPointLightOuterCutOffAngle()));
    mRenderData.rdLightData.at(i).constantAttFactor = constantFactor;
    mRenderData.rdLightData.at(i).linearAttFactor = linearFactor;
    mRenderData.rdLightData.at(i).quadraticAttFactor = quadraticFactor;

    mRenderData.rdLightData.at(i).shadowMapOffset = mModelInstCamData.micDynLights.at(lightIndex)->getShadowMapOffset();
  }

  if (mRenderData.rdNumDynamicLightsWithShadow > 0) {
    glm::mat4 dynLightProjectionMat = glm::perspective(
      glm::radians(90.0f), 1.0f, mRenderData.rdNearPlane, mRenderData.rdFarPlane);

    // draw upside down by manipulating the up vector
    glm::vec3 worldUpVector = glm::vec3(0.0f, -1.0f, 0.0f);

    glm::vec3 negYUpVector = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 posYUpVector = glm::vec3(0.0f, 0.0f, 1.0f);

    for (int i = 0; i < mRenderData.rdNumDynamicLightsWithShadow; ++i ) {
      // null light is at pos 0, skip
      glm::vec3 lightPos = mRenderData.rdLightData.at(i + 1).position;

      glm::mat4 posXMat = glm::lookAt(lightPos, lightPos + glm::vec3(1.0f, 0.0f, 0.0f), worldUpVector);
      glm::mat4 negXMat = glm::lookAt(lightPos, lightPos + glm::vec3(-1.0f, 0.0f, 0.0f), worldUpVector);

      glm::mat4 posYMat = glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 1.0f, 0.0f), posYUpVector);
      glm::mat4 negYMat = glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, -1.0f, 0.0f), negYUpVector);

      glm::mat4 posZMat = glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, 1.0f), worldUpVector);
      glm::mat4 negZMat = glm::lookAt(lightPos, lightPos + glm::vec3(0.0f, 0.0f, -1.0f), worldUpVector);

      int cubeArrayOffset = i * 6;
      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset).lightViewProjectionMat = dynLightProjectionMat * posXMat;
      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset + 1).lightViewProjectionMat = dynLightProjectionMat * negXMat;

      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset + 2).lightViewProjectionMat = dynLightProjectionMat * posYMat;
      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset + 3).lightViewProjectionMat = dynLightProjectionMat * negYMat;

      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset + 4).lightViewProjectionMat = dynLightProjectionMat * posZMat;
      mRenderData.rdDynamicLightShadowMapData.cascades.at(cubeArrayOffset + 5).lightViewProjectionMat = dynLightProjectionMat * negZMat;
    }
  }
}

void VkRenderer::cloneCamera() {
  std::shared_ptr<Camera> currentCam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  std::shared_ptr<Camera> newCam = std::make_shared<Camera>();

  CameraSettings settings = currentCam->getCameraSettings();
  settings.csCamName = generateUniqueCameraName(settings.csCamName);
  newCam->setCameraSettings(settings);

  mModelInstCamData.micCameras.emplace_back(newCam);
  mModelInstCamData.micSelectedCamera = mModelInstCamData.micCameras.size() - 1;
}

void VkRenderer::deleteCamera() {
  mModelInstCamData.micCameras.erase(mModelInstCamData.micCameras.begin() + mModelInstCamData.micSelectedCamera);
  mModelInstCamData.micSelectedCamera = mModelInstCamData.micCameras.size() - 1;
}

std::string VkRenderer::generateUniqueCameraName(std::string camBaseName) {
  std::string camName = camBaseName;
  std::string matches("01234567890");

  while (checkCameraNameUsed(camName)) {
    const auto iter = std::find_first_of(camName.begin(), camName.end(), matches.begin(), matches.end());
    if (iter == camName.end()) {
      camName.append("1");
    } else {
      std::string cameraNameString = camName.substr(0, std::distance(camName.begin(), iter));
      std::string cameraNumString = camName.substr(std::distance(camName.begin(), iter));
      int cameraNumber = std::stoi(cameraNumString);
      camName = cameraNameString + std::to_string(++cameraNumber);
    }
  }
  return camName;
}

bool VkRenderer::checkCameraNameUsed(std::string cameraName) {
  for (const auto& cam : mModelInstCamData.micCameras) {
    if (cam->getCameraSettings().csCamName == cameraName) {
      return true;
    }
  }

  return false;
}

void VkRenderer::setSize(unsigned int width, unsigned int height) {
  // handle minimize
  if (width == 0 || height == 0) {
    return;
  }

  // Vulkan detects changes and recreates swapchain on Windows and X11, but NOT on Wayland
  if (mRenderData.rdWaylandFound) {
    recreateSwapchain();
  }
  Logger::log(1, "%s: resized window to %ix%i\n", __FUNCTION__, width, height);

#if !defined(__ANDROID__)
  float xScale, yScale;
  glfwGetWindowContentScale(mRenderData.rdWindow, &xScale, &yScale);
  Logger::log(1, "%s: window scale is %.2f (x) / %.2f (y) \n", __FUNCTION__, xScale, yScale);
#endif
}

void VkRenderer::setXRSize(unsigned int width, unsigned int height) {
  mRenderData.rdXRWidth = width;
  mRenderData.rdXRHeight = height;
}

void VkRenderer::setConfigDirtyFlag(bool flag) {
  mConfigIsDirty = flag;
  if (mConfigIsDirty) {
    mWindowTitleDirtySign = "*";
  } else {
    mWindowTitleDirtySign = " ";
  }
  setModeInWindowTitle();
}

bool VkRenderer::getConfigDirtyFlag() {
  return mConfigIsDirty;
}

void VkRenderer::setModeInWindowTitle() {
  mModelInstCamCallbacks.micSetWindowTitleFunction(mOrigWindowTitle + " (" +
  mRenderData.rdAppModeMap.at(mRenderData.rdApplicationMode) + " Mode)" +
  mWindowTitleDirtySign);
}

void VkRenderer::setAppMode(appMode newMode) {
  mRenderData.rdApplicationMode = newMode;
  setModeInWindowTitle();
  checkMouseEnable();
}

void VkRenderer::toggleFullscreen() {
#if !defined(__ANDROID__)
  mRenderData.rdFullscreen = mRenderData.rdFullscreen ? false : true;

  static int xPos = 0;
  static int yPos = 0;
  static int width = mRenderData.rdWidth;
  static int height = mRenderData.rdHeight;
  if (mRenderData.rdFullscreen) {
    // save position and resolution
    glfwGetWindowPos(mRenderData.rdWindow, &xPos, &yPos);
    glfwGetWindowSize(mRenderData.rdWindow, &width, &height);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(mRenderData.rdWindow, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
  } else {
    glfwSetWindowMonitor(mRenderData.rdWindow, nullptr, xPos, yPos, width, height, 0);
  }
#endif
}

void VkRenderer::checkMouseEnable() {
#if !defined(__ANDROID__)
  if (mMouseLock || mMouseMove || mRenderData.rdApplicationMode != appMode::edit) {
    glfwSetInputMode(mRenderData.rdWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    // enable raw mode if possible
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(mRenderData.rdWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
  } else {
    glfwSetInputMode(mRenderData.rdWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
#endif
}

void VkRenderer::handleKeyEvents(int key, int scancode, int action, int mods) {
#if !defined(__ANDROID__)
  // forward to ImGui only when in edit mode
  if (mRenderData.rdApplicationMode == appMode::edit) {
    ImGuiIO& io = ImGui::GetIO();

    // hide from application if above ImGui window
    if (io.WantCaptureKeyboard || io.WantTextInput) {
      return;
    }
  }

  // toggle between edit and view mode by pressing F10
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_F10) == GLFW_PRESS) {
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
      setAppMode(--mRenderData.rdApplicationMode);
      } else {
      setAppMode(++mRenderData.rdApplicationMode);
    }
  }

  // use ESC to return to edit mode
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    setAppMode(appMode::edit);
  }

  // toggle between full-screen and window mode by pressing F11
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_F11) == GLFW_PRESS) {
    toggleFullscreen();
  }

  // use F1 to show help text
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_F1) == GLFW_PRESS) {
    mRenderData.rdShowControlsHelpRequest = true;
  }

  if (mRenderData.rdApplicationMode == appMode::edit) {
    // instance edit modes
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_1) == GLFW_PRESS) {
      instanceEditMode oldMode = mRenderData.rdInstanceEditMode;
      mRenderData.rdInstanceEditMode = instanceEditMode::move;
      mModelInstCamData.micSettingsContainer->applyChangeEditMode(mRenderData.rdInstanceEditMode, oldMode);
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_2) == GLFW_PRESS) {
      instanceEditMode oldMode = mRenderData.rdInstanceEditMode;
      mRenderData.rdInstanceEditMode = instanceEditMode::rotate;
      mModelInstCamData.micSettingsContainer->applyChangeEditMode(mRenderData.rdInstanceEditMode, oldMode);
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_3) == GLFW_PRESS) {
      instanceEditMode oldMode = mRenderData.rdInstanceEditMode;
      mRenderData.rdInstanceEditMode = instanceEditMode::scale;
      mModelInstCamData.micSettingsContainer->applyChangeEditMode(mRenderData.rdInstanceEditMode, oldMode);
    }

    // undo/redo only in edit mode
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_Z) == GLFW_PRESS &&
      (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
      undoLastOperation();
    }

    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_Y) == GLFW_PRESS &&
      (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
      redoLastOperation();
    }

    // new config/load/save keyboard shortcuts
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_N) == GLFW_PRESS &&
      (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
      mRenderData.rdNewConfigRequest = true;
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_L) == GLFW_PRESS &&
      (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
      mRenderData.rdLoadConfigRequest = true;
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_S) == GLFW_PRESS &&
      (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
      glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
      mRenderData.rdSaveConfigRequest = true;
    }
  }

  // exit via CTRL+Q, allow in edit and view mode
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_Q) == GLFW_PRESS &&
    (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
    glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)) {
    requestExitApplication();
  }

  // toggle moving instance on Y axis when SHIFT is pressed
  // hack to react to both shift keys - remember which one was pressed
  if (mMouseMove) {
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      mMouseMoveVerticalShiftKey = GLFW_KEY_LEFT_SHIFT;
      mMouseMoveVertical = true;
    }
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
      mMouseMoveVerticalShiftKey = GLFW_KEY_RIGHT_SHIFT;
      mMouseMoveVertical = true;
    }
  }
  if  (glfwGetKey(mRenderData.rdWindow, mMouseMoveVerticalShiftKey) == GLFW_RELEASE) {
    mMouseMoveVerticalShiftKey = 0;
    mMouseMoveVertical = false;
  }

  // switch cameras forward and backwards with square brackets, active in edit AND view mode
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS) {
    if (mModelInstCamData.micSelectedCamera > 0) {
      mModelInstCamData.micSelectedCamera--;
    }
  }
  if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS) {
    if (mModelInstCamData.micSelectedCamera < mModelInstCamData.micCameras.size() - 1) {
      mModelInstCamData.micSelectedCamera++;
    }
  }

  checkMouseEnable();
#endif
}

void VkRenderer::handleMouseButtonEvents(int button, int action, int mods) {
#if !defined(__ANDROID__)
  // forward to ImGui only when in edit mode
  if (mRenderData.rdApplicationMode == appMode::edit) {
    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT) {
      io.AddMouseButtonEvent(button, action == GLFW_PRESS);
    }

    // hide from application if above ImGui window
    if (io.WantCaptureMouse || io.WantTextInput) {
      return;
    }
  }

  // trigger selection when left button has been released
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE
    && mRenderData.rdApplicationMode == appMode::edit) {
    mMousePick = true;
    mSavedSelectedInstanceId = mModelInstCamData.micSelectedInstance;
  }

  // move instance around with middle button pressed
  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS
    && mRenderData.rdApplicationMode == appMode::edit) {
    mMouseMove = true;
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      mMouseMoveVerticalShiftKey = GLFW_KEY_LEFT_SHIFT;
      mMouseMoveVertical = true;
    }
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
      mMouseMoveVerticalShiftKey = GLFW_KEY_RIGHT_SHIFT;
      mMouseMoveVertical = true;
    }

    if (mModelInstCamData.micSelectedInstance > 0) {
      mSavedInstanceSettings = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance)->getInstanceSettings();
    }
  }

  if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE
    && mRenderData.rdApplicationMode == appMode::edit) {
    mMouseMove = false;
    if (mModelInstCamData.micSelectedInstance > 0) {
      std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
      InstanceSettings settings = instance->getInstanceSettings();
      mModelInstCamData.micSettingsContainer->applyEditInstanceSettings(instance, settings, mSavedInstanceSettings);
      setConfigDirtyFlag(true);
    }
  }

  std::shared_ptr<Camera> camera = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = camera->getCameraSettings();

  // mouse camera movement only in edit mode, or with a free cam in view mode 
  if (mRenderData.rdApplicationMode == appMode::edit ||
    (mRenderData.rdApplicationMode == appMode::view && camSettings.csCamType == cameraType::free)) {
    // move camera view while right button is hold  
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
      mMouseLock = true;
      mSavedCameraSettings = camSettings;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
      mMouseLock = false;
      mModelInstCamData.micSettingsContainer->applyEditCameraSettings(camera, camSettings, mSavedCameraSettings);
      setConfigDirtyFlag(true);
    }
  }

  checkMouseEnable();
#endif
}

void VkRenderer::handleMousePositionEvents(double xPos, double yPos) {
#if !defined(__ANDROID__)
  // forward to ImGui only when in edit mode
  if (mRenderData.rdApplicationMode == appMode::edit) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent((float)xPos, (float)yPos);

    // hide from application if above ImGui window
    if (io.WantCaptureMouse || io.WantTextInput) {
      return;
    }
  }

  // calculate relative movement from last position
  int mouseMoveRelX = static_cast<int>(xPos) - mMouseXPos;
  int mouseMoveRelY = static_cast<int>(yPos) - mMouseYPos;

  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = cam->getCameraSettings();

  if (mMouseLock) {
    camSettings.csViewAzimuth += mouseMoveRelX / 10.0f;
    // keep between 0 and 360 degree
    if (camSettings.csViewAzimuth < 0.0f) {
      camSettings.csViewAzimuth += 360.0f;
    }
    if (camSettings.csViewAzimuth >= 360.0f) {
      camSettings.csViewAzimuth -= 360.0f;
    }

    camSettings.csViewElevation -= mouseMoveRelY / 10.0f;
    // keep between -89 and +89 degree
    camSettings.csViewElevation =  std::clamp(camSettings.csViewElevation, -89.0f, 89.0f);
  }

  cam->setCameraSettings(camSettings);

  std::shared_ptr<AssimpInstance> currentInstance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
  // instance rotation with mouse
  if (mRenderData.rdApplicationMode != appMode::edit) {
    if (mModelInstCamData.micSelectedInstance > 0) {
      float mouseXScaled = mouseMoveRelX / 10.0f;

      // XXX: let user look up and down in first-person?
      currentInstance->rotateInstance(mouseXScaled);
    }
  }

  if (mMouseMove) {
    float mouseXScaled = mouseMoveRelX / 20.0f;
    float mouseYScaled = mouseMoveRelY / 20.0f;
    float sinAzimuth = std::sin(glm::radians(camSettings.csViewAzimuth));
    float cosAzimuth = std::cos(glm::radians(camSettings.csViewAzimuth));

    if (mModelInstCamData.micSelectedInstance > 0) {
      float modelDistance = glm::length(camSettings.csWorldPosition - currentInstance->getWorldPosition()) / 50.0f;

      // avoid breaking camera pos on model world position the logic in first-person camera
      if (camSettings.csCamType == cameraType::firstPerson) {
        modelDistance = 0.1f;
      }

      glm::vec3 instancePos = currentInstance->getWorldPosition();
      glm::vec3 instanceRot = currentInstance->getRotation();
      float instanceScale = currentInstance->getScale();

      if (mMouseMoveVertical) {
        switch(mRenderData.rdInstanceEditMode) {
          case instanceEditMode::move:
            instancePos.y -= mouseYScaled * modelDistance;
            currentInstance->setWorldPosition(instancePos);
            break;
          case instanceEditMode::rotate:
            instanceRot.y -= mouseXScaled * 5.0f;
            currentInstance->rotateInstance(instanceRot);
            break;
          case instanceEditMode::scale:
            // uniform scale, do nothing here 
            break;
        }
      } else {
        switch(mRenderData.rdInstanceEditMode) {
          case instanceEditMode::move:
            instancePos.x += mouseXScaled * modelDistance * cosAzimuth - mouseYScaled * modelDistance * sinAzimuth;
            instancePos.z += mouseXScaled * modelDistance * sinAzimuth + mouseYScaled * modelDistance * cosAzimuth;
            currentInstance->setWorldPosition(instancePos);
            break;
          case instanceEditMode::rotate:
            instanceRot.z -= (mouseXScaled * cosAzimuth - mouseYScaled * sinAzimuth) * 5.0f;
            instanceRot.x += (mouseXScaled * sinAzimuth + mouseYScaled * cosAzimuth) * 5.0f;
            currentInstance->rotateInstance(instanceRot);
            break;
          case instanceEditMode::scale:
            instanceScale -= mouseYScaled / 2.0f;
            instanceScale= std::max(0.001f, instanceScale);
            currentInstance->setScale(instanceScale);
            break;
        }
      }
    }

    if (mModelInstCamData.micSelectedDynLight > 0) {
      std::shared_ptr<AssimpDynLight> currentLight = mModelInstCamData.micDynLights.at(mModelInstCamData.micSelectedDynLight);

      float modelDistance = glm::length(camSettings.csWorldPosition - currentLight->getWorldPosition()) / 50.0f;

      glm::vec3 lightPos = currentLight->getWorldPosition();
      glm::vec3 lightRot = currentLight->getRotation();

      if (mMouseMoveVertical) {
        switch(mRenderData.rdInstanceEditMode) {
          case instanceEditMode::move:
            lightPos.y -= mouseYScaled * modelDistance;
            currentLight->setWorldPosition(lightPos);
            break;
          case instanceEditMode::rotate:
            lightRot.y -= mouseXScaled * 5.0f;
            currentLight->rotateLight(lightRot);
            break;
          case instanceEditMode::scale:
            // do nothing here
            break;
        }
      } else {
        switch(mRenderData.rdInstanceEditMode) {
          case instanceEditMode::move:
            lightPos.x += mouseXScaled * modelDistance * cosAzimuth - mouseYScaled * modelDistance * sinAzimuth;
            lightPos.z += mouseXScaled * modelDistance * sinAzimuth + mouseYScaled * modelDistance * cosAzimuth;
            currentLight->setWorldPosition(lightPos);
            break;
          case instanceEditMode::rotate:
            lightRot.z -= (mouseXScaled * cosAzimuth - mouseYScaled * sinAzimuth) * 5.0f;
            lightRot.x += (mouseXScaled * sinAzimuth + mouseYScaled * cosAzimuth) * 5.0f;
            currentLight->rotateLight(lightRot);
            break;
          case instanceEditMode::scale:
            currentLight->setLightingDistance(currentLight->getLightingDistance() - mouseYScaled);
            // do nothing here
            break;
        }
      }
    }
  }

  // save old values
  mMouseXPos = static_cast<int>(xPos);
  mMouseYPos = static_cast<int>(yPos);
#endif
}

void VkRenderer::handleMouseWheelEvents(double xOffset, double yOffset) {
#if !defined(__ANDROID__)
  // forward to ImGui only when in edit mode
  if (mRenderData.rdApplicationMode == appMode::edit) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent((float)xOffset, (float)yOffset);

    // hide from application if above ImGui window
    if (io.WantCaptureMouse || io.WantTextInput) {
      return;
    }
  }

  if (mRenderData.rdApplicationMode == appMode::edit) {
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
      mMouseWheelScaleShiftKey = GLFW_KEY_LEFT_SHIFT;
      mMouseWheelScale = 4.0f;
    }
    if  (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
      mMouseWheelScaleShiftKey = GLFW_KEY_RIGHT_SHIFT;
      mMouseWheelScale = 4.0f;
    }

    if  (glfwGetKey(mRenderData.rdWindow, mMouseWheelScaleShiftKey) == GLFW_RELEASE) {
      mMouseWheelScaleShiftKey = 0;
      mMouseWheelScale = 1.0f;
    }

    // save timestamp of last scroll activity to check of scroll inactivity
    mMouseWheelScrolling = true;
    mMouseWheelLastScrollTime = std::chrono::steady_clock::now();

    std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
    CameraSettings camSettings = cam->getCameraSettings();
    mSavedCameraWheelSettings = camSettings;

    int fieldOfView = camSettings.csFieldOfView - yOffset * mMouseWheelScale;
    fieldOfView = std::clamp(fieldOfView, 40, 100);
    camSettings.csFieldOfView = fieldOfView;

    cam->setCameraSettings(camSettings);
  }
#endif
}

void VkRenderer::handleMovementKeys() {
#if !defined(__ANDROID__)
  mRenderData.rdMoveForward = 0;
  mRenderData.rdMoveRight = 0;
  mRenderData.rdMoveUp = 0;

  // forward to ImGui only when in edit mode
  if (mRenderData.rdApplicationMode == appMode::edit) {
    ImGuiIO& io = ImGui::GetIO();

    // hide from application if above ImGui window
    if (io.WantCaptureKeyboard || io.WantTextInput) {
      return;
    }
  }

  // do not accept input whenever any dialog request comes in
  if (mRenderData.rdRequestApplicationExit || mRenderData.rdNewConfigRequest ||
    mRenderData.rdLoadConfigRequest || mRenderData.rdSaveConfigRequest) {
    return;
  }

  // camera movement
  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = cam->getCameraSettings();
  if (mRenderData.rdApplicationMode == appMode::edit ||
      (mRenderData.rdApplicationMode == appMode::view && camSettings.csCamType == cameraType::free)) {
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_W) == GLFW_PRESS) {
      mRenderData.rdMoveForward += 4;
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_S) == GLFW_PRESS) {
      mRenderData.rdMoveForward -= 4;
    }

    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_A) == GLFW_PRESS) {
      mRenderData.rdMoveRight -= 4;
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_D) == GLFW_PRESS) {
      mRenderData.rdMoveRight += 4;
    }

    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_E) == GLFW_PRESS) {
      mRenderData.rdMoveUp += 4;
    }
    if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_Q) == GLFW_PRESS) {
      mRenderData.rdMoveUp -= 4;
    }

    // speed up movement with shift
    if ((glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
        (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)) {
      mRenderData.rdMoveForward *= 5;
      mRenderData.rdMoveRight *= 5;
      mRenderData.rdMoveUp *= 5;
    }
  }

  // instance movement
  std::shared_ptr<AssimpInstance> currentInstance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);

  if (mRenderData.rdApplicationMode != appMode::edit && camSettings.csCamType != cameraType::free) {
    if (mModelInstCamData.micSelectedInstance > 0) {
      // reset state to idle in every frame first
      moveState state = moveState::idle;
      moveState nextState = moveState::idle;
      moveDirection dir = moveDirection::none;

      // then check for movement and actions
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_A) == GLFW_PRESS) {
        state = moveState::walk;
        dir |= moveDirection::left;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_D) == GLFW_PRESS) {
        state = moveState::walk;
        dir |= moveDirection::right;
      }

      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_W) == GLFW_PRESS) {
        dir |= moveDirection::forward;
        state = moveState::walk;
        if ((glfwGetKey(mRenderData.rdWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
          (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)) {
          // only run forward in double speed
          state = moveState::run;
          }
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_S) == GLFW_PRESS) {
        state = moveState::walk;
        dir |= moveDirection::back;
      }
      currentInstance->updateInstanceState(state, dir);

      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_R) == GLFW_PRESS) {
        nextState = moveState::roll;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_E) == GLFW_PRESS) {
        nextState = moveState::punch;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_Q) == GLFW_PRESS) {
        nextState = moveState::kick;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_F) == GLFW_PRESS) {
        nextState = moveState::wave;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_U) == GLFW_PRESS) {
        nextState = moveState::interact;
        if (mRenderData.rdInteraction) {
          if (mRenderData.rdInteractWithInstanceId > 0) {
            mBehaviorManager->addEvent(getInstanceById(mRenderData.rdInteractWithInstanceId), nodeEvent::interaction);
          }
        }
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_P) == GLFW_PRESS) {
        nextState = moveState::pick;
      }
      if (glfwGetKey(mRenderData.rdWindow, GLFW_KEY_SPACE) == GLFW_PRESS) {
        if (state == moveState::walk || state == moveState::run) {
          nextState = moveState::jump;
        } else {
          nextState = moveState::hop;
        }
      }
      currentInstance->setNextInstanceState(nextState);

      // run and walk soundsteps
      if (currentInstance->getAnimState() == animationState::playIdleWalkRun &&
        mRenderData.rdApplicationMode == appMode::view &&
        (camSettings.csCamType == cameraType::firstPerson || camSettings.csCamType == cameraType::thirdPerson) &&
        cam->getInstanceToFollow() && currentInstance == cam->getInstanceToFollow()) {
        switch (state) {
          case moveState::run:
            mModelInstCamCallbacks.micPlayRunFootstepCallbackFunction();
            break;
          case moveState::walk:
            mModelInstCamCallbacks.micPlayWalkFootstepCallbackFunction();
            break;
          default:
            mModelInstCamCallbacks.micStopFootstepCallbackFunction();
            break;
        }
      } else {
        mModelInstCamCallbacks.micStopFootstepCallbackFunction();
      }
    }
  }
#endif
}

bool VkRenderer::createAABBLookup(std::shared_ptr<AssimpModel> model) {
  const int LOOKUP_SIZE = 1023;

  // we use a single instance per clip
  size_t numberOfClips = model->getAnimClips().size();
  size_t numberOfBones = model->getBoneList().size();

  // we need valid model with triangles and animations
  if (numberOfClips > 0 && numberOfBones > 0 && model->getTriangleCount() > 0) {
    Logger::log(1, "%s: playing animations for model %s\n", __FUNCTION__, model->getModelFileName().c_str());

    size_t trsMatrixSize = LOOKUP_SIZE * numberOfClips * numberOfBones * 3 * sizeof(glm::vec4);
    size_t bufferMatrixSize = LOOKUP_SIZE * numberOfClips * numberOfBones * sizeof(glm::mat4);

    mPerInstanceAnimData.clear();
    mPerInstanceAnimData.resize(LOOKUP_SIZE * numberOfClips);

    // play all animation steps
    size_t clipToStore = 0;
    float timeScaleFactor = model->getMaxClipDuration() / static_cast<float>(LOOKUP_SIZE);
    for (int lookups = 0; lookups < LOOKUP_SIZE; ++lookups) {
      for (size_t i = 0; i < numberOfClips; ++i) {

        PerInstanceAnimData animData{};
        animData.firstAnimClipNum = i;
        animData.secondAnimClipNum = 0;
        animData.firstClipReplayTimestamp = lookups * timeScaleFactor;
        animData.secondClipReplayTimestamp = 0.0f;
        animData.blendFactor = 0.0f;

        mPerInstanceAnimData.at(clipToStore + i) = animData;
      }
      clipToStore += numberOfClips;
    }

    // we need to update descriptors after the upload if buffer size changed
    bool bufferResized = false;
    mRenderData.rdUploadToUBOTimer.start();
    bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdPerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mPerInstanceAnimData);
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    // resize SSBO if needed
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame), bufferMatrixSize);
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShaderTRSMatrixBuffers.at(mRenderData.currentFrame), trsMatrixSize);

    if (bufferResized) {
      VkHelper::updateDescriptorSets(mRenderData);
      VkHelper::updateComputeDescriptorSets(mRenderData);
    }

    // record compute commands
    VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
      Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
      return false;
    }

    // do all calculations at once... may be undefined behavior
    // runComputeShaders(model, numberOfClips * LOOKUP_SIZE, 0, 0, true);

    uint32_t computeShaderClipOffset = 0;
    uint32_t computeShaderInstanceOffset = 0;
    for (int lookups = 0; lookups < LOOKUP_SIZE; ++lookups) {
      VkHelper::runComputeShaders(mRenderData, model, numberOfClips, computeShaderClipOffset, computeShaderInstanceOffset, true);

      computeShaderClipOffset += numberOfClips * numberOfBones;
      computeShaderInstanceOffset += numberOfClips;
    }

    if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
      return false;
    }

    // submit compute commands
    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

    result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
      return false;
    };

    // we must wait for the compute shaders to finish before we can read the bone data
    result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    // extract bone matrix from SSBO
    mRenderData.rdDownloadFromUBOTimer.start();
    std::vector<glm::mat4> boneMatrix = ShaderStorageBuffer::getSsboDataMat4(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame),
      0, LOOKUP_SIZE * numberOfClips * numberOfBones);
    mRenderData.rdDownloadFromUBOTime += mRenderData.rdDownloadFromUBOTimer.stop();

    // our axis aligned bounding box
    AABB aabb;

    std::vector<std::vector<AABB>> aabbLookups;
    aabbLookups.resize(numberOfClips);

    // some models have a scaling set here...
    glm::mat4 rootTransformMat = glm::transpose(model->getRootTranformationMatrix());

    // and loop over clips and bones
    size_t offset = 0;
    for (int lookups = 0; lookups < LOOKUP_SIZE; ++lookups) {
      for (size_t i = 0; i < numberOfClips; ++i) {
        // add first point
        glm::vec3 bonePos = (rootTransformMat * boneMatrix.at(offset + numberOfBones * i))[3];
        aabb.create(bonePos);

        // extend AABB for other points
        for (size_t j = 1; j < numberOfBones; ++j) {
          // Shader:  uint index = node + numberOfBones * instance;
          glm::vec3 bonePos = (rootTransformMat * boneMatrix.at(offset + numberOfBones * i + j))[3];
          aabb.addPoint(bonePos);
        }

        // add all animation frames for the current clip
        aabbLookups.at(i).emplace_back(aabb);
      }
      offset += numberOfClips * numberOfBones;
    }

    model->setAABBLookup(aabbLookups);
  }

  return true;
}

bool VkRenderer::checkForInstanceCollisions() {
  // get bounding box intersections
  mModelInstCamData.micInstanceCollisions = mOctree->findAllIntersections();

  // save bounding box collisions of non-animated instances
  std::set<std::pair<int, int>> nonAnimatedCollisions{};
  for (const auto& instancePair : mModelInstCamData.micInstanceCollisions) {
    if (!mModelInstCamData.micAssimpInstances.at(instancePair.first)->getModel()->hasAnimations() ||
        !mModelInstCamData.micAssimpInstances.at(instancePair.second)->getModel()->hasAnimations()) {
      nonAnimatedCollisions.insert(instancePair);
    }
  }

  if (mRenderData.rdCheckCollisions == collisionChecks::boundingSpheres) {
    mBoundingSpheresPerInstance.clear();

    // calculate collision spheres per model
    std::map<std::string, std::set<int>> modelToInstanceMapping;

    for (const auto& instancePairs : mModelInstCamData.micInstanceCollisions) {
      modelToInstanceMapping[mModelInstCamData.micAssimpInstances.at(instancePairs.first)->getModel()->getModelFileName()].insert(instancePairs.first);
      modelToInstanceMapping[mModelInstCamData.micAssimpInstances.at(instancePairs.second)->getModel()->getModelFileName()].insert(instancePairs.second);
    }

    // count total number of spheres to calculate
    int totalSpheres = 0;
    for (const auto& collisionInstances : modelToInstanceMapping) {
      std::shared_ptr<AssimpModel> model = getModel(collisionInstances.first);
      if (!model->hasAnimations()) {
        continue;
      }

      std::string modelName = model->getModelFileName();
      std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[modelName];

      size_t numberOfBones = model->getBoneList().size();
      size_t numInstances = instances.size();

      size_t numberOfSpheres = numInstances * numberOfBones;

      totalSpheres += numberOfSpheres;
    }

    // resize SSBO if needed
    bool bufferResized = ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdBoundingSphereBuffers.at(mRenderData.currentFrame), totalSpheres * sizeof(glm::vec4));

    if (bufferResized) {
      VkHelper::updateSphereComputeDescriptorSets(mRenderData);
    }

    int sphereModelOffset = 0;
    for (const auto& collisionInstances : modelToInstanceMapping) {
      std::shared_ptr<AssimpModel> model = getModel(collisionInstances.first);
      if (!model->hasAnimations()) {
        continue;
      }

      size_t numInstances = collisionInstances.second.size();
      std::vector<int> instanceIds = std::vector(collisionInstances.second.begin(), collisionInstances.second.end());

      size_t numberOfBones = model->getBoneList().size();

      size_t numberOfSpheres = numInstances * numberOfBones;
      size_t trsMatrixSize = numInstances * numberOfBones * 3 * sizeof(glm::vec4);
      size_t bufferMatrixSize = numInstances * numberOfBones * sizeof(glm::mat4);

      // Vulkan needs separate buffers
      mSphereWorldPosMatrices.clear();
      mSphereWorldPosMatrices.resize(numInstances);

      mSpherePerInstanceAnimData.clear();
      mSpherePerInstanceAnimData.resize(numInstances);

      for (size_t i = 0; i < numInstances; ++i) {
        InstanceSettings instSettings = mModelInstCamData.micAssimpInstances.at(instanceIds.at(i))->getInstanceSettings();

        PerInstanceAnimData animData{};
        animData.firstAnimClipNum = instSettings.isFirstAnimClipNr;
        animData.secondAnimClipNum = instSettings.isSecondAnimClipNr;
        animData.firstClipReplayTimestamp = instSettings.isFirstClipAnimPlayTimePos;
        animData.secondClipReplayTimestamp = instSettings.isSecondClipAnimPlayTimePos;
        animData.blendFactor = instSettings.isAnimBlendFactor;

        mSpherePerInstanceAnimData.at(i) = animData;

        mSphereWorldPosMatrices.at(i) = mModelInstCamData.micAssimpInstances.at(instanceIds.at(i))->getWorldTransformMatrix();
      }

      // we need to update descriptors after the upload if buffer size changed
      bool bufferResized = false;
      mRenderData.rdUploadToUBOTimer.start();
      bufferResized =  ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSpherePerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mSpherePerInstanceAnimData);
      bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSphereModelRootMatrixBuffers.at(mRenderData.currentFrame), mSphereWorldPosMatrices);
      mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

      // resize SSBO if needed
      bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereBoneMatrixBuffers.at(mRenderData.currentFrame), bufferMatrixSize);
      bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereTRSMatrixBuffers.at(mRenderData.currentFrame), trsMatrixSize);

      if (bufferResized) {
        VkHelper::updateDescriptorSets(mRenderData);
        VkHelper::updateSphereComputeDescriptorSets(mRenderData);
      }

      // in case data was changed
      model->updateBoundingSphereAdjustments(mRenderData);

      // record compute commands
      VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
        return false;
      }

      if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
        Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
        return false;
      }

      if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
        Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
        return false;
      }

      VkHelper::runBoundingSphereComputeShaders(mRenderData, model, numInstances, sphereModelOffset);
      sphereModelOffset += numberOfSpheres;

      if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
        Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
        return false;
      }

      // submit compute commands
      VkSubmitInfo computeSubmitInfo{};
      computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      computeSubmitInfo.commandBufferCount = 1;
      computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

      result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
        return false;
      };

      // we must wait for the compute shaders to finish before we can read the bone data
      result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
        return false;
      }

    }

    // read sphere SSBO
    mRenderData.rdDownloadFromUBOTimer.start();
    std::vector<glm::vec4> boundingSpheres = ShaderStorageBuffer::getSsboDataVec4(mRenderData, mRenderData.rdBoundingSphereBuffers.at(mRenderData.currentFrame), totalSpheres);
    mRenderData.rdDownloadFromUBOTime += mRenderData.rdDownloadFromUBOTimer.stop();

    sphereModelOffset = 0;
    for (const auto& collisionInstances : modelToInstanceMapping) {
      std::shared_ptr<AssimpModel> model = getModel(collisionInstances.first);
      if (!model->hasAnimations()) {
        continue;
      }

      size_t numInstances = collisionInstances.second.size();
      std::vector<int> instanceIds = std::vector(collisionInstances.second.begin(), collisionInstances.second.end());

      size_t numberOfBones = model->getBoneList().size();
      size_t numberOfSpheres = numInstances * numberOfBones;

      for (size_t i = 0; i < numInstances; ++i) {
        InstanceSettings instSettings = mModelInstCamData.micAssimpInstances.at(instanceIds.at(i))->getInstanceSettings();
        int instanceIndex = instSettings.isInstanceIndexPosition;
        mBoundingSpheresPerInstance[instanceIndex].resize(numberOfBones);

        std::copy(boundingSpheres.begin() + sphereModelOffset + i * numberOfBones,
          boundingSpheres.begin() + sphereModelOffset + (i + 1) * numberOfBones,
          mBoundingSpheresPerInstance[instanceIndex].begin());
      }
      sphereModelOffset += numberOfSpheres;
    }

    checkForBoundingSphereCollisions();
  }

  // add up non-animated collisions
  mModelInstCamData.micInstanceCollisions.merge(nonAnimatedCollisions);

  // get (possibly cleaned) number of collisions
  mRenderData.rdNumberOfCollisions = mModelInstCamData.micInstanceCollisions.size();

  if (mRenderData.rdCheckCollisions != collisionChecks::none) {
    reactToInstanceCollisions();
  }
  return true;
}

void VkRenderer::checkForLevelCollisions() {
  mLevelCollidingTriangleMesh->vertices.clear();

  for (const auto& instance : mModelInstCamData.micAssimpInstances) {
    InstanceSettings instSettings = instance->getInstanceSettings();
    if (instSettings.isInstanceIndexPosition == 0) {
      continue;
    }
    mRenderData.rdNumberOfCollidingTriangles += instSettings.isCollidingTriangles.size();

    instance->setCurrentGroundTriangleIndex(-1);
    for (const auto& tri : instSettings.isCollidingTriangles) {
      glm::vec3 vertexColor = glm::vec3(1.0f, 1.0f, 1.0f);

      // check for slope
      bool isWalkable = false;

      if (glm::dot(tri.normal, glm::vec3(0.0f, 1.0f, 0.0f)) >= std::cos(glm::radians(mRenderData.rdMaxLevelGroundSlopeAngle))) {
        isWalkable = true;
        // find triangle we are walking on
        AABB instanceAABB = instance->getModel()->getAABB(instSettings);
        float instanceHeight = instanceAABB.getMaxPos().y - instanceAABB.getMinPos().y;
        float instanceHalfHeight = instanceHeight / 2.0f;
        std::optional<glm::vec3> result = Tools::rayTriangleIntersection(instSettings.isWorldPosition +
          glm::vec3(0.0f, instanceHalfHeight, 0.0f), glm::vec3(0.0f, -instanceHeight, 0.0f), tri);
        if (result.has_value()) {
          instance->setCurrentGroundTriangleIndex(tri.index);
        }
      }

      // stair handling
      bool isStair = false;
      AABB triangleAABB;
      triangleAABB.create(tri.points.at(0));
      triangleAABB.addPoint(tri.points.at(1));
      triangleAABB.addPoint(tri.points.at(2));

      // ignore triangles smaller than rdMaxStairHeight if they are on the foot of the instance
      if (triangleAABB.getMaxPos().y - triangleAABB.getMinPos().y < mRenderData.rdMaxStairstepHeight &&
        triangleAABB.getMinPos().y > instSettings.isWorldPosition.y - mRenderData.rdMaxStairstepHeight &&
        triangleAABB.getMaxPos().y < instSettings.isWorldPosition.y + mRenderData.rdMaxStairstepHeight) {
        isStair = true;
        }

       // check if upper bounds of structures are below foot level, offset max stair height high
       bool isBelowFootLevel = false;
      if (triangleAABB.getMaxPos().y < instSettings.isWorldPosition.y + mRenderData.rdMaxStairstepHeight) {
        isBelowFootLevel = true;
      }

      // check if we have a ground triangle
      if (isWalkable || isStair || isBelowFootLevel) {
        vertexColor = glm::vec3(0.0f, 0.0f, 1.0f);
        mRenderData.rdNumberOfCollidingGroundTriangles++;
      } else {
        vertexColor = glm::vec3(1.0f, 0.0f, 0.0f);
        // fire wall collision event only when instance is on ground
        if (instSettings.isInstanceOnGround) {
          mModelInstCamCallbacks.micNodeEventCallbackFunction(instance, nodeEvent::instanceToLevelCollision);
        }
      }

      if (mRenderData.rdDrawLevelCollisionTriangles) {
        VkSimpleVertex vert;
        vert.color = vertexColor;
        // move wireframe overdraw a bit above the planes
        vert.position = glm::vec4(tri.points.at(0) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);
        vert.position = glm::vec4(tri.points.at(1) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);

        vert.position = glm::vec4(tri.points.at(1) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);
        vert.position = glm::vec4(tri.points.at(2) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);

        vert.position = glm::vec4(tri.points.at(2) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);
        vert.position = glm::vec4(tri.points.at(0) + tri.normal * 0.01f, 1.0f);
        mLevelCollidingTriangleMesh->vertices.push_back(vert);

        mLineIndexCount += mLevelCollidingTriangleMesh->vertices.size();
        mLineMesh->vertices.insert(mLineMesh->vertices.end(),
          mLevelCollidingTriangleMesh->vertices.begin(),
          mLevelCollidingTriangleMesh->vertices.end());
      }
    }
  }
}

void VkRenderer::checkForBorderCollisions() {
  for (const auto& instancesPerModel : mModelInstCamData.micAssimpInstancesPerModel) {
    std::shared_ptr<AssimpModel> model = getModel(instancesPerModel.first);
    // non-animated models have no lookup data
    if (!model || !model->hasAnimations()) {
      continue;
    }

    std::vector<std::shared_ptr<AssimpInstance>> instances = instancesPerModel.second;
    for (size_t i = 0; i < instances.size(); ++i) {
      InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

      // check world borders
      AABB instanceAABB = model->getAABB(instSettings);
      glm::vec3 minPos = instanceAABB.getMinPos();
      glm::vec3 maxPos = instanceAABB.getMaxPos();
      if (minPos.x < mWorldBoundaries->getFrontTopLeft().x || maxPos.x > mWorldBoundaries->getRight() ||
          minPos.y < mWorldBoundaries->getFrontTopLeft().y || maxPos.y > mWorldBoundaries->getBottom() ||
          minPos.z < mWorldBoundaries->getFrontTopLeft().z || maxPos.z > mWorldBoundaries->getBack()) {
        mModelInstCamCallbacks.micNodeEventCallbackFunction(instances.at(i), nodeEvent::instanceToEdgeCollision);
      }
    }
  }
}

void VkRenderer::checkForBoundingSphereCollisions() {
  std::set<std::pair<int, int>> sphereCollisions{};

  for (const auto& instancePairs : mModelInstCamData.micInstanceCollisions) {
    int firstId = instancePairs.first;
    int secondId = instancePairs.second;

    // brute force check of sphere vs sphere
    bool collisionDetected = false;

    for (size_t first = 0; first < mBoundingSpheresPerInstance[firstId].size(); ++first) {
      glm::vec4 firstSphereData = mBoundingSpheresPerInstance[firstId].at(first);
      float firstRadius = firstSphereData.w;

      // no need to check disabled spheres
      if (firstRadius == 0.0f) {
        continue;
      }

      glm::vec3 firstSpherePos = glm::vec3(firstSphereData.x, firstSphereData.y, firstSphereData.z);

      for (size_t second = 0; second < mBoundingSpheresPerInstance[secondId].size(); ++second) {
        glm::vec4 secondSphereData = mBoundingSpheresPerInstance[secondId].at(second);
        float secondRadius = secondSphereData.w;

        // no need to check disabled spheres
        if (secondRadius == 0.0f) {
          continue;
        }

        glm::vec3 secondSpherePos = glm::vec3(secondSphereData.x, secondSphereData.y, secondSphereData.z);

        // check for intersections 
        glm::vec3 centerDistance = firstSpherePos - secondSpherePos;
        float centerDistanceSquared = glm::dot(centerDistance, centerDistance);

        float sphereRadiusSum = firstRadius + secondRadius;
        float sphereRadiusSumSquared = sphereRadiusSum * sphereRadiusSum;

        // flag as a hit and exit immediately
        if (centerDistanceSquared <= sphereRadiusSumSquared) {
          collisionDetected = true;
          break;
        }
      }

      if (collisionDetected) {
        break;
      }
    }

    // store collisions in set
    if (collisionDetected) {
      sphereCollisions.insert({firstId, secondId});
    }
  }

  // replace collided instance data with new ones
  mModelInstCamData.micInstanceCollisions.clear();
  mModelInstCamData.micInstanceCollisions.insert(sphereCollisions.begin(), sphereCollisions.end());
}

void VkRenderer::reactToInstanceCollisions() {
  std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstances;

  for (const auto& instancePairs : mModelInstCamData.micInstanceCollisions) {
    std::shared_ptr<AssimpInstance> firstInstance = instances.at(instancePairs.first);
    InstanceSettings firstInstSettings = firstInstance->getInstanceSettings();

    std::shared_ptr<AssimpInstance> secondInstance = instances.at(instancePairs.second);
    InstanceSettings secondInstSettings = secondInstance->getInstanceSettings();

    mModelInstCamCallbacks.micNodeEventCallbackFunction(firstInstance, nodeEvent::instanceToInstanceCollision);
    mModelInstCamCallbacks.micNodeEventCallbackFunction(secondInstance, nodeEvent::instanceToInstanceCollision);

    // disable navigation if we collide with target
    if (firstInstSettings.isNavigationEnabled && firstInstSettings.isPathTargetInstance == secondInstSettings.isInstanceIndexPosition) {
      firstInstance->setNavigationEnabled(false);
      firstInstance->setPathTargetInstanceId(-1);
      mModelInstCamCallbacks.micNodeEventCallbackFunction(firstInstance, nodeEvent::navTargetReached);
    }
    if (secondInstSettings.isNavigationEnabled && secondInstSettings.isPathTargetInstance == firstInstSettings.isInstanceIndexPosition) {
      secondInstance->setNavigationEnabled(false);
      secondInstance->setPathTargetInstanceId(-1);
      mModelInstCamCallbacks.micNodeEventCallbackFunction(secondInstance, nodeEvent::navTargetReached);
    }
  }
}


void VkRenderer::findInteractionInstances() {
  if (!mRenderData.rdInteraction) {
    return;
  }
  mRenderData.rdInteractionCandidates.clear();

  if (mModelInstCamData.micSelectedInstance == 0) {
    return;
  }
  std::shared_ptr<AssimpInstance> currentInstance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
  InstanceSettings curInstSettings = currentInstance->getInstanceSettings();

  // query octree with a bounding box
  glm::vec3 instancePos = curInstSettings.isWorldPosition;
  glm::vec3 querySize = glm::vec3(mRenderData.rdInteractionMaxRange);
  BoundingBox3D queryBox = BoundingBox3D(instancePos - querySize / 2.0f, querySize);

  std::set<int> queriedNearInstances = mOctree->query(queryBox);

  // skip ourselve
  queriedNearInstances.erase(curInstSettings.isInstanceIndexPosition);

  if (queriedNearInstances.empty()) {
    return;
  }

  std::set<int> nearInstances{};
  for (const auto& id : queriedNearInstances) {
    std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(id);
    InstanceSettings instSettings = instance->getInstanceSettings();

    float distance = glm::length(instSettings.isWorldPosition - curInstSettings.isWorldPosition);
    if (distance > mRenderData.rdInteractionMinRange) {
      nearInstances.emplace(id);
    }
  }

  if (nearInstances.empty()) {
    return;
  }

  mRenderData.rdNumberOfInteractionCandidates = nearInstances.size();

  if (mRenderData.rdDrawInteractionAABBs == interactionDebugDraw::distance) {
    mRenderData.rdInteractionCandidates = nearInstances;
  }

  std::set<int> instancesFacingToUs{};
  for (const auto& id : nearInstances) {
    std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(id);
    InstanceSettings instSettings = instance->getInstanceSettings();

    glm::vec3 distanceVector = glm::normalize(instSettings.isWorldPosition - curInstSettings.isWorldPosition);
    float angle = glm::degrees(glm::acos(glm::dot(currentInstance->get2DRotationVector(), distanceVector)));
    float instAngle = glm::degrees(glm::acos(glm::dot(instance->get2DRotationVector(), -distanceVector)));

    if (angle < mRenderData.rdInteractionFOV && instAngle < mRenderData.rdInteractionFOV)  {
      instancesFacingToUs.emplace(id);
    }
  }

  if (instancesFacingToUs.empty()) {
    return;
  }

  if (mRenderData.rdDrawInteractionAABBs == interactionDebugDraw::facingTowardsUs) {
    mRenderData.rdInteractionCandidates = instancesFacingToUs;
  }

  std::vector<std::pair<float, int>> sortedDistances;
  for (const auto& id : instancesFacingToUs) {
    std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(id);
    InstanceSettings instSettings = instance->getInstanceSettings();

    float distance = glm::length(instSettings.isWorldPosition - curInstSettings.isWorldPosition);
    sortedDistances.emplace_back(std::make_pair(distance, id));
  }

  std::sort(sortedDistances.begin(), sortedDistances.end());
  mRenderData.rdInteractWithInstanceId = sortedDistances.begin()->second;

  if (mRenderData.rdDrawInteractionAABBs == interactionDebugDraw::nearestCandidate) {
    mRenderData.rdInteractionCandidates = { mRenderData.rdInteractWithInstanceId };
  }
}

void VkRenderer::createInteractionDebug() {
  if (mModelInstCamData.micSelectedInstance == 0 || !mRenderData.rdInteraction) {
    return;
  }

  glm::vec4 aabbColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

  VkSimpleMesh InteractionMesh;
  VkSimpleVertex vertex;
  vertex.color = aabbColor;

  std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
  InstanceSettings instSettings = instance->getInstanceSettings();

  if (mRenderData.rdDrawInteractionRange) {
    glm::vec3 instancePos = instSettings.isWorldPosition;
    glm::vec2 instancePos2D = glm::vec2(instancePos.x, instancePos.z);

    glm::vec2 minQueryBoxTopLeft = glm::vec2(instancePos2D) - glm::vec2(mRenderData.rdInteractionMinRange / 2.0f);
    glm::vec2 minQueryBoxBottomRight = glm::vec2(instancePos2D) + glm::vec2(mRenderData.rdInteractionMinRange / 2.0f);

    glm::vec2 maxQueryBoxTopLeft = glm::vec2(instancePos2D) - glm::vec2(mRenderData.rdInteractionMaxRange / 2.0f);
    glm::vec2 maxQueryBoxBottomRight = glm::vec2(instancePos2D) + glm::vec2(mRenderData.rdInteractionMaxRange / 2.0f);

    // min range
    vertex.position = glm::vec3(minQueryBoxTopLeft.x, instancePos.y, minQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(minQueryBoxTopLeft.x, instancePos.y, minQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(minQueryBoxTopLeft.x, instancePos.y, minQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(minQueryBoxBottomRight.x, instancePos.y, minQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(minQueryBoxBottomRight.x, instancePos.y, minQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(minQueryBoxBottomRight.x, instancePos.y, minQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(minQueryBoxBottomRight.x, instancePos.y, minQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(minQueryBoxTopLeft.x, instancePos.y, minQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);

    // max range
    vertex.position = glm::vec3(maxQueryBoxTopLeft.x, instancePos.y, maxQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(maxQueryBoxTopLeft.x, instancePos.y, maxQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(maxQueryBoxTopLeft.x, instancePos.y, maxQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(maxQueryBoxBottomRight.x, instancePos.y, maxQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(maxQueryBoxBottomRight.x, instancePos.y, maxQueryBoxBottomRight.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(maxQueryBoxBottomRight.x, instancePos.y, maxQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);

    vertex.position = glm::vec3(maxQueryBoxBottomRight.x, instancePos.y, maxQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);
    vertex.position = glm::vec3(maxQueryBoxTopLeft.x, instancePos.y, maxQueryBoxTopLeft.y);
    InteractionMesh.vertices.emplace_back(vertex);
  }

  // draw FOV lines
  if (mRenderData.rdDrawInteractionFOV) {
    std::set<int> drawFOVLines = mRenderData.rdInteractionCandidates;
    drawFOVLines.emplace(instSettings.isInstanceIndexPosition);

    for (const auto id : drawFOVLines) {
      std::shared_ptr<AssimpInstance> fovInstance = mModelInstCamData.micAssimpInstances.at(id);
      InstanceSettings fovInstSettings = fovInstance->getInstanceSettings();

      vertex.position = fovInstSettings.isWorldPosition;
      InteractionMesh.vertices.emplace_back(vertex);

      float minAngle = fovInstSettings.isWorldRotation.y - mRenderData.rdInteractionFOV;
      if (minAngle < -180.0f) {
        minAngle += 360.0f;
      }
      if (minAngle > 180.0f) {
        minAngle -= 360.0f;
      }
      float sinRot = std::sin(glm::radians(minAngle));
      float cosRot = std::cos(glm::radians(minAngle));
      vertex.position = fovInstSettings.isWorldPosition + glm::normalize(glm::vec3(sinRot, 0.0f, cosRot)) * 3.0f;
      InteractionMesh.vertices.emplace_back(vertex);

      vertex.position = fovInstSettings.isWorldPosition;
      InteractionMesh.vertices.emplace_back(vertex);

      float maxAngle = fovInstSettings.isWorldRotation.y + mRenderData.rdInteractionFOV;
      if (maxAngle < -180.0f) {
        maxAngle += 360.0f;
      }
      if (maxAngle > 180.0f) {
        maxAngle -= 360.0f;
      }
      sinRot = std::sin(glm::radians(maxAngle));
      cosRot = std::cos(glm::radians(maxAngle));
      vertex.position = fovInstSettings.isWorldPosition + glm::normalize(glm::vec3(sinRot, 0.0f, cosRot)) * 3.0f;
      InteractionMesh.vertices.emplace_back(vertex);
    }
  }

  mLineIndexCount += InteractionMesh.vertices.size();
  mLineMesh->vertices.insert(mLineMesh->vertices.end(), InteractionMesh.vertices.begin(), InteractionMesh.vertices.end());

  // draw instance AABBs
  if (mRenderData.rdInteractionCandidates.empty()) {
    return;
  }

  std::vector<std::shared_ptr<AssimpInstance>> instancesToDraw{};
  for (const int id : mRenderData.rdInteractionCandidates) {
    instancesToDraw.emplace_back(mModelInstCamData.micAssimpInstances.at(id));
  }

  createAABBDebugLiness(instancesToDraw, aabbColor);
}

void VkRenderer::createAABBDebugLiness(std::vector<std::shared_ptr<AssimpInstance>> instances, glm::vec4 aabbColor) {
  std::shared_ptr<VkSimpleMesh> aabbLineMesh = nullptr;;

  mAABBMesh->vertices.clear();
  AABB instanceAABB;
  mAABBMesh->vertices.resize(instances.size() * instanceAABB.getAABBLines(aabbColor)->vertices.size());

  for (size_t i = 0; i < instances.size(); ++i) {
    InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

    // skip null instance
    if (instSettings.isInstanceIndexPosition == 0) {
      continue;
    }

    std::shared_ptr<AssimpModel> model = instances.at(i)->getModel();

    instanceAABB = model->getAABB(instSettings);
    aabbLineMesh = instanceAABB.getAABBLines(aabbColor);

    if (aabbLineMesh) {
      std::copy(aabbLineMesh->vertices.begin(), aabbLineMesh->vertices.end(),
        mAABBMesh->vertices.begin() + i * aabbLineMesh->vertices.size());
    }
  }

  mLineIndexCount += mAABBMesh->vertices.size();
  mLineMesh->vertices.insert(mLineMesh->vertices.end(), mAABBMesh->vertices.begin(), mAABBMesh->vertices.end());
}

void VkRenderer::resetLevelData() {
  mRenderData.rdWorldStartPos = mRenderData.rdDefaultWorldStartPos;
  mRenderData.rdWorldSize = mRenderData.rdDefaultWorldSize;

  mRenderData.rdNearPlane = 0.1f;
  mRenderData.rdFarPlane = 500.0f;

  mWorldBoundaries = std::make_shared<BoundingBox3D>(mRenderData.rdDefaultWorldStartPos, mRenderData.rdDefaultWorldSize);
  mRenderData.rdWorldStartPos = mWorldBoundaries->getFrontTopLeft();
  mRenderData.rdWorldSize = mWorldBoundaries->getSize();
  initOctree(mRenderData.rdOctreeThreshold, mRenderData.rdOctreeMaxDepth);
  initTriangleOctree(mRenderData.rdOctreeThreshold, mRenderData.rdOctreeMaxDepth);

  mRenderData.rdDrawLevelAABB = false;
  mRenderData.rdDrawLevelWireframe = false;
  mRenderData.rdDrawLevelWireframeMiniMap = false;
  mRenderData.rdDrawLevelOctree = false;
  mRenderData.rdDrawLevelCollisionTriangles = false;
  mRenderData.rdEnableSimpleGravity = false;

  mRenderData.rdMaxLevelGroundSlopeAngle = 0.0f;
  mRenderData.rdLevelOctreeThreshold = 10;
  mRenderData.rdLevelOctreeMaxDepth = 5;

  mRenderData.rdEnableFeetIK = false;
  mRenderData.rdDrawIKDebugLines = false;

  mRenderData.rdDrawNeighborTriangles = false;
  mRenderData.rdDrawGroundTriangles = false;
  mRenderData.rdDrawInstancePaths = false;

  mRenderData.rdEnableNavigation = false;

  mRenderData.rdDrawSkybox = false;
  mRenderData.rdFogDensity = 0.0f;

  mRenderData.rdLightSourceAngleEastWest = 40.0f;
  mRenderData.rdLightSourceAngleNorthSouth = 40.0f;
  mRenderData.rdLightSourceColor = glm::vec3(1.0f);
  mRenderData.rdLightSourceIntensity = 1.0f;

  mRenderData.rdEnableTimeOfDay = false;
  mRenderData.rdTimeOfDay = 720.0f;
  mRenderData.rdTimeScaleFactor = 10.0f;
  mRenderData.rdTimeOfDayPreset = timeOfDay::fullLight;

  mRenderData.rdEnableSSAO = false;
  mRenderData.rdSSAORadius = 5.0f;
  mRenderData.rdSSAOBias = 0.1f;
  mRenderData.rdSSAOExponent = 4;
  mRenderData.rdEnableSSAOBlur = false;
  mRenderData.rdSSAOBlurRadius = 3;

  mRenderData.rdEnableShadowMap = false;
  mRenderData.rdShadowMapSplitLambda = 0.95f;
  mRenderData.rdShadowMapConstantDepthBias = 1.25f;
  mRenderData.rdShadowMapSlopeDepthBias = 1.75f;
  mRenderData.rdEnableShadowMapPCF = false;
  mRenderData.rdShadowMapPCFScale = 1.0f;
  mRenderData.rdShadowMapPCFRange = 1;

  mRenderData.rdEnableLightDebug = false;
  mRenderData.rdEnableLightSphereDebug = false;
  mRenderData.rdDynLightShadowMapConstantDepthBias = 1.25f;
  mRenderData.rdDynLightShadowMapSlopeDepthBias = 1.75f;

  // add loaded levels to pending delete list
  for (const auto& level : mModelInstCamData.micLevels) {
    if (level && (level->getTriangleCount() > 0)) {
      mModelInstCamData.micPendingDeleteAssimpLevels.insert(level);
    }
  }

  mModelInstCamData.micLevels.erase(mModelInstCamData.micLevels.begin(), mModelInstCamData.micLevels.end());

  // re-add null level
  std::shared_ptr<AssimpLevel> nullLevel = std::make_shared<AssimpLevel>();
  mModelInstCamData.micLevels.emplace_back(nullLevel);

  mAllLevelAABB.clear();

  mModelInstCamData.micSelectedLevel = 0;

  // stop music too
  if (mModelInstCamCallbacks.micStopMusicCallbackFunction) {
    mModelInstCamCallbacks.micStopMusicCallbackFunction();
  }

  mUserInterface.resetPositionWindowOctreeView();
}

void VkRenderer::createInstanceCollisionDebug() {
  // draw AABB lines and bounding sphere of selected instance
  if (mRenderData.rdDrawCollisionAABBs == collisionDebugDraw::colliding ||
    mRenderData.rdDrawCollisionAABBs == collisionDebugDraw::all) {
    std::set<int> uniqueInstanceIds;

    for (const auto& colliding : mModelInstCamData.micInstanceCollisions) {
      uniqueInstanceIds.insert(colliding.first);
      uniqueInstanceIds.insert(colliding.second);
    }

    std::vector<std::shared_ptr<AssimpInstance>> instancestoDraw;
    glm::vec4 aabbColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    // draw colliding instances in red
    if (mRenderData.rdDrawCollisionAABBs == collisionDebugDraw::colliding ||
      mRenderData.rdDrawCollisionAABBs == collisionDebugDraw::all) {
      for (const auto id : uniqueInstanceIds) {
        instancestoDraw.push_back(mModelInstCamData.micAssimpInstances.at(id));
      }
      // draw red lines for collisions
      aabbColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
      createAABBDebugLiness(instancestoDraw, aabbColor);
    }

    // draw yellow lines for non-colliiding instances
    // we can just overdraw the lines, thanks to the z-buffer the red lines stay :)
    if (mRenderData.rdDrawCollisionAABBs == collisionDebugDraw::all) {
      instancestoDraw = mModelInstCamData.micAssimpInstances;
      aabbColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
      createAABBDebugLiness(instancestoDraw, aabbColor);
    }
  }
}

bool VkRenderer::createSelectedBoundingSpheres() {
  if (mModelInstCamData.micSelectedInstance > 0 ) {
    std::shared_ptr<AssimpInstance> instance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
    std::shared_ptr<AssimpModel> model = instance->getModel();

    if (!model->hasAnimations()) {
      return false;
    }

    size_t numberOfBones = model->getBoneList().size();

    size_t numberOfSpheres = numberOfBones;
    size_t trsMatrixSize = numberOfBones * 3 * sizeof(glm::vec4);
    size_t bufferMatrixSize = numberOfBones * sizeof(glm::mat4);

    mSphereWorldPosMatrices.clear();
    mSphereWorldPosMatrices.resize(1);

    mSpherePerInstanceAnimData.clear();
    mSpherePerInstanceAnimData.resize(1);

    InstanceSettings instSettings = instance->getInstanceSettings();

    PerInstanceAnimData animData{};
    animData.firstAnimClipNum = instSettings.isFirstAnimClipNr;
    animData.secondAnimClipNum = instSettings.isSecondAnimClipNr;
    animData.firstClipReplayTimestamp = instSettings.isFirstClipAnimPlayTimePos;
    animData.secondClipReplayTimestamp = instSettings.isSecondClipAnimPlayTimePos;
    animData.blendFactor = instSettings.isAnimBlendFactor;

    mSpherePerInstanceAnimData.at(0) = animData;

    mSphereWorldPosMatrices.at(0) = instance->getWorldTransformMatrix();

    // resize SSBOs if needed
    bool bufferResized = false;
    mRenderData.rdUploadToUBOTimer.start();
    bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSpherePerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mSpherePerInstanceAnimData);
    bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSphereModelRootMatrixBuffers.at(mRenderData.currentFrame), mSphereWorldPosMatrices);
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    // resize SSBO if needed
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereBoneMatrixBuffers.at(mRenderData.currentFrame), bufferMatrixSize);
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereTRSMatrixBuffers.at(mRenderData.currentFrame), trsMatrixSize);
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdBoundingSphereBuffers.at(mRenderData.currentFrame), numberOfSpheres * sizeof(glm::vec4));

    if (bufferResized) {
      VkHelper::updateDescriptorSets(mRenderData);
      VkHelper::updateSphereComputeDescriptorSets(mRenderData);
    }

    // in case data was changed
    model->updateBoundingSphereAdjustments(mRenderData);

    // record compute commands
    VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
      Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
      return false;
    }

    VkHelper::runBoundingSphereComputeShaders(mRenderData, model, 1, 0);
    mCollidingSphereCount = numberOfSpheres;

    if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
      return false;
    }

    // submit compute commands
    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

    result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
      return false;
    };

    // we must wait for the compute shaders to finish before we can read the bone data
    result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  if (mCollidingSphereCount > 0) {
    mRenderData.rdUploadToVBOTimer.start();
    VertexBuffer::uploadData(mRenderData, mRenderData.rdSphereVertexBuffers.at(mRenderData.currentFrame), mSphereMesh.vertices);
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }

  return true;
}

bool VkRenderer::createCollidingBoundingSpheres() {
  // split instances in models - use a std::set to get unique instance IDs
  std::map<std::string, std::set<int>> modelToInstanceMapping;

  for (const auto& instancePairs : mModelInstCamData.micInstanceCollisions) {
    modelToInstanceMapping[mModelInstCamData.micAssimpInstances.at(instancePairs.first)->getModel()->getModelFileName()].insert(instancePairs.first);
    modelToInstanceMapping[mModelInstCamData.micAssimpInstances.at(instancePairs.second)->getModel()->getModelFileName()].insert(instancePairs.second);
  }

  int totalSpheres = 0;
  for (const auto& collisionInstances : modelToInstanceMapping) {
    std::shared_ptr<AssimpModel> model = getModel(collisionInstances.first);
    if (!model->hasAnimations()) {
      continue;
    }

    std::string modelName = model->getModelFileName();
    std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[modelName];

    size_t numberOfBones = model->getBoneList().size();
    size_t numInstances = instances.size();

    size_t numberOfSpheres = numInstances * numberOfBones;

    totalSpheres += numberOfSpheres;
  }

  // resize SSBO if needed
  bool bufferResized = false;
  bufferResized = ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdBoundingSphereBuffers.at(mRenderData.currentFrame), totalSpheres * sizeof(glm::vec4));

  if (bufferResized) {
    VkHelper::updateSphereComputeDescriptorSets(mRenderData);
  }

  int sphereModelOffset = 0;
  for (const auto& collisionInstances : modelToInstanceMapping) {
    std::shared_ptr<AssimpModel> model = getModel(collisionInstances.first);
    if (!model->hasAnimations()) {
      continue;
    }

    size_t numInstances = collisionInstances.second.size();
    std::vector<int> instanceIds = std::vector(collisionInstances.second.begin(), collisionInstances.second.end());

    size_t numberOfBones = model->getBoneList().size();

    size_t numberOfSpheres = numInstances * numberOfBones;
    size_t trsMatrixSize = numInstances * numberOfBones * 3 * sizeof(glm::vec4);
    size_t bufferMatrixSize = numInstances * numberOfBones * sizeof(glm::mat4);

    mSphereWorldPosMatrices.clear();
    mSphereWorldPosMatrices.resize(numInstances);

    mSpherePerInstanceAnimData.clear();
    mSpherePerInstanceAnimData.resize(numInstances);

    for (size_t i = 0; i < instanceIds.size(); ++i) {
      InstanceSettings instSettings = mModelInstCamData.micAssimpInstances.at(instanceIds.at(i))->getInstanceSettings();

      PerInstanceAnimData animData{};
      animData.firstAnimClipNum = instSettings.isFirstAnimClipNr;
      animData.secondAnimClipNum = instSettings.isSecondAnimClipNr;
      animData.firstClipReplayTimestamp = instSettings.isFirstClipAnimPlayTimePos;
      animData.secondClipReplayTimestamp = instSettings.isSecondClipAnimPlayTimePos;
      animData.blendFactor = instSettings.isAnimBlendFactor;

      mSpherePerInstanceAnimData.at(i) = animData;

      mSphereWorldPosMatrices.at(i) = mModelInstCamData.micAssimpInstances.at(instanceIds.at(i))->getWorldTransformMatrix();
    }

    // we need to update descriptors after the upload if buffer size changed
    bool bufferResized = false;
    mRenderData.rdUploadToUBOTimer.start();
    bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSpherePerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mSpherePerInstanceAnimData);
    bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSphereModelRootMatrixBuffers.at(mRenderData.currentFrame), mSphereWorldPosMatrices);
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    // resize SSBO if needed
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereBoneMatrixBuffers.at(mRenderData.currentFrame), bufferMatrixSize);
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereTRSMatrixBuffers.at(mRenderData.currentFrame), trsMatrixSize);

    if (bufferResized) {
      VkHelper::updateDescriptorSets(mRenderData);
      VkHelper::updateSphereComputeDescriptorSets(mRenderData);
    }

    // in case data was changed
    model->updateBoundingSphereAdjustments(mRenderData);

    // record compute commands
    VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
      Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
      return false;
    }

    VkHelper::runBoundingSphereComputeShaders(mRenderData, model, numInstances, sphereModelOffset);
    sphereModelOffset += numberOfSpheres;
    mCollidingSphereCount += numberOfSpheres;

    if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
      return false;
    }

    // submit compute commands
    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

    result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
      return false;
    };

    // we must wait for the compute shaders to finish before we can read the bone data
    result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  if (mCollidingSphereCount > 0) {
    mRenderData.rdUploadToVBOTimer.start();
    VertexBuffer::uploadData(mRenderData, mRenderData.rdSphereVertexBuffers.at(mRenderData.currentFrame), mCollidingSphereMesh.vertices);
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }

  return true;
}

bool VkRenderer::createAllBoundingSpheres() {
  // count total number of spheres to calculate
  int totalSpheres = 0;
  for (const auto& model : mModelInstCamData.micModelList) {
    if (!model->hasAnimations()) {
      continue;
    }
    std::string modelName = model->getModelFileName();
    std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[modelName];

    size_t numberOfBones = model->getBoneList().size();
    size_t numInstances = instances.size();

    size_t numberOfSpheres = numInstances * numberOfBones;

    totalSpheres += numberOfSpheres;
  }

  // resize SSBO if needed
  bool bufferResized = false;
  bufferResized = ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdBoundingSphereBuffers.at(mRenderData.currentFrame), totalSpheres * sizeof(glm::vec4));

  if (bufferResized) {
    VkHelper::updateSphereComputeDescriptorSets(mRenderData);
  }

  int sphereModelOffset = 0;
  for (const auto& model : mModelInstCamData.micModelList) {
    if (!model->hasAnimations()) {
      continue;
    }
    std::string modelName = model->getModelFileName();
    std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[modelName];

    size_t numberOfBones = model->getBoneList().size();
    size_t numInstances = instances.size();

    size_t numberOfSpheres = numInstances * numberOfBones;
    size_t trsMatrixSize = numInstances * numberOfBones * 3 * sizeof(glm::vec4);
    size_t bufferMatrixSize = numInstances * numberOfBones * sizeof(glm::mat4);

    mSphereWorldPosMatrices.clear();
    mSphereWorldPosMatrices.resize(numInstances);

    mSpherePerInstanceAnimData.clear();
    mSpherePerInstanceAnimData.resize(numInstances);

    for (size_t i = 0; i < numInstances; ++i) {
      InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

      PerInstanceAnimData animData{};
      animData.firstAnimClipNum = instSettings.isFirstAnimClipNr;
      animData.secondAnimClipNum = instSettings.isSecondAnimClipNr;
      animData.firstClipReplayTimestamp = instSettings.isFirstClipAnimPlayTimePos;
      animData.secondClipReplayTimestamp = instSettings.isSecondClipAnimPlayTimePos;
      animData.blendFactor = instSettings.isAnimBlendFactor;

      mSpherePerInstanceAnimData.at(i) = animData;

      mSphereWorldPosMatrices.at(i) = instances.at(i)->getWorldTransformMatrix();
    }

    // we need to update descriptors after the upload if buffer size changed
    bool bufferResized = false;
    mRenderData.rdUploadToUBOTimer.start();
    bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSpherePerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mSpherePerInstanceAnimData);
    bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSphereModelRootMatrixBuffers.at(mRenderData.currentFrame), mSphereWorldPosMatrices);
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    // resize SSBO if needed
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereBoneMatrixBuffers.at(mRenderData.currentFrame), bufferMatrixSize);
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdSphereTRSMatrixBuffers.at(mRenderData.currentFrame), trsMatrixSize);

    if (bufferResized) {
      VkHelper::updateDescriptorSets(mRenderData);
      VkHelper::updateSphereComputeDescriptorSets(mRenderData);
    }

    // in case data was changed
    model->updateBoundingSphereAdjustments(mRenderData);

    // record compute commands
    VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
      Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
      return false;
    }

    VkHelper::runBoundingSphereComputeShaders(mRenderData, model, numInstances, sphereModelOffset);
    sphereModelOffset += numberOfSpheres;
    mCollidingSphereCount += numberOfSpheres;

    if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
      return false;
    }

    // submit compute commands
    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

    result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
      return false;
    };

    // we must wait for the compute shaders to finish before we can read the bone data
    result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  if (mCollidingSphereCount > 0) {
    mRenderData.rdUploadToVBOTimer.start();
    VertexBuffer::uploadData(mRenderData, mRenderData.rdSphereVertexBuffers.at(mRenderData.currentFrame), mSphereMesh.vertices);
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }

  return true;
}

/* Based on:
  https://www.saschawillems.de/blog/2017/12/30/new-vulkan-example-cascaded-shadow-mapping/
*/
void VkRenderer::updateShadowMapCascades() {
  std::vector<float> splits;
  splits.resize(mRenderData.SHADOW_MAP_LAYERS);

  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);

  float minZ = mRenderData.rdNearPlane;
  float maxZ = mRenderData.rdFarPlane;
  float clipRange = mRenderData.rdFarPlane - mRenderData.rdNearPlane;

  float ratio = maxZ / minZ;

  // split points
  for (int i = 0; i < mRenderData.SHADOW_MAP_LAYERS; ++i) {
    float splitRangeEnd = (i + 1) / static_cast<float>(mRenderData.SHADOW_MAP_LAYERS);
    float log = minZ * std::pow(ratio, splitRangeEnd);
    float uniform = minZ + clipRange * splitRangeEnd;
    float dist = mRenderData.rdShadowMapSplitLambda * (log - uniform) + uniform;
    splits[i] = (dist - minZ) / clipRange;
  }

  // ortho projection matrix for each split
  float lastSplitDist = 0.0f;
  for (int i = 0; i < mRenderData.SHADOW_MAP_LAYERS; ++i) {
    float splitDist = splits[i];

    std::array<glm::vec3, 8> frustumCornerPoints = {
      glm::vec3(-1.0f,  1.0f, 0.0f),
      glm::vec3( 1.0f,  1.0f, 0.0f),
      glm::vec3( 1.0f, -1.0f, 0.0f),
      glm::vec3(-1.0f, -1.0f, 0.0f),
      glm::vec3(-1.0f,  1.0f, 1.0f),
      glm::vec3( 1.0f,  1.0f, 1.0f),
      glm::vec3( 1.0f, -1.0f, 1.0f),
      glm::vec3(-1.0f, -1.0f, 1.0f),
    };

    // project frustum corners into world space
    glm::mat4 invViewProj = mRenderUploadData.inverseViewMatrix.at(0) * mRenderUploadData.inverseProjectionMatrix.at(0);
    for (size_t j = 0; j < frustumCornerPoints.size(); ++j) {
      glm::vec4 point = invViewProj * glm::vec4(frustumCornerPoints.at(j), 1.0f);
      frustumCornerPoints.at(j) = point / point.w;
    }

    // adjust world points to calculated split size
    for (size_t j = 0; j < frustumCornerPoints.size() / 2; ++j) {
      glm::vec3 dist = frustumCornerPoints[j + 4] - frustumCornerPoints[j];
      frustumCornerPoints[j + 4] = frustumCornerPoints[j] + (dist * splitDist);
      frustumCornerPoints[j] = frustumCornerPoints[j] + (dist * lastSplitDist);
    }

    // get frustum center
    glm::vec3 frustumCenter = glm::vec3(0.0f);
    for (size_t j = 0; j < frustumCornerPoints.size(); ++j) {
      frustumCenter += frustumCornerPoints[j];
    }
    frustumCenter /= static_cast<float>(frustumCornerPoints.size());

    // calculate bounding sphere radius for every frustum split
    float radius = 0.0f;
    for (size_t j = 0; j < frustumCornerPoints.size(); ++j) {
      float dist = glm::length(frustumCornerPoints[j] - frustumCenter);
      radius = glm::max(radius, dist);
    }

    radius = std::ceil(radius * 16.0f) / 16.0f;

    glm::vec3 maxExtents = glm::vec3(radius);
    glm::vec3 minExtents = -maxExtents;

    glm::vec3 lightDir = glm::normalize(-mRenderUploadData.lightPos);
    glm::mat4 lightViewMat = glm::lookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightOrthoMat = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, minExtents.z / 4.0f, maxExtents.z - minExtents.z);

    mRenderData.rdShadowMapCascadeData.cascades.at(i).splitDepth.x = -(mRenderData.rdNearPlane + splitDist * clipRange);
    mRenderData.rdShadowMapCascadeData.cascades.at(i).lightViewProjectionMat = lightOrthoMat * mVulkanViewCorrectionMatrix * lightViewMat;

    lastSplitDist = splits[i];
  }
}

void VkRenderer::resetLightData() {
  mModelInstCamData.micDynLights.erase(mModelInstCamData.micDynLights.begin(), mModelInstCamData.micDynLights.end());

  // add null light, similar to the other objects
  addDynLight();

  mModelInstCamData.micSelectedDynLight = 0;

  mRenderData.rdEnableLightDebug = false;
}

void VkRenderer::drawScene(bool shadowMapPass, bool dynamcicShadows, uint32_t dynLight) {
  // draw levels first
  uint32_t levelPosOffset = 0;
  for (const auto& level : mModelInstCamData.micLevels) {
    if (level->getTriangleCount() == 0) {
      continue;
    }

    if (shadowMapPass) {
      if (dynamcicShadows) {
        vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
          mRenderData.rdDynamicShadowMapAssimpLevelPipeline);
        vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynLightShadowMapConstantDepthBias, 0.0f, mRenderData.rdDynLightShadowMapSlopeDepthBias);
      } else {
        vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
            mRenderData.rdShadowMapAssimpLevelPipeline);
        vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapConstantDepthBias, 0.0f, mRenderData.rdShadowMapSlopeDepthBias);
      }
    } else {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdAssimpLevelPipeline);
    }

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdAssimpLevelPipelineLayout, 1, 1,
      &mRenderData.rdAssimpLevelDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    mRenderData.rdUploadToUBOTimer.start();
    mRenderData.rdModelData.pkWorldPosOffset = levelPosOffset;
    mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
    vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpLevelPipelineLayout,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);

    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();
    level->draw(mRenderData);

    ++levelPosOffset;
  }

  // draw instances second
  mWorldPosOffset = 0;
  uint32_t skinMatOffset = 0;
  for (const auto& model : mModelInstCamData.micModelList) {
    size_t numberOfInstances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].size();
    if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

      // animated models
      if (model->hasAnimations() && !model->getBoneList().empty()) {
        size_t numberOfBones = model->getBoneList().size();

        // draw all meshes without morph anims first
        if (shadowMapPass) {
          if (dynamcicShadows) {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
            mRenderData.rdDynamicShadowMapAssimpSkinningPipeline);
            vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynLightShadowMapConstantDepthBias, 0.0f, mRenderData.rdDynLightShadowMapSlopeDepthBias);
          } else {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
            mRenderData.rdShadowMapAssimpSkinningPipeline);
            vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapConstantDepthBias, 0.0f, mRenderData.rdShadowMapSlopeDepthBias);
          }

          vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
            mRenderData.rdAssimpSkinningPipelineLayout, 1, 1,
            &mRenderData.rdAssimpSkinningDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
        } else {
          if (mMousePick && mRenderData.rdApplicationMode == appMode::edit) {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSkinningSelectionPipeline);

            vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSkinningSelectionPipelineLayout, 1, 1,
             &mRenderData.rdAssimpSkinningSelectionDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
          } else {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSkinningPipeline);

            vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSkinningPipelineLayout, 1, 1,
              &mRenderData.rdAssimpSkinningDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
          }
        }

        mRenderData.rdUploadToUBOTimer.start();
        mRenderData.rdModelData.pkModelStride = numberOfBones;
        mRenderData.rdModelData.pkWorldPosOffset = mWorldPosOffset;
        mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
        mRenderData.rdModelData.pkSkinMatOffset = skinMatOffset;
        if (mMousePick && mRenderData.rdApplicationMode == appMode::edit) {
          vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSkinningSelectionPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
        } else {
          vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSkinningPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
        }
        mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

        model->drawInstancedNoMorphAnims(mRenderData, numberOfInstances, mMousePick && !shadowMapPass);

        // and if the model has morph anims, draw them in a separate pass 
        if (model->hasAnimMeshes()) {
          if (shadowMapPass) {
            if (dynamcicShadows) {
              vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdDynamicShadowMapAssimpSkinningMorphPipeline);
              vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynLightShadowMapConstantDepthBias, 0.0f, mRenderData.rdDynLightShadowMapSlopeDepthBias);
            } else {
              vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdShadowMapAssimpSkinningMorphPipeline);
              vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapConstantDepthBias, 0.0f, mRenderData.rdShadowMapSlopeDepthBias);
            }

            vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSkinningMorphPipelineLayout, 1, 1,
              &mRenderData.rdAssimpSkinningMorphDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

          } else {
            if (mMousePick && mRenderData.rdApplicationMode == appMode::edit) {
              vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
                mRenderData.rdAssimpSkinningMorphSelectionPipeline);

              vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
                mRenderData.rdAssimpSkinningMorphSelectionPipelineLayout, 1, 1,
                &mRenderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
            } else {
              vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
                mRenderData.rdAssimpSkinningMorphPipeline);

              vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
                mRenderData.rdAssimpSkinningMorphPipelineLayout, 1, 1,
                &mRenderData.rdAssimpSkinningMorphDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
            }
          }

          mRenderData.rdUploadToUBOTimer.start();
          mRenderData.rdModelData.pkModelStride = numberOfBones;
          mRenderData.rdModelData.pkWorldPosOffset = mWorldPosOffset;
          mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
          mRenderData.rdModelData.pkSkinMatOffset = skinMatOffset;
          if (mMousePick && mRenderData.rdApplicationMode == appMode::edit) {
            vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSkinningMorphSelectionPipelineLayout,
              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
          } else {
            vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSkinningMorphPipelineLayout,
              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
          }
          mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

          model->drawInstancedMorphAnims(mRenderData, numberOfInstances, mMousePick && !shadowMapPass);
        }

        mWorldPosOffset += numberOfInstances;
        skinMatOffset += numberOfInstances * numberOfBones;
      } else {
        // non-animated models
        if (shadowMapPass) {
          if (dynamcicShadows) {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdDynamicShadowMapAssimpPipeline);
            vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynLightShadowMapConstantDepthBias, 0.0f, mRenderData.rdDynLightShadowMapSlopeDepthBias);
          } else {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdShadowMapAssimpPipeline);
            vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapConstantDepthBias, 0.0f, mRenderData.rdShadowMapSlopeDepthBias);
          }

          vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
            mRenderData.rdAssimpPipelineLayout, 1, 1, &mRenderData.rdAssimpDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

        } else {
          if (mMousePick) {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdAssimpSelectionPipeline);

            vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpSelectionPipelineLayout, 1, 1, &mRenderData.rdAssimpSelectionDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
          } else {
            vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdAssimpPipeline);

            vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
              mRenderData.rdAssimpPipelineLayout, 1, 1, &mRenderData.rdAssimpDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
          }
        }

        mRenderData.rdUploadToUBOTimer.start();
        mRenderData.rdModelData.pkWorldPosOffset = mWorldPosOffset;
        mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
        if (mMousePick) {
          vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSelectionPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
        } else {
          vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
        }
        mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

        model->drawInstanced(mRenderData, numberOfInstances, mMousePick && !shadowMapPass);

        mWorldPosOffset += numberOfInstances;
      }
    }
  }
}

bool VkRenderer::drawXRControllers(bool shadowMapPass, bool dynamcicShadows, uint32_t dynLight) {
  // draw VR controllers
  if (shadowMapPass) {
    if (dynamcicShadows) {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdDynamicShadowMapVRControllerPipeline);
      vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynLightShadowMapConstantDepthBias, 0.0f, mRenderData.rdDynLightShadowMapSlopeDepthBias);
    } else {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
          mRenderData.rdShadowMapVRControllerPipeline);
      vkCmdSetDepthBias(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapConstantDepthBias, 0.0f, mRenderData.rdShadowMapSlopeDepthBias);
    }
  } else {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdVRControllerPipeline);
  }

  vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
    mRenderData.rdVRControllerPipelineLayout, 1, 1, &mRenderData.rdVRControllerDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

  mRenderData.rdUploadToUBOTimer.start();
  mRenderData.rdModelData.pkWorldPosOffset = 0;
  mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
  vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdVRControllerPipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
  mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

  mRHandVRControllerModel->drawXRControllers(mRenderData);

  mRenderData.rdUploadToUBOTimer.start();
  mRenderData.rdModelData.pkWorldPosOffset = 1;
  mRenderData.rdModelData.pShadowMapLayerIndex = dynLight;
  vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdVRControllerPipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
  mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

  mLHandVRControllerModel->drawXRControllers(mRenderData);

  return true;
}

bool VkRenderer::initDraw(float deltaTime) {
  if (!mApplicationRunning) {
    return false;
  }

  // no update on zero diff
  if (deltaTime == 0.0f) {
    return true;
  }

  if (mRenderData.rdEnableTimeOfDay) {
    mRenderData.rdTimeOfDay += deltaTime * mRenderData.rdTimeScaleFactor;
    if (mRenderData.rdTimeOfDay > mRenderData.rdLengthOfDay) {
      mRenderData.rdTimeOfDay -= mRenderData.rdLengthOfDay;
    }
  }

  mRenderData.rdFrameTime = mRenderData.rdFrameTimer.stop();
  mRenderData.rdFrameTimer.start();

  // reset timers and other values
  mRenderData.rdMatricesSize = 0;
  mRenderData.rdUploadToUBOTime = 0.0f;
  mRenderData.rdUploadToVBOTime = 0.0f;
  mRenderData.rdDownloadFromUBOTime = 0.0f;
  mRenderData.rdMatrixGenerateTime = 0.0f;
  mRenderData.rdUIGenerateTime = 0.0f;
  mRenderData.rdNumberOfCollisions = 0;
  mRenderData.rdCollisionDebugDrawTime = 0.0f;
  mRenderData.rdCollisionCheckTime = 0.0f;
  mRenderData.rdBehaviorTime = 0.0f;
  mRenderData.rdInteractionTime = 0.0f;
  mRenderData.rdNumberOfInteractionCandidates = 0;
  mRenderData.rdInteractWithInstanceId = 0;
  mRenderData.rdFaceAnimTime = 0.0f;
  mRenderData.rdNumberOfCollidingTriangles = 0;
  mRenderData.rdNumberOfCollidingGroundTriangles = 0;
  mRenderData.rdLevelCollisionTime = 0.0f;
  mRenderData.rdIKTime = 0.0f;
  mRenderData.rdPathFindingTime = 0.0f;
  mRenderData.rdLevelGroundNeighborUpdateTime = 0.0f;

  return true;
}

bool VkRenderer::acquireDesktopImage() {
  // wait for both fences before getting the new framebuffer image
  std::vector<VkFence> waitFences = {
    mRenderData.rdComputeFences.at(mRenderData.currentFrame),
    mRenderData.rdRenderFences.at(mRenderData.currentFrame)
  };

  VkResult result = vkWaitForFences(mRenderData.rdVkbDevice.device,
    static_cast<uint32_t>(waitFences.size()), waitFences.data(), VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: waiting for fences failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = vkAcquireNextImageKHR(mRenderData.rdVkbDevice.device,
      mRenderData.rdVkbSwapchain.swapchain,
      UINT64_MAX,
      mRenderData.rdPresentSemaphores.at(mRenderData.currentFrame),
      VK_NULL_HANDLE,
      &mImageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return recreateSwapchain();
  } else {
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      Logger::log(1, "%s error: failed to acquire swapchain image. Error is '%i'\n", __FUNCTION__, result);
      return false;
    }
  }

  return true;
}

bool VkRenderer::updateLevelAndModels(float deltaTime) {
  VkResult result = VK_ERROR_UNKNOWN;

  // calculate the size of the lookup matrix buffer over all animated instances
  size_t boneMatrixBufferSize = 0;
  size_t lookupBufferSize = 0;
  for (const auto& model : mModelInstCamData.micModelList) {
    size_t numberOfInstances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].size();
    if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

      // animated models
      if (model->hasAnimations() && !model->getBoneList().empty()) {
        size_t numberOfBones = model->getBoneList().size();

        // buffer size must always be a multiple of "local_size_y" instances to avoid undefined behavior
        boneMatrixBufferSize += numberOfBones * ((numberOfInstances - 1) / 32 + 1) * 32;
        lookupBufferSize += numberOfInstances;
      }
    }
  }

  // clear and resize world pos matrices
  mWorldPosMatrices.clear();
  mWorldPosMatrices.resize(mModelInstCamData.micAssimpInstances.size() + mModelInstCamData.micDynLights.size());
  mSelectedInstance.clear();
  mSelectedInstance.resize(mModelInstCamData.micAssimpInstances.size() + mModelInstCamData.micDynLights.size());

  mPerInstanceAnimData.clear();
  mPerInstanceAnimData.resize(lookupBufferSize);
  mFaceAnimPerInstanceData.clear();
  mFaceAnimPerInstanceData.resize(mModelInstCamData.micAssimpInstances.size());

  // save the selected instance for color highlight
  std::shared_ptr<AssimpInstance> currentSelectedInstance = nullptr;
  if (mRenderData.rdApplicationMode == appMode::edit) {
    if (mRenderData.rdHighlightSelectedInstance) {
      currentSelectedInstance = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance);
      mRenderData.rdSelectedInstanceHighlightValue += deltaTime * 4.0f;
      if (mRenderData.rdSelectedInstanceHighlightValue > 2.0f) {
        mRenderData.rdSelectedInstanceHighlightValue = 0.1f;
      }
    }
  }

  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = cam->getCameraSettings();

  int firstPersonCamWorldPos = -1;
  int firstPersonCamBoneMatrixPos = -1;

  // we need to track the presence of animated models too
  bool animatedModelLoaded = false;

  size_t instanceToStore = 0;
  size_t animatedInstancesToStore = 0;
  size_t animatedInstancesLookupToStore = 0;

  mLevelGroundNeighborsMesh->vertices.clear();
  mInstancePathMesh->vertices.clear();

  mOctree->clear();

  for (const auto& model : mModelInstCamData.micModelList) {
    size_t numberOfInstances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].size();
    std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];
    if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

      // animated models
      if (model->hasAnimations() && !model->getBoneList().empty()) {
        size_t numberOfBones = model->getBoneList().size();
        ModelSettings modSettings = model->getModelSettings();

        animatedModelLoaded = true;

        mRenderData.rdMatrixGenerateTimer.start();

        for (unsigned int i = 0; i < numberOfInstances; ++i) {
          InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

          // animations
          PerInstanceAnimData animData{};
          animData.firstAnimClipNum = instSettings.isFirstAnimClipNr;
          animData.secondAnimClipNum = instSettings.isSecondAnimClipNr;
          animData.firstClipReplayTimestamp = instSettings.isFirstClipAnimPlayTimePos;
          animData.secondClipReplayTimestamp = instSettings.isSecondClipAnimPlayTimePos;
          animData.blendFactor = instSettings.isAnimBlendFactor;

          if (model->hasHeadMovementAnimationsMapped()) {
            if (instSettings.isHeadLeftRightMove > 0.0f) {
              animData.headLeftRightAnimClipNum = modSettings.msHeadMoveClipMappings[headMoveDirection::left];
            } else {
              animData.headLeftRightAnimClipNum = modSettings.msHeadMoveClipMappings[headMoveDirection::right];
            }
            if (instSettings.isHeadUpDownMove > 0.0f) {
              animData.headUpDownAnimClipNum = modSettings.msHeadMoveClipMappings[headMoveDirection::up];
            } else {
              animData.headUpDownAnimClipNum = modSettings.msHeadMoveClipMappings[headMoveDirection::down];
            }
            animData.headLeftRightReplayTimestamp = std::fabs(instSettings.isHeadLeftRightMove) * model->getMaxClipDuration();
            animData.headUpDownReplayTimestamp = std::fabs(instSettings.isHeadUpDownMove) * model->getMaxClipDuration();
          }

          mPerInstanceAnimData.at(animatedInstancesLookupToStore + i) = animData;

          if (mRenderData.rdApplicationMode == appMode::edit) {
            if (currentSelectedInstance == instances.at(i)) {
              mSelectedInstance.at(instanceToStore + i).x = mRenderData.rdSelectedInstanceHighlightValue;
            } else {
              mSelectedInstance.at(instanceToStore + i).x = 1.0f;
            }

            if (mMousePick) {
              mSelectedInstance.at(instanceToStore + i).y = static_cast<float>(instSettings.isInstanceIndexPosition);
            }
          } else {
            mSelectedInstance.at(instanceToStore + i).x = 1.0f;
          }

          if (camSettings.csCamType == cameraType::firstPerson && cam->getInstanceToFollow() &&
            instSettings.isInstanceIndexPosition == cam->getInstanceToFollow()->getInstanceIndexPosition()) {
            firstPersonCamWorldPos = instanceToStore + i;
            firstPersonCamBoneMatrixPos = animatedInstancesToStore + i * numberOfBones;
          }

          instances.at(i)->updateAnimation(deltaTime);

          // get AABB and calculate 3D boundaries
          AABB instanceAABB = model->getAABB(instSettings);

          glm::vec3 position = instanceAABB.getMinPos();
          glm::vec3 size = glm::vec3(std::fabs(instanceAABB.getMaxPos().x - instanceAABB.getMinPos().x),
                                     std::fabs(instanceAABB.getMaxPos().y - instanceAABB.getMinPos().y),
                                     std::fabs(instanceAABB.getMaxPos().z - instanceAABB.getMinPos().z));

          BoundingBox3D box{position, size};
          instances.at(i)->setBoundingBox(box);

          // add instance to octree
          mOctree->add(instSettings.isInstanceIndexPosition);

          mRenderData.rdFaceAnimTimer.start();

          glm::vec4 morphData = glm::vec4(0.0f);
          if (instSettings.isFaceAnimType != faceAnimation::none)  {
            morphData.x = instSettings.isFaceAnimWeight;
            morphData.y = static_cast<int>(instSettings.isFaceAnimType) - 1;
            morphData.z = model->getAnimMeshVertexSize();
          }
          mFaceAnimPerInstanceData.at(animatedInstancesLookupToStore + i) = morphData;

          mRenderData.rdFaceAnimTime += mRenderData.rdFaceAnimTimer.stop();

          // gravity and ground collisions
          mRenderData.rdLevelCollisionTimer.start();

          // extend the AABB a bit below the feet to allow a better ground collision handling
          glm::vec3 instBoxPos = position - mRenderData.rdLevelCollisionAABBExtension;
          glm::vec3 instBoxSize = size + mRenderData.rdLevelCollisionAABBExtension;
          BoundingBox3D instanceBox{instBoxPos, instBoxSize};

          std::vector<MeshTriangle> collidingTriangles = mTriangleOctree->query(instanceBox);
          instances.at(i)->setCollidingTriangles(collidingTriangles);

          // set state to "instance on ground" if gravity is disabled
          bool instanceOnGround = true;
          if (mRenderData.rdEnableSimpleGravity) {
            glm::vec3 gravity = glm::vec3(0.0f, GRAVITY_CONSTANT * deltaTime, 0.0f);
            glm::vec3 footPoint = instSettings.isWorldPosition;

            instanceOnGround = false;
            for (const auto& tri : collidingTriangles) {
              // check for slope
              bool isWalkable = false;
              if (glm::dot(tri.normal, glm::vec3(0.0f, 1.0f, 0.0f)) >= std::cos(glm::radians(mRenderData.rdMaxLevelGroundSlopeAngle))) {
                isWalkable = true;
              }

              if (isWalkable) {
                std::optional<glm::vec3> result =
                  Tools::rayTriangleIntersection(instSettings.isWorldPosition - gravity, glm::vec3(0.0f, 1.0f, 0.0f), tri);
                if (result.has_value()) {
                  footPoint = result.value();
                  instances.at(i)->setWorldPosition(footPoint);
                  instanceOnGround = true;
                }
              }
            }
          }
          instances.at(i)->setInstanceOnGround(instanceOnGround);
          instances.at(i)->applyGravity(deltaTime);
          mRenderData.rdLevelCollisionTime += mRenderData.rdLevelCollisionTimer.stop();

          // update instance speed and position
          instances.at(i)->updateInstanceSpeed(deltaTime);
          instances.at(i)->updateInstancePosition(deltaTime);

          mWorldPosMatrices.at(instanceToStore + i) = instances.at(i)->getWorldTransformMatrix();

          // path update
          if (mRenderData.rdEnableNavigation && instSettings.isNavigationEnabled) {
            mRenderData.rdPathFindingTimer.start();
            int pathTargetInstance = instSettings.isPathTargetInstance;

            // invalid target, reset
            if (pathTargetInstance >= mModelInstCamData.micAssimpInstances.size()) {
              pathTargetInstance = -1;
              instances.at(i)->setPathTargetInstanceId(pathTargetInstance);
            }

            int pathTargetInstanceTriIndex = -1;
            glm::vec3 pathTargetWorldPos = glm::vec3(0.0f);
            if (pathTargetInstance != -1) {
              // target instance is always valid here
              std::shared_ptr<AssimpInstance> targetInstance = mModelInstCamData.micAssimpInstances.at(pathTargetInstance);
              pathTargetInstanceTriIndex = targetInstance->getCurrentGroundTriangleIndex();
              pathTargetWorldPos = targetInstance->getWorldPosition();
            }

            // do a path update only if both start and end triangle indices are valid and we or target changed its triangle
            if ((instSettings.isCurrentGroundTriangleIndex > -1 && pathTargetInstanceTriIndex > -1) &&
              (instSettings.isCurrentGroundTriangleIndex != instSettings.isPathStartTriangleIndex ||
              pathTargetInstanceTriIndex != instSettings.isPathTargetTriangleIndex)) {
              instances.at(i)->setPathStartTriIndex(instSettings.isCurrentGroundTriangleIndex);
            instances.at(i)->setPathTargetTriIndex(pathTargetInstanceTriIndex);

            std::vector<int> pathToTarget = mPathFinder.findPath(instSettings.isCurrentGroundTriangleIndex, pathTargetInstanceTriIndex);

            // disable navigation if target is unreachable
            if (pathToTarget.empty()) {
              instances.at(i)->setNavigationEnabled(false);
              instances.at(i)->setPathTargetInstanceId(-1);
            } else {
              instances.at(i)->setPathToTarget(pathToTarget);
            }
              }

              std::vector<int> pathToTarget = instances.at(i)->getPathToTarget();

              // remove first and last elements, they are the target centers of start and target triangles
              if (pathToTarget.size() > 1) {
                pathToTarget.pop_back();
              }
              if (!pathToTarget.empty()) {
                pathToTarget.erase(pathToTarget.begin());
              }

              // navigate to target
              if (!pathToTarget.empty()) {
                /* navigate to next triangle, not the one we may stand on (start triangle)*/
                int nextTarget = pathToTarget.at(0);
                glm::vec3 destPos = mPathFinder.getTriangleCenter(nextTarget);
                instances.at(i)->rotateTo(destPos, deltaTime);
              } else {
                // empty path means we have only the target itself left
                instances.at(i)->rotateTo(pathTargetWorldPos, deltaTime);
              }

              if (mRenderData.rdDrawInstancePaths && pathTargetInstance > -1) {
                glm::vec3 pathColor = glm::vec3(0.4f, 1.0f, 0.4f);
                glm::vec3 pathYOffset = glm::vec3(0.0f, 1.0f, 0.0f);

                VkSimpleVertex vert;
                vert.color = pathColor;

                vert.position = instSettings.isWorldPosition + pathYOffset;
                mInstancePathMesh->vertices.emplace_back(vert);

                if (!pathToTarget.empty()) {
                  vert.position = mPathFinder.getTriangleCenter(pathToTarget.at(0)) + pathYOffset;
                  mInstancePathMesh->vertices.emplace_back(vert);

                  std::shared_ptr<VkSimpleMesh> pathMesh =
                  mPathFinder.getAsLineMesh(pathToTarget, pathColor, pathYOffset);

                  mInstancePathMesh->vertices.insert(mInstancePathMesh->vertices.end(), pathMesh->vertices.begin(), pathMesh->vertices.end());

                  vert.position = mPathFinder.getTriangleCenter(pathToTarget.at(pathToTarget.size() - 1)) + pathYOffset;
                  mInstancePathMesh->vertices.emplace_back(vert);
                }

                vert.position = pathTargetWorldPos + pathYOffset;
                mInstancePathMesh->vertices.emplace_back(vert);
              }
              mRenderData.rdPathFindingTime += mRenderData.rdPathFindingTimer.stop();
          }

          // neighbor triangles
          mRenderData.rdLevelGroundNeighborUpdateTimer.start();
          int groundTri = instSettings.isCurrentGroundTriangleIndex;
          if (groundTri > -1) {
            std::vector<int> neighborIndices = mPathFinder.getGroundTriangleNeighbors(groundTri);
            instances.at(i)->setNeighborGroundTriangleIndices(neighborIndices);

            std::shared_ptr<VkSimpleMesh> neighborMesh =
            mPathFinder.getAsTriangleMesh(neighborIndices, glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, 0.8f), glm::vec3(0.0f, 0.01f, 0.0f));
            mLevelGroundNeighborsMesh->vertices.insert(mLevelGroundNeighborsMesh->vertices.end(),
              neighborMesh->vertices.begin(), neighborMesh->vertices.end());
          }
          mRenderData.rdLevelGroundNeighborUpdateTime += mRenderData.rdLevelGroundNeighborUpdateTimer.stop();
        }

        size_t trsMatrixSize = numberOfBones * numberOfInstances * sizeof(glm::mat4);

        mRenderData.rdMatrixGenerateTime += mRenderData.rdMatrixGenerateTimer.stop();
        mRenderData.rdMatricesSize += trsMatrixSize;

        instanceToStore += numberOfInstances;
        animatedInstancesToStore += numberOfInstances * numberOfBones;
        animatedInstancesLookupToStore += numberOfInstances;
      } else {
        // non-animated models
        mRenderData.rdMatrixGenerateTimer.start();

        for (unsigned int i = 0; i < numberOfInstances; ++i) {
          InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

          if (mRenderData.rdApplicationMode == appMode::edit) {
            if (currentSelectedInstance == instances.at(i)) {
              mSelectedInstance.at(instanceToStore + i).x = mRenderData.rdSelectedInstanceHighlightValue;
            } else {
              mSelectedInstance.at(instanceToStore + i).x = 1.0f;
            }

            if (mMousePick) {
              mSelectedInstance.at(instanceToStore + i).y = static_cast<float>(instSettings.isInstanceIndexPosition);
            }
          } else {
            mSelectedInstance.at(instanceToStore + i).x = 1.0f;
          }

          // get AABB and calculate 3D boundaries
          AABB instanceAABB = model->getAABB(instSettings);

          glm::vec3 position = instanceAABB.getMinPos();
          glm::vec3 size = glm::vec3(std::fabs(instanceAABB.getMaxPos().x - instanceAABB.getMinPos().x),
                                     std::fabs(instanceAABB.getMaxPos().y - instanceAABB.getMinPos().y),
                                     std::fabs(instanceAABB.getMaxPos().z - instanceAABB.getMinPos().z));

          BoundingBox3D box{position, size};
          instances.at(i)->setBoundingBox(box);

          // add instance to octree
          mOctree->add(instSettings.isInstanceIndexPosition);

          // gravity and ground collisions
          mRenderData.rdLevelCollisionTimer.start();

          // extend the AABB a bit below the feet to allow a better ground collision handling
          glm::vec3 instBoxPos = position - mRenderData.rdLevelCollisionAABBExtension;
          glm::vec3 instBoxSize = size + mRenderData.rdLevelCollisionAABBExtension;
          BoundingBox3D instanceBox{instBoxPos, instBoxSize};

          std::vector<MeshTriangle> collidingTriangles = mTriangleOctree->query(instanceBox);
          instances.at(i)->setCollidingTriangles(collidingTriangles);

          // set state to "instance on ground" if gravity is disabled
          bool instanceOnGround = true;
          if (mRenderData.rdEnableSimpleGravity) {
            glm::vec3 gravity = glm::vec3(0.0f, GRAVITY_CONSTANT * deltaTime, 0.0f);
            glm::vec3 footPoint = instSettings.isWorldPosition;

            instanceOnGround = false;
            for (const auto& tri : collidingTriangles) {
              // check for slope
              bool isWalkable = false;
              if (glm::dot(tri.normal, glm::vec3(0.0f, 1.0f, 0.0f)) >= std::cos(glm::radians(mRenderData.rdMaxLevelGroundSlopeAngle))) {
                isWalkable = true;
              }

              if (isWalkable) {
                std::optional<glm::vec3> result = Tools::rayTriangleIntersection(instSettings.isWorldPosition - gravity, glm::vec3(0.0f, 1.0f, 0.0f), tri);
                if (result.has_value()) {
                  footPoint = result.value();
                  instances.at(i)->setWorldPosition(footPoint);
                  instanceOnGround = true;
                }
              }
            }
          }
          instances.at(i)->setInstanceOnGround(instanceOnGround);
          instances.at(i)->applyGravity(deltaTime);
          mRenderData.rdLevelCollisionTime += mRenderData.rdLevelCollisionTimer.stop();

          instances.at(i)->updateInstancePosition(deltaTime);
          mWorldPosMatrices.at(instanceToStore + i) = instances.at(i)->getWorldTransformMatrix();
        }

        mRenderData.rdMatrixGenerateTime += mRenderData.rdMatrixGenerateTimer.stop();
        mRenderData.rdMatricesSize += numberOfInstances * sizeof(glm::mat4);

        instanceToStore += numberOfInstances;
      }

      // remove instances that fell out of the level boundaries
      for (size_t i = 0; i < numberOfInstances; ++i) {
        InstanceSettings instSettings = instances.at(i)->getInstanceSettings();

        if (instSettings.isWorldPosition.y < mRenderData.rdWorldStartPos.y - 50.0f) {
          int instanceId = instSettings.isInstanceIndexPosition;
          Logger::log(1, "%s warning: instance id %i fell out of level boundaries, deleting\n", __FUNCTION__, instanceId);
          deleteInstance(getInstanceById(instanceId));
          if (model->hasAnimations() && !model->getBoneList().empty()) {
            size_t numberOfBones = model->getBoneList().size();
            animatedInstancesToStore -= numberOfBones;
            animatedInstancesLookupToStore -= 1;
          }
          instanceToStore -=1;
        }
      }
    }
  }

  // dynamic lights are handled like normal instances
  // we also use light at position 0 as "no light selected"
  for (size_t i = 1; i < mModelInstCamData.micDynLights.size(); ++i) {
    DynamicLightSettings lightSettings = mModelInstCamData.micDynLights.at(i)->getDynLightSettings();
    mWorldPosMatrices.at(instanceToStore + i - 1) = mModelInstCamData.micDynLights.at(i)->getWorldTransformMatrix();

    if (mMousePick) {
      mSelectedInstance.at(instanceToStore + i - 1).y = static_cast<float>(lightSettings.dlsIndexPosition) + LIGHT_OBJECT_OFFSET;
    }
    mSelectedInstance.at(instanceToStore + i - 1).x = 1.0f;
  }

  // upload vertex data for instance paths and neighbor triangles
  mRenderData.rdUploadToVBOTimer.start();
  VertexBuffer::uploadData(mRenderData, mRenderData.rdInstancePathVertexBuffers.at(mRenderData.currentFrame), mInstancePathMesh->vertices);
  VertexBuffer::uploadData(mRenderData, mRenderData.rdGroundMeshNeighborVertexBuffers.at(mRenderData.currentFrame), mLevelGroundNeighborsMesh->vertices);
  mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();

  // we need to update descriptors after the upload if buffer size changed
  bool bufferResized = false;
  mRenderData.rdUploadToUBOTimer.start();
  bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdPerInstanceAnimDataBuffers.at(mRenderData.currentFrame), mPerInstanceAnimData);
  bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdSelectedInstanceBuffers.at(mRenderData.currentFrame), mSelectedInstance);
  bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdFaceAnimPerInstanceDataBuffers.at(mRenderData.currentFrame), mFaceAnimPerInstanceData);
  mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

  // resize SSBO if needed
  bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShaderTRSMatrixBuffers.at(mRenderData.currentFrame), boneMatrixBufferSize * 3 * sizeof(glm::vec4));
  bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame), boneMatrixBufferSize * sizeof(glm::mat4));

  if (bufferResized) {
    VkHelper::updateDescriptorSets(mRenderData);
    VkHelper::updateComputeDescriptorSets(mRenderData);
  }

  if (animatedModelLoaded) {
    // record compute commands
    result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }

    uint32_t computeShaderModelOffset = 0;
    uint32_t computeShaderInstanceOffset = 0;
    if (!CommandBuffer::reset(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame), 0)) {
      Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::beginSingleShot(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
      return false;
    }

    for (const auto& model : mModelInstCamData.micModelList) {
      size_t numberOfInstances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].size();
      if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

        // compute shader for animated models only
        if (model->hasAnimations() && !model->getBoneList().empty()) {
          size_t numberOfBones = model->getBoneList().size();

          VkHelper::runComputeShaders(mRenderData, model, numberOfInstances, computeShaderModelOffset, computeShaderInstanceOffset);

          computeShaderModelOffset += numberOfInstances * numberOfBones;
          computeShaderInstanceOffset += numberOfInstances;
        }
      }
    }

    if (!CommandBuffer::end(mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame))) {
      Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
      return false;
    }

    // submit compute commands
    VkSubmitInfo computeSubmitInfo{};
    computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    computeSubmitInfo.commandBufferCount = 1;
    computeSubmitInfo.pCommandBuffers = &mRenderData.rdComputeCommandBuffers.at(mRenderData.currentFrame);

    result = vkQueueSubmit(mRenderData.rdComputeQueue, 1, &computeSubmitInfo, mRenderData.rdComputeFences.at(mRenderData.currentFrame));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
      return false;
    };

    // we must wait for the compute shaders to finish before we can read the bone data
    result = vkWaitForFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdComputeFences.at(mRenderData.currentFrame), VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  // first person follow cam node
  if (camSettings.csCamType == cameraType::firstPerson && cam->getInstanceToFollow()) {
    std::shared_ptr<AssimpModel> model = cam->getInstanceToFollow()->getModel();
    size_t numberOfBones = model->getBoneList().size();
    if (numberOfBones > 0) {
      int selectedBone = camSettings.csFirstPersonBoneToFollow;

      glm::mat4 offsetMatrix = glm::translate(glm::mat4(1.0f), camSettings.csFirstPersonOffsets);

      // get the bone matrix of the selected bone from the SSBO
      mRenderData.rdDownloadFromUBOTimer.start();
      glm::mat4 boneMatrix = ShaderStorageBuffer::getSsboDataMat4(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame),
        firstPersonCamBoneMatrixPos + selectedBone);
      mRenderData.rdDownloadFromUBOTime += mRenderData.rdDownloadFromUBOTimer.stop();

      cam->setBoneMatrix(mWorldPosMatrices.at(firstPersonCamWorldPos) * boneMatrix * offsetMatrix *
        model->getInverseBoneOffsetMatrix(selectedBone));

      cam->setCameraSettings(camSettings);
    }
  }

  // inverse kinematics
  if (mRenderData.rdDrawIKDebugLines) {
    mIKFootPointMesh->vertices.clear();
  }

  if (mRenderData.rdEnableFeetIK && boneMatrixBufferSize > 0) {
    mRenderData.rdIKTimer.start();

    mRenderData.rdIKMatrices.clear();
    mRenderData.rdIKMatrices.resize(boneMatrixBufferSize);
    mRenderData.rdTRSData.clear();
    mRenderData.rdTRSData.resize(boneMatrixBufferSize);

    // read back all node positions for foot positions 
    mRenderData.rdDownloadFromUBOTimer.start();
    mRenderData.rdIKMatrices = ShaderStorageBuffer::getSsboDataMat4(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame), 0, boneMatrixBufferSize);
    mRenderData.rdTRSData = ShaderStorageBuffer::getSsboDataTRSMatrixData(mRenderData, mRenderData.rdShaderTRSMatrixBuffers.at(mRenderData.currentFrame), 0, boneMatrixBufferSize);
    mRenderData.rdDownloadFromUBOTime += mRenderData.rdDownloadFromUBOTimer.stop();

    // resize SSBO if needed
    bool bufferResized = false;
    bufferResized = ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdIKBoneMatrixBuffers.at(mRenderData.currentFrame), boneMatrixBufferSize * sizeof(glm::mat4));
    bufferResized |= ShaderStorageBuffer::checkForResize(mRenderData, mRenderData.rdIKTRSMatrixBuffers.at(mRenderData.currentFrame), boneMatrixBufferSize * 3 * sizeof(glm::vec4));

    if (bufferResized) {
      VkHelper::updateIKComputeDescriptorSets(mRenderData);
    }

    uint32_t ikModelOffset = 0;
    uint32_t ikAnimatedModelOffset = 0;
    for (const auto& model : mModelInstCamData.micModelList) {
      size_t numberOfInstances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()].size();
      std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstCamData.micAssimpInstancesPerModel[model->getModelFileName()];

      if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

        // animated models only
        if (model->hasAnimations() && !model->getBoneList().empty()) {

          size_t numberOfBones = model->getBoneList().size();
          ModelSettings modSettings = model->getModelSettings();

          for (int foot = 0; foot < modSettings.msFootIKChainPair.size(); ++foot) {
            mNewNodePositions.at(foot).clear();
          }

          // get positions of left and right foot from final world positions
          for (size_t i = 0; i < numberOfInstances; ++i) {
            InstanceSettings instSettings = instances.at(i)->getInstanceSettings();
            for (int foot = 0; foot < modSettings.msFootIKChainPair.size(); ++foot) {

              int nodeChainSize = modSettings.msFootIKChainNodes[foot].size();

              // no data (yet), continue
              if (nodeChainSize == 0) {
                continue;
              }

              // extract foot position from world position matrix
              int footNodeId = modSettings.msFootIKChainPair.at(foot).first;

              glm::vec3 footWorldPos = Tools::extractGlobalPosition(mWorldPosMatrices.at(ikModelOffset + i) *
                mRenderData.rdIKMatrices.at(ikAnimatedModelOffset + i * numberOfBones + footNodeId) *
                model->getInverseBoneOffsetMatrix(footNodeId));
              float footDistAboveGround = std::fabs(instSettings.isWorldPosition.y - footWorldPos.y);

              AABB instanceAABB = model->getAABB(instSettings);
              float instanceHeight = instanceAABB.getMaxPos().y - instanceAABB.getMinPos().y;
              float instanceHalfHeight = instanceHeight / 2.0f;

              VkSimpleVertex vert;
              glm::vec3 hitPoint = footWorldPos;
              for (const auto& tri : instSettings.isCollidingTriangles) {
                std::optional<glm::vec3> result{};

                // raycast downwards from middle height to detect ground below foot
                result = Tools::rayTriangleIntersection(footWorldPos +
                  glm::vec3(0.0f, instanceHalfHeight, 0.0f), glm::vec3(0.0f, -instanceHeight, 0.0f), tri);

                glm::mat3 normalRotMatrix = glm::mat3_cast(glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), tri.normal));

                if (result.has_value()) {
                  hitPoint = result.value() + glm::vec3(0.0f, footDistAboveGround, 0.0f);

                  // draw a cross onto the surface to mark the hit point
                  if (mRenderData.rdDrawIKDebugLines) {
                    vert.color = glm::vec3(1.0f);

                    vert.position = result.value() -
                    normalRotMatrix * glm::vec3(-0.5f, 0.0f, 0.0f) + glm::vec3(0.0f, 0.01f, 0.0f);
                    mIKFootPointMesh->vertices.push_back(vert);
                    vert.position = result.value() -
                    normalRotMatrix * glm::vec3(0.5f, 0.0f, 0.0f) + glm::vec3(0.0f, 0.01f, 0.0f);
                    mIKFootPointMesh->vertices.push_back(vert);
                    vert.position = result.value() -
                    normalRotMatrix * glm::vec3(0.0f, 0.0f, 0.5f) + glm::vec3(0.0f, 0.01f, 0.0f);
                    mIKFootPointMesh->vertices.push_back(vert);
                    vert.position = result.value() -
                    normalRotMatrix * glm::vec3(0.0f, 0.0f, -0.5f) + glm::vec3(0.0f, 0.01f, 0.0f);
                    mIKFootPointMesh->vertices.push_back(vert);
                  }
                }
              }

              // extract world positions of IK chain nodes
              mIKWorldPositionsToSolve.clear();

              for (int nodeId : modSettings.msFootIKChainNodes[foot]) {
                mIKWorldPositionsToSolve.emplace_back(mWorldPosMatrices.at(ikModelOffset + i) *
                  mRenderData.rdIKMatrices.at(ikAnimatedModelOffset + i * numberOfBones + nodeId) *
                  model->getInverseBoneOffsetMatrix(nodeId));
              }

              mIKSolvedPositions = mIKSolver.solveFARBIK(mIKWorldPositionsToSolve, hitPoint);
              mNewNodePositions.at(foot).insert(mNewNodePositions.at(foot).end(),
                mIKSolvedPositions.begin(), mIKSolvedPositions.end());

              // draw a cross for every node in the node chain to mark the final position
              if (mRenderData.rdDrawIKDebugLines) {
                for (const auto& position : mIKSolvedPositions) {
                  vert.color = glm::vec3(0.1f, 0.6f, 0.8f);

                  vert.position = position - glm::vec3(-0.5f, 0.0f, 0.0f);
                  mIKFootPointMesh->vertices.push_back(vert);
                  vert.position = position - glm::vec3(0.5f, 0.0f, 0.0f);
                  mIKFootPointMesh->vertices.push_back(vert);
                  vert.position = position - glm::vec3(0.0f, 0.0f, 0.5f);
                  mIKFootPointMesh->vertices.push_back(vert);
                  vert.position = position - glm::vec3(0.0f, 0.0f, -0.5f);
                  mIKFootPointMesh->vertices.push_back(vert);
                }
              }
            }
          }

          // we need to ROTATE the original bones to get the final position, starting with the root node
          for (int foot = 0; foot < modSettings.msFootIKChainPair.size(); ++foot) {
            int nodeChainSize = modSettings.msFootIKChainNodes[foot].size();

            // no data (yet), continue
            if (nodeChainSize == 0) {
              continue;
            }

            // we need to run the compute shader for every node of the IK chain
            for (int index = nodeChainSize - 1; index > 0; --index) {

              // apply the local rotation to the bones to have the same rotations as the IK result
              for (size_t i = 0; i < numberOfInstances; ++i) {
                int nodeId = modSettings.msFootIKChainNodes[foot].at(index);
                int nextNodeId = modSettings.msFootIKChainNodes[foot].at(index - 1);

                glm::vec3 position = Tools::extractGlobalPosition(mWorldPosMatrices.at(ikModelOffset + i) *
                  mRenderData.rdIKMatrices.at(ikAnimatedModelOffset + i * numberOfBones + nodeId) *
                  model->getInverseBoneOffsetMatrix(nodeId));
                glm::vec3 nextPosition = Tools::extractGlobalPosition(mWorldPosMatrices.at(ikModelOffset + i) *
                  mRenderData.rdIKMatrices.at(ikAnimatedModelOffset + i * numberOfBones + nextNodeId) *
                  model->getInverseBoneOffsetMatrix(nextNodeId));

                glm::vec3 toNext = glm::normalize(nextPosition - position);
                int newNodePosOffset = i * nodeChainSize + index;
                glm::vec3 toDesired =
                  glm::normalize(mNewNodePositions.at(foot).at(newNodePosOffset - 1) - mNewNodePositions.at(foot).at(newNodePosOffset));
                glm::quat nodeRotation = glm::rotation(toNext, toDesired);

                glm::quat rotation = Tools::extractGlobalRotation(mWorldPosMatrices.at(ikModelOffset + i) *
                  mRenderData.rdIKMatrices.at(ikAnimatedModelOffset + i * numberOfBones + nodeId) *
                  model->getInverseBoneOffsetMatrix(nodeId));
                glm::quat localRotation = rotation * nodeRotation * glm::conjugate(rotation);

                glm::quat currentRotation = mRenderData.rdTRSData.at(ikAnimatedModelOffset + i * numberOfBones + nodeId).rotation;
                glm::quat newRotation = currentRotation * localRotation;

                mRenderData.rdTRSData.at(ikAnimatedModelOffset + i * numberOfBones + nodeId).rotation = newRotation;
              }
              // un the compute shader to create the bone matrices 
              VkHelper::runIKComputeShaders(mRenderData, model, numberOfInstances, ikAnimatedModelOffset);
            }
          }

         ikAnimatedModelOffset += numberOfBones * numberOfInstances;
         ikModelOffset += numberOfInstances;
        } else {
          // just skip the world pos offset for any non-animated models inbetween
         ikModelOffset += numberOfInstances;
        }
      }
    }

    if (!mIKFootPointMesh->vertices.empty()) {
      mRenderData.rdUploadToVBOTimer.start();
      VertexBuffer::uploadData(mRenderData, mRenderData.rdIKLinesVertexBuffers.at(mRenderData.currentFrame), mIKFootPointMesh->vertices);
      mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
    }

    // update original bone matrix buffer for drawing
    mRenderData.rdUploadToUBOTimer.start();
    ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdShaderBoneMatrixBuffers.at(mRenderData.currentFrame), mRenderData.rdIKMatrices);
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    mRenderData.rdIKTime += mRenderData.rdIKTimer.stop();
  }

  // find interactions
  mRenderData.rdInteractionTimer.start();
  findInteractionInstances();
  mRenderData.rdInteractionTime += mRenderData.rdInteractionTimer.stop();

  // do collision checks after instances were updated and before drawing
  mRenderData.rdCollisionCheckTimer.start();
  checkForInstanceCollisions();
  checkForBorderCollisions();
  mRenderData.rdCollisionCheckTime += mRenderData.rdCollisionCheckTimer.stop();

  handleMovementKeys();

  // save mouse wheel (FOV/ortho scale) after 250ms of inactiviy
  if (mMouseWheelScrolling) {
    std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
    float scrollDelta = std::chrono::duration_cast<std::chrono::microseconds>(now - mMouseWheelLastScrollTime).count() / 1'000'000.0f;
    if (scrollDelta > 0.25f) {
      mModelInstCamData.micSettingsContainer->applyEditCameraSettings(cam, camSettings, mSavedCameraWheelSettings);

      setConfigDirtyFlag(true);

      mMouseWheelScrolling = false;
    }
  }


  // clear and resize world pos matrix for level data
  mLevelWorldPosMatrices.clear();
  mLevelWorldPosMatrices.resize(mModelInstCamData.micLevels.size());

  int levelToStore = 0;
  for (const auto& level : mModelInstCamData.micLevels) {
    if (level->getTriangleCount() == 0) {
      continue;
    }
    mLevelWorldPosMatrices.at(levelToStore) = level->getWorldTransformMatrix();
    ++levelToStore;
  }

  // we need to update descriptors after the upload if buffer size changed
  mRenderData.rdUploadToUBOTimer.start();
  bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdShaderLevelRootMatrixBuffers.at(mRenderData.currentFrame), mLevelWorldPosMatrices);
  mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

  if (bufferResized) {
    VkHelper::updateDescriptorSets(mRenderData);
    VkHelper::updateImageDescriptorSets(mRenderData);
    VkHelper::updateLevelDescriptorSets(mRenderData);
  }

  // behavior update
  mRenderData.rdBehviorTimer.start();
  mBehaviorManager->update(deltaTime);
  mRenderData.rdBehaviorTime += mRenderData.rdBehviorTimer.stop();

  // lines
  mLineIndexCount = 0;
  mLineMesh->vertices.clear();

  // level stuff
  if (mModelInstCamData.micLevels.size() > 1) {
    mRenderData.rdLevelCollisionTimer.start();
    checkForLevelCollisions();
    mRenderData.rdLevelCollisionTime += mRenderData.rdLevelCollisionTimer.stop();
  }

  // Coordinate arrows
  if (mRenderData.rdApplicationMode == appMode::edit) {
    bool drawArrows = false;
    glm::vec3 coordArrowRotation = glm::vec3(0.0f);
    glm::vec3 coordArrowPosition = glm::vec3(0.0f);

    if (mModelInstCamData.micSelectedInstance > 0) {
      InstanceSettings instSettings = mModelInstCamData.micAssimpInstances.at(mModelInstCamData.micSelectedInstance)->getInstanceSettings();
      drawArrows = true;
      coordArrowPosition = instSettings.isWorldPosition;
      coordArrowRotation = instSettings.isWorldRotation;
    }

    if (mModelInstCamData.micSelectedDynLight > 0) {
      DynamicLightSettings lightSettings = mModelInstCamData.micDynLights.at(mModelInstCamData.micSelectedDynLight)->getDynLightSettings();
      drawArrows = true;
      coordArrowPosition = lightSettings.dlsWorldPosition;
      coordArrowRotation = lightSettings.dlsWorldRotation;
    }

    if (drawArrows) {
      // draw coordiante arrows at origin of selected instance or light
      switch(mRenderData.rdInstanceEditMode) {
        case instanceEditMode::move:
          mCoordArrowsMesh = mCoordArrowsModel.getVertexData();
          break;
        case instanceEditMode::rotate:
          mCoordArrowsMesh = mRotationArrowsModel.getVertexData();
          break;
        case instanceEditMode::scale:
          mCoordArrowsMesh = mScaleArrowsModel.getVertexData();
          break;
      }

      mLineIndexCount += mCoordArrowsMesh.vertices.size();
      std::for_each(mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end(),
                    [=](auto &n) {
                      n.color /= 2.0f;
                      n.position = glm::quat(glm::radians(coordArrowRotation)) * n.position;
                      n.position += coordArrowPosition;
                    });
      mLineMesh->vertices.insert(mLineMesh->vertices.end(),
        mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end());
    }
  }

  // bounding spheres
  mCollidingSphereCount = 0;
  mSphereVertexCount = 0;

  switch (mRenderData.rdDrawBoundingSpheres) {
    case collisionDebugDraw::none:
      break;
    case collisionDebugDraw::colliding:
      if (!mModelInstCamData.micInstanceCollisions.empty()) {
        createCollidingBoundingSpheres();
        mSphereVertexCount = mCollidingSphereMesh.vertices.size();
      }
      break;
    case collisionDebugDraw::selected:
      // no bounding sphere collision will be done with this setting, so run the computer shaders just for the selected instance
      createSelectedBoundingSpheres();
      mSphereVertexCount = mSphereMesh.vertices.size();
      break;
    case collisionDebugDraw::all:
      createAllBoundingSpheres();
      mSphereVertexCount = mSphereMesh.vertices.size();
      break;
  }


  // debug for interaction
  mRenderData.rdInteractionTimer.start();
  createInteractionDebug();
  mRenderData.rdInteractionTime += mRenderData.rdInteractionTimer.stop();

  // reate AABB lines and bounding sphere of selected instance
  mRenderData.rdCollisionDebugDrawTimer.start();
  createInstanceCollisionDebug();
  mRenderData.rdCollisionDebugDrawTime += mRenderData.rdCollisionDebugDrawTimer.stop();

  // upload lines
  if (mLineIndexCount > 0) {
    mRenderData.rdUploadToVBOTimer.start();
    VertexBuffer::uploadData(mRenderData, mRenderData.rdLineVertexBuffers.at(mRenderData.currentFrame), mLineMesh->vertices);
    mRenderData.rdUploadToVBOTime += mRenderData.rdUploadToVBOTimer.stop();
  }

  // imGui overlay
  mRenderData.rdUIGenerateTimer.start();
  mUserInterface.createFrame(mRenderData);

  if (mRenderData.rdApplicationMode == appMode::edit) {
    mUserInterface.hideMouse(mMouseLock);
    mUserInterface.createSettingsWindow(mRenderData, mModelInstCamData, mModelInstCamCallbacks);
    mUserInterface.createPositionsWindow(mRenderData, mModelInstCamData, mModelInstCamCallbacks);
    mUserInterface.createDebugWindow(mRenderData);
  }

  // always draw the status bar
  mUserInterface.createStatusBar(mRenderData, mModelInstCamData, mModelInstCamCallbacks);
  mRenderData.rdUIGenerateTime += mRenderData.rdUIGenerateTimer.stop();

  // only loaded data right now
  if (mGraphEditor->getShowEditor()) {
    mGraphEditor->updateGraphNodes(deltaTime);
  }

  if (mRenderData.rdApplicationMode != appMode::view) {
    mGraphEditor->createNodeEditorWindow(mRenderData, mModelInstCamData);
  }

  return true;
}

bool VkRenderer::renderGraphics() {
  // start with graphics rendering
  VkResult result = vkResetFences(mRenderData.rdVkbDevice.device, 1, &mRenderData.rdRenderFences.at(mRenderData.currentFrame));
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  if (!CommandBuffer::reset(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0)) {
    Logger::log(1, "%s error: failed to reset command buffer\n", __FUNCTION__);
    return false;
  }

  if (!CommandBuffer::beginSingleShot(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame))) {
    Logger::log(1, "%s error: failed to begin command buffer\n", __FUNCTION__);
    return false;
  }

  VkClearValue colorClearValue;
  colorClearValue.color = { { 0.25f, 0.25f, 0.25f, 1.0f } };

  VkClearValue blackClearValue;
  blackClearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

  VkClearValue selectionClearValue;
  selectionClearValue.color = { { -1.0f } };

  VkClearValue ssaoClearValue;
  ssaoClearValue.color = { { 1.0f } };

  VkClearValue ssaoBlurClearValue;
  ssaoBlurClearValue.color = { { 1.0f } };

  // position stores depth (z) value in a
  VkClearValue depthImageClearValue;
  depthImageClearValue.color = { { 1.0f } };

  VkClearValue normalClearValue;
  normalClearValue.color = { { 0.0, 0.0, 0.0, 1.0f } };

  VkClearValue depthClearValue;
  depthClearValue.depthStencil.depth = 1.0f;

  // shadow Map
  // Dynamic light shadow pass
  if (mRenderData.rdNumDynamicLightsWithShadow > 0) {
    VkViewport dynLightShadowMapViewport{};
    dynLightShadowMapViewport.x = 0.0f;
    dynLightShadowMapViewport.y = 0.0f;
    dynLightShadowMapViewport.width = static_cast<float>(mRenderData.rdDynLightShadowMapSize.width);
    dynLightShadowMapViewport.height = static_cast<float>(mRenderData.rdDynLightShadowMapSize.height);
    dynLightShadowMapViewport.minDepth = 0.0f;
    dynLightShadowMapViewport.maxDepth = 1.0f;

    VkRect2D dynLightShadowMapScissor{};
    dynLightShadowMapScissor.offset = { 0, 0 };
    dynLightShadowMapScissor.extent = mRenderData.rdDynLightShadowMapSize;

    vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &dynLightShadowMapViewport);
    vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &dynLightShadowMapScissor);

    VkRect2D dynLightShadowMapenderArea = VkRect2D{VkOffset2D{}, mRenderData.rdDynLightShadowMapSize};

    // update shadow map data of dynamic lights
    vkCmdUpdateBuffer(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      mRenderData.rdShadowMapCascadeDataBuffers.at(mRenderData.currentFrame).buffer, 0, mRenderData.rdNumDynamicLightsWithShadow * 6 * sizeof(ShadowMapCascades),
      mRenderData.rdDynamicLightShadowMapData.cascades.data());

    for (int i = 0; i < mRenderData.rdNumDynamicLightsWithShadow; ++i) {
      VkRenderingAttachmentInfo dynShadowMapBufferAttachmentInfo {};
      dynShadowMapBufferAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      dynShadowMapBufferAttachmentInfo.imageView = mRenderData.rdDynamicLightShadowData.imageViews.at(i);
      dynShadowMapBufferAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      dynShadowMapBufferAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      dynShadowMapBufferAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      dynShadowMapBufferAttachmentInfo.clearValue = depthImageClearValue;

      VkRenderingInfo dynShadowMapRenderInfo{};
      dynShadowMapRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
      dynShadowMapRenderInfo.renderArea = dynLightShadowMapenderArea;
      dynShadowMapRenderInfo.layerCount = 6;
      dynShadowMapRenderInfo.viewMask = 0b00111111;
      dynShadowMapRenderInfo.pDepthAttachment = &dynShadowMapBufferAttachmentInfo;

      vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &dynShadowMapRenderInfo);
      drawScene(true, true, i * 6);
      drawXRControllers(true, true, i * 6);
      vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));
    }

    auto iter = std::find(mRenderData.rdLightIndices.begin(), mRenderData.rdLightIndices.end(),
      mModelInstCamData.micSelectedDynLight);
    int dynLightIndicesIndex = std::distance(mRenderData.rdLightIndices.begin() + 1, iter);

    // combine the six images for the selected shadow into one
    if (dynLightIndicesIndex < mRenderData.rdNumDynamicLightsWithShadow) {
      VkImageSubresourceRange srcBlitRange{};
      srcBlitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      srcBlitRange.baseMipLevel = 0;
      srcBlitRange.levelCount = 1;
      srcBlitRange.baseArrayLayer = dynLightIndicesIndex * 6;
      srcBlitRange.layerCount = 6;

      VkImageSubresourceRange dstBlitRange{};
      dstBlitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      dstBlitRange.baseMipLevel = 0;
      dstBlitRange.levelCount = 1;
      dstBlitRange.baseArrayLayer = 0;
      dstBlitRange.layerCount = 1;

      VkImageMemoryBarrier firstSrcBarrier{};
      firstSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      firstSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      firstSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      firstSrcBarrier.image = mRenderData.rdDynamicLightShadowData.image;
      firstSrcBarrier.subresourceRange = srcBlitRange;
      firstSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      firstSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      VkImageMemoryBarrier firstDstBarrier{};
      firstDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      firstDstBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      firstDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      firstDstBarrier.image = mRenderData.rdDynamicLightCombinedShadowData.image;
      firstDstBarrier.subresourceRange = dstBlitRange;
      firstDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      firstDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &firstSrcBarrier);

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &firstDstBarrier);

      VkImageBlit depthBlit{};
      depthBlit.srcOffsets[0] = { 0, 0, 0 };
      depthBlit.srcOffsets[1] = {
        static_cast<int32_t>(mRenderData.rdDynLightShadowMapSize.width), static_cast<int32_t>(mRenderData.rdDynLightShadowMapSize.height),
        1 };
      depthBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      depthBlit.srcSubresource.baseArrayLayer = 0;
      depthBlit.srcSubresource.layerCount = 1;
      depthBlit.srcSubresource.mipLevel = 0;

      depthBlit.dstOffsets[0] = { 0, 0, 0 };
      depthBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      depthBlit.dstSubresource.baseArrayLayer = 0;
      depthBlit.dstSubresource.layerCount = 1;
      depthBlit.dstSubresource.mipLevel = 0;

      int32_t cubeFaceWidth = mRenderData.rdDynLightShadowMapSize.width / 4;
      int32_t cubeFaceHeight = mRenderData.rdDynLightShadowMapSize.height / 3;

      for (int i = 0; i < 6; ++i) {
        switch (i) {
          case 0:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6;
            depthBlit.dstOffsets[0] = {
              cubeFaceWidth * 2, cubeFaceHeight, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth * 3, cubeFaceHeight * 2, 1
            };
            break;
          case 1:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6 + 1;
            depthBlit.dstOffsets[0] = {
              0, cubeFaceHeight, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth, cubeFaceHeight * 2, 1
            };
            break;
          case 2:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6 + 2;
            depthBlit.dstOffsets[0] = {
              cubeFaceWidth, 0, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth * 2, cubeFaceHeight, 1
            };
            break;
          case 3:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6 + 3;
            depthBlit.dstOffsets[0] = {
              cubeFaceWidth, cubeFaceHeight * 2, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth * 2, cubeFaceHeight * 3, 1
            };
            break;
          case 4:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6 + 4;
            depthBlit.dstOffsets[0] = {
              cubeFaceWidth, cubeFaceHeight, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth * 2, cubeFaceHeight * 2, 1
            };
            break;
          case 5:
            depthBlit.srcSubresource.baseArrayLayer = dynLightIndicesIndex * 6 + 5;
            depthBlit.dstOffsets[0] = {
              cubeFaceWidth * 3, cubeFaceHeight, 0
            };
            depthBlit.dstOffsets[1] = {
              cubeFaceWidth * 4, cubeFaceHeight * 2, 1
            };
            break;
        }

        vkCmdBlitImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
          mRenderData.rdDynamicLightShadowData.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          mRenderData.rdDynamicLightCombinedShadowData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          1, &depthBlit, VK_FILTER_NEAREST);
      }

      VkImageMemoryBarrier secondSrcBarrier{};
      secondSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      secondSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      secondSrcBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      secondSrcBarrier.image = mRenderData.rdDynamicLightShadowData.image;
      secondSrcBarrier.subresourceRange = srcBlitRange;
      secondSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      secondSrcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      VkImageMemoryBarrier secondDstBarrier{};
      secondDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      secondDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      secondDstBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      secondDstBarrier.image = mRenderData.rdDynamicLightCombinedShadowData.image;
      secondDstBarrier.subresourceRange = dstBlitRange;
      secondDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      secondDstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &secondSrcBarrier);

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &secondDstBarrier);
    } else {
      // clear image when we don't blit
      VkImageSubresourceRange blitRange{};
      blitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      blitRange.baseMipLevel = 0;
      blitRange.levelCount = 1;
      blitRange.baseArrayLayer = 0;
      blitRange.layerCount = 1;

      VkImageMemoryBarrier firstDstBarrier{};
      firstDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      firstDstBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      firstDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      firstDstBarrier.image = mRenderData.rdDynamicLightCombinedShadowData.image;
      firstDstBarrier.subresourceRange = blitRange;
      firstDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      firstDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &firstDstBarrier);

      VkClearDepthStencilValue clearShadowMapDepth = { .depth = 0.0f };

      vkCmdClearDepthStencilImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdDynamicLightCombinedShadowData.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearShadowMapDepth, 1, &blitRange);

      VkImageMemoryBarrier secondDstBarrier{};
      secondDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      secondDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      secondDstBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
      secondDstBarrier.image = mRenderData.rdDynamicLightCombinedShadowData.image;
      secondDstBarrier.subresourceRange = blitRange;
      secondDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      secondDstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

      vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &secondDstBarrier);
    }
  }

  // sun light shadow map pass
  if (mRenderData.rdEnableShadowMap) {
    VkViewport shadowMapViewport{};
    shadowMapViewport.x = 0.0f;
    shadowMapViewport.y = 0.0f;
    shadowMapViewport.width = static_cast<float>(mRenderData.rdShadowMapSize.width);
    shadowMapViewport.height = static_cast<float>(mRenderData.rdShadowMapSize.height);
    shadowMapViewport.minDepth = 0.0f;
    shadowMapViewport.maxDepth = 1.0f;

    VkRect2D shadowMapScissor{};
    shadowMapScissor.offset = { 0, 0 };
    shadowMapScissor.extent = mRenderData.rdShadowMapSize;

    vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &shadowMapViewport);
    vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &shadowMapScissor);

    VkRect2D shadowMapenderArea = VkRect2D{VkOffset2D{}, mRenderData.rdShadowMapSize};

    VkRenderingAttachmentInfo shadowMapBufferAttachmentInfo {};
    shadowMapBufferAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    shadowMapBufferAttachmentInfo.imageView = mRenderData.rdShadowMapDepthBufferData.imageView;
    shadowMapBufferAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowMapBufferAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    shadowMapBufferAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    shadowMapBufferAttachmentInfo.clearValue = depthImageClearValue;

    VkRenderingInfo shadowMapRenderInfo{};
    shadowMapRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    shadowMapRenderInfo.renderArea = shadowMapenderArea;
    shadowMapRenderInfo.viewMask = 0b00001111;
    shadowMapRenderInfo.layerCount = mRenderData.SHADOW_MAP_LAYERS;
    shadowMapRenderInfo.pDepthAttachment = &shadowMapBufferAttachmentInfo;

    VkImageSubresourceRange shadowMapDepthBufferSubresourceRangeOne{};
    shadowMapDepthBufferSubresourceRangeOne.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    shadowMapDepthBufferSubresourceRangeOne.baseArrayLayer = 0;
    shadowMapDepthBufferSubresourceRangeOne.layerCount = mRenderData.SHADOW_MAP_LAYERS;
    shadowMapDepthBufferSubresourceRangeOne.baseMipLevel = 0;
    shadowMapDepthBufferSubresourceRangeOne.levelCount = 1;

    VkImageMemoryBarrier shadowMapDepthBufferImageMemoryBarrierOne {};
    shadowMapDepthBufferImageMemoryBarrierOne.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shadowMapDepthBufferImageMemoryBarrierOne.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    shadowMapDepthBufferImageMemoryBarrierOne.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowMapDepthBufferImageMemoryBarrierOne.image = mRenderData.rdShadowMapDepthBufferData.image;
    shadowMapDepthBufferImageMemoryBarrierOne.subresourceRange = shadowMapDepthBufferSubresourceRangeOne;

    vkCmdPipelineBarrier(
      mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
      0,
      0, nullptr, 0, nullptr,
      1, &shadowMapDepthBufferImageMemoryBarrierOne // pImageMemoryBarriers
    );

    vkCmdUpdateBuffer(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      mRenderData.rdShadowMapCascadeDataBuffers.at(mRenderData.currentFrame).buffer, 0, 4 * sizeof(ShadowMapCascades),
      mRenderData.rdShadowMapCascadeData.cascades.data());

    vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &shadowMapRenderInfo);
    drawScene(true);
    drawXRControllers(true);
    vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

    // combine all four depth images into a single image for debug display (ImGui can't handle array textures)
    VkImageSubresourceRange srcBlitRange{};
    srcBlitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    srcBlitRange.baseMipLevel = 0;
    srcBlitRange.levelCount = 1;
    srcBlitRange.baseArrayLayer = 0;
    srcBlitRange.layerCount = mRenderData.SHADOW_MAP_LAYERS;

    VkImageSubresourceRange dstBlitRange{};
    dstBlitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dstBlitRange.baseMipLevel = 0;
    dstBlitRange.levelCount = 1;
    dstBlitRange.baseArrayLayer = 0;
    dstBlitRange.layerCount = 1;

    VkImageMemoryBarrier firstSrcBarrier{};
    firstSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    firstSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    firstSrcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    firstSrcBarrier.image = mRenderData.rdShadowMapDepthBufferData.image;
    firstSrcBarrier.subresourceRange = srcBlitRange;
    firstSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    firstSrcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkImageMemoryBarrier firstDstBarrier{};
    firstDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    firstDstBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    firstDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    firstDstBarrier.image = mRenderData.rdShadowMapCombinedDepthBufferData.image;
    firstDstBarrier.subresourceRange = dstBlitRange;
    firstDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    firstDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &firstSrcBarrier);

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &firstDstBarrier);

    VkImageBlit depthBlit{};
    depthBlit.srcOffsets[0] = { 0, 0, 0 };
    depthBlit.srcOffsets[1] = {
      static_cast<int32_t>(mRenderData.rdShadowMapSize.width), static_cast<int32_t>(mRenderData.rdShadowMapSize.height),
      1 };
    depthBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBlit.srcSubresource.baseArrayLayer = 0;
    depthBlit.srcSubresource.layerCount = 1;
    depthBlit.srcSubresource.mipLevel = 0;

    depthBlit.dstOffsets[0] = { 0, 0, 0 };
    depthBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthBlit.dstSubresource.baseArrayLayer = 0;
    depthBlit.dstSubresource.layerCount = 1;
    depthBlit.dstSubresource.mipLevel = 0;

    for (int i = 0; i < mRenderData.SHADOW_MAP_LAYERS; ++i) {
      switch (i) {
        case 0:
          depthBlit.srcSubresource.baseArrayLayer = 0;
          depthBlit.dstOffsets[0] = { 0, 0, 0 };
          depthBlit.dstOffsets[1] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width) / 2, static_cast<int32_t>(mRenderData.rdShadowMapSize.height) / 2,
            1 };
          break;
        case 1:
          depthBlit.srcSubresource.baseArrayLayer = 1;
          depthBlit.dstOffsets[0] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width) / 2,
            0,
            0 };
          depthBlit.dstOffsets[1] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width), static_cast<int32_t>(mRenderData.rdShadowMapSize.height) / 2,
            1 };
          break;
        case 2:
          depthBlit.srcSubresource.baseArrayLayer = 2;
          depthBlit.dstOffsets[0] = {
            0,
            static_cast<int32_t>(mRenderData.rdShadowMapSize.height) / 2,
            0 };
          depthBlit.dstOffsets[1] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width) / 2, static_cast<int32_t>(mRenderData.rdShadowMapSize.height),
            1 };
          break;
        case 3:
          depthBlit.srcSubresource.baseArrayLayer = 3;
          depthBlit.dstOffsets[0] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width) / 2,
            static_cast<int32_t>(mRenderData.rdShadowMapSize.height) / 2,
            0 };
          depthBlit.dstOffsets[1] = {
            static_cast<int32_t>(mRenderData.rdShadowMapSize.width), static_cast<int32_t>(mRenderData.rdShadowMapSize.height),
            1 };
          break;
      }

      vkCmdBlitImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
        mRenderData.rdShadowMapDepthBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        mRenderData.rdShadowMapCombinedDepthBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &depthBlit, VK_FILTER_NEAREST);
    }

    VkImageMemoryBarrier secondSrcBarrier{};
    secondSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    secondSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    secondSrcBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    secondSrcBarrier.image = mRenderData.rdShadowMapDepthBufferData.image;
    secondSrcBarrier.subresourceRange = srcBlitRange;
    secondSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    secondSrcBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkImageMemoryBarrier secondDstBarrier{};
    secondDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    secondDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    secondDstBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    secondDstBarrier.image = mRenderData.rdShadowMapCombinedDepthBufferData.image;
    secondDstBarrier.subresourceRange = dstBlitRange;
    secondDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    secondDstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &secondSrcBarrier);

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &secondDstBarrier);
  } else {
    // clear image when we don't blit
    VkImageSubresourceRange blitRange{};
    blitRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    blitRange.baseMipLevel = 0;
    blitRange.levelCount = 1;
    blitRange.baseArrayLayer = 0;
    blitRange.layerCount = 1;

    VkImageMemoryBarrier firstDstBarrier{};
    firstDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    firstDstBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    firstDstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    firstDstBarrier.image = mRenderData.rdShadowMapCombinedDepthBufferData.image;
    firstDstBarrier.subresourceRange = blitRange;
    firstDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    firstDstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &firstDstBarrier);

    VkClearDepthStencilValue clearShadowMapDepth = { .depth = 1.0f };

    vkCmdClearDepthStencilImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdShadowMapCombinedDepthBufferData.image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearShadowMapDepth, 1, &blitRange);

    VkImageMemoryBarrier secondDstBarrier{};
    secondDstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    secondDstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    secondDstBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    secondDstBarrier.image = mRenderData.rdShadowMapCombinedDepthBufferData.image;
    secondDstBarrier.subresourceRange = blitRange;
    secondDstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    secondDstBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &secondDstBarrier);

    // Shadowmap depth, image conversion happens already in shadow map combined blitting if enabled
    VkImageSubresourceRange shadowMapDepthBufferSubresourceRangeTwo{};
    shadowMapDepthBufferSubresourceRangeTwo.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    shadowMapDepthBufferSubresourceRangeTwo.baseArrayLayer = 0;
    shadowMapDepthBufferSubresourceRangeTwo.layerCount = mRenderData.SHADOW_MAP_LAYERS;
    shadowMapDepthBufferSubresourceRangeTwo.baseMipLevel = 0;
    shadowMapDepthBufferSubresourceRangeTwo.levelCount = 1;

    VkImageMemoryBarrier shadowMapDepthBufferImageMemoryBarrierTwo {};
    shadowMapDepthBufferImageMemoryBarrierTwo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shadowMapDepthBufferImageMemoryBarrierTwo.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    shadowMapDepthBufferImageMemoryBarrierTwo.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    shadowMapDepthBufferImageMemoryBarrierTwo.image = mRenderData.rdShadowMapDepthBufferData.image;
    shadowMapDepthBufferImageMemoryBarrierTwo.subresourceRange = shadowMapDepthBufferSubresourceRangeTwo;

    vkCmdPipelineBarrier(
      mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
      0,
      0, nullptr, 0, nullptr,
      1, &shadowMapDepthBufferImageMemoryBarrierTwo // pImageMemoryBarriers
    );
  }

  VkRect2D renderArea = VkRect2D{VkOffset2D{}, VkExtent2D{mRenderData.rdXRWidth, mRenderData.rdXRHeight}};

  // render XR visibility mask
  VkRenderingAttachmentInfo visColorAttachmentInfo {};
  visColorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  visColorAttachmentInfo.imageView = mRenderData.rdGBuffer.color.imageView;
  visColorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  visColorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  visColorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  visColorAttachmentInfo.clearValue = colorClearValue;

  VkRenderingAttachmentInfo visDepthImageAttachmentInfo {};
  visDepthImageAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  visDepthImageAttachmentInfo.imageView = mRenderData.rdGBuffer.depth.imageView;
  visDepthImageAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  visDepthImageAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  visDepthImageAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  visDepthImageAttachmentInfo.clearValue = depthImageClearValue;

  VkRenderingAttachmentInfo visDepthAttachmentInfo {};
  visDepthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  visDepthAttachmentInfo.imageView = mRenderData.rdDepthBufferData.imageView;
  visDepthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  visDepthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  visDepthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  visDepthAttachmentInfo.clearValue = depthClearValue;

  std::vector<VkRenderingAttachmentInfo> visMaskttachmentInfos {
    visColorAttachmentInfo,
    visDepthImageAttachmentInfo,
  };

  VkRenderingInfo visMaskRenderInfo{};
  visMaskRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  visMaskRenderInfo.renderArea = renderArea;
  visMaskRenderInfo.layerCount = 2;
  visMaskRenderInfo.viewMask = kXRPipelineViewMask;
  visMaskRenderInfo.colorAttachmentCount = static_cast<uint32_t>(visMaskttachmentInfos.size());
  visMaskRenderInfo.pColorAttachments = visMaskttachmentInfos.data();
  visMaskRenderInfo.pDepthAttachment = &visDepthAttachmentInfo;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(mRenderData.rdXRWidth);
  viewport.height = static_cast<float>(mRenderData.rdXRHeight);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = VkExtent2D{mRenderData.rdXRWidth, mRenderData.rdXRHeight};

  vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &viewport);
  vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &scissor);

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.color.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.depth.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &visMaskRenderInfo);

  drawXRVisibilityMask();

  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

  // scene drawing
  VkRenderingAttachmentInfo colorAttachmentInfo {};
  colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  colorAttachmentInfo.imageView = mRenderData.rdGBuffer.color.imageView;
  colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  //colorAttachmentInfo.clearValue = colorClearValue;

  VkRenderingAttachmentInfo depthImageAttachmentInfo {};
  depthImageAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthImageAttachmentInfo.imageView = mRenderData.rdGBuffer.depth.imageView;
  depthImageAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  depthImageAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depthImageAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  //depthImageAttachmentInfo.clearValue = depthImageClearValue;

  VkRenderingAttachmentInfo normalAttachmentInfo {};
  normalAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  normalAttachmentInfo.imageView = mRenderData.rdGBuffer.normal.imageView;
  normalAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  normalAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  normalAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  normalAttachmentInfo.clearValue = normalClearValue;

  VkRenderingAttachmentInfo selectionAttachmentInfo {};
  selectionAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  selectionAttachmentInfo.imageView = mRenderData.rdSelectionImageData.imageView;
  selectionAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  selectionAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  selectionAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  selectionAttachmentInfo.clearValue = selectionClearValue;

  VkRenderingAttachmentInfo depthAttachmentInfo {};
  depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  depthAttachmentInfo.imageView = mRenderData.rdDepthBufferData.imageView;
  depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  //depthAttachmentInfo.clearValue = depthClearValue;

  std::vector<VkRenderingAttachmentInfo> attachmentInfos {
    colorAttachmentInfo,
    depthImageAttachmentInfo,
    normalAttachmentInfo,
    selectionAttachmentInfo,
  };

  VkRenderingInfo renderInfo{};
  renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  renderInfo.renderArea = renderArea;
  renderInfo.layerCount = 2;
  renderInfo.viewMask = kXRPipelineViewMask;
  renderInfo.colorAttachmentCount = static_cast<uint32_t>(attachmentInfos.size());
  renderInfo.pColorAttachments = attachmentInfos.data();
  renderInfo.pDepthAttachment = &depthAttachmentInfo;

  vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &viewport);
  vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &scissor);

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.normal.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSelectionImageData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdLightSpheresBufferData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdShadowMapCombinedDepthBufferData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1);

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &renderInfo);

  drawScene();
  drawXRControllers();

  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.depth.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdGBuffer.normal.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOColorBufferData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOBlurBufferData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);

  VkDeviceSize offset = 0;

  // light sphere pass
  if (mRenderData.rdNumDynamicLights > 0) {
    VkRenderingAttachmentInfo lightSphereBufferAttachmentInfo {};
    lightSphereBufferAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    lightSphereBufferAttachmentInfo.imageView = mRenderData.rdLightSpheresBufferData.imageView;
    lightSphereBufferAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    lightSphereBufferAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    lightSphereBufferAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    lightSphereBufferAttachmentInfo.clearValue = blackClearValue;

    std::vector<VkRenderingAttachmentInfo> lightSphereAttachmentInfos {
      lightSphereBufferAttachmentInfo,
    };

    VkRenderingInfo lightSphereRenderInfo{};
    lightSphereRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    lightSphereRenderInfo.renderArea = renderArea;
    lightSphereRenderInfo.layerCount = 2;
    lightSphereRenderInfo.viewMask = kXRPipelineViewMask;
    lightSphereRenderInfo.colorAttachmentCount = static_cast<uint32_t>(lightSphereAttachmentInfos.size());
    lightSphereRenderInfo.pColorAttachments = lightSphereAttachmentInfos.data();

    // transition to shader read only
    VkImageSubresourceRange dynLightShadowDataSubresourceRangeTwo{};
    dynLightShadowDataSubresourceRangeTwo.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dynLightShadowDataSubresourceRangeTwo.baseArrayLayer = 0;
    dynLightShadowDataSubresourceRangeTwo.layerCount = mRenderData.rdDynamicLightShadowData.numLayers;
    dynLightShadowDataSubresourceRangeTwo.baseMipLevel = 0;
    dynLightShadowDataSubresourceRangeTwo.levelCount = 1;

    VkImageMemoryBarrier dynLightShadowDataImageMemoryBarrierTwo {};
    dynLightShadowDataImageMemoryBarrierTwo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dynLightShadowDataImageMemoryBarrierTwo.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    dynLightShadowDataImageMemoryBarrierTwo.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    dynLightShadowDataImageMemoryBarrierTwo.image = mRenderData.rdDynamicLightShadowData.image;
    dynLightShadowDataImageMemoryBarrierTwo.subresourceRange = dynLightShadowDataSubresourceRangeTwo;

    vkCmdPipelineBarrier(
      mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
      0,
      0, nullptr, 0, nullptr,
      1, &dynLightShadowDataImageMemoryBarrierTwo // pImageMemoryBarriers
    );

    vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &lightSphereRenderInfo);

    if (mRenderData.rdNumDynamicLightsWithShadow > 0) {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLightSphereShadowPipeline);
      vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLightSphereShadowPipelineLayout, 0, 1,
        &mRenderData.rdLightSphereShadowsDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
      vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLightSphereVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);

      vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mFullSphereMesh.vertices.size()), mRenderData.rdNumDynamicLightsWithShadow, 0, 1);
    }

    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLightSpherePipeline);
    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLightSpherePipelineLayout, 0, 1,
      &mRenderData.rdLightSphereDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLightSphereVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);

    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mFullSphereMesh.vertices.size()), mRenderData.rdNumDynamicLights - mRenderData.rdNumDynamicLightsWithShadow, 0, mRenderData.rdNumDynamicLightsWithShadow + 1);

    vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

    // transition back
    VkImageSubresourceRange dynLightShadowDataSubresourceRangeThree{};
    dynLightShadowDataSubresourceRangeThree.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dynLightShadowDataSubresourceRangeThree.baseArrayLayer = 0;
    dynLightShadowDataSubresourceRangeThree.layerCount = mRenderData.rdDynamicLightShadowData.numLayers;
    dynLightShadowDataSubresourceRangeThree.baseMipLevel = 0;
    dynLightShadowDataSubresourceRangeThree.levelCount = 1;

    VkImageMemoryBarrier dynLightShadowDataImageMemoryBarrierThree{};
    dynLightShadowDataImageMemoryBarrierThree.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    dynLightShadowDataImageMemoryBarrierThree.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    dynLightShadowDataImageMemoryBarrierThree.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    dynLightShadowDataImageMemoryBarrierThree.image = mRenderData.rdDynamicLightShadowData.image;
    dynLightShadowDataImageMemoryBarrierThree.subresourceRange = dynLightShadowDataSubresourceRangeThree;

    vkCmdPipelineBarrier(
      mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
      0,
      0, nullptr, 0, nullptr,
      1, &dynLightShadowDataImageMemoryBarrierThree // pImageMemoryBarriers
    );
  }

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdLightSpheresBufferData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

  // SSAO pass
  if (mRenderData.rdEnableSSAO) {
    VkClearValue ssaoClearValue;
    ssaoClearValue.color = { { 1.0f } };

    VkRenderingAttachmentInfo ssaoColorBufferAttachmentInfo {};
    ssaoColorBufferAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ssaoColorBufferAttachmentInfo.imageView = mRenderData.rdSSAOColorBufferData.imageView;
    ssaoColorBufferAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ssaoColorBufferAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ssaoColorBufferAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ssaoColorBufferAttachmentInfo.clearValue = ssaoClearValue;

    std::vector<VkRenderingAttachmentInfo> ssaoAttachmentInfos {
      ssaoColorBufferAttachmentInfo,
    };

    VkRenderingInfo ssaoRenderInfo{};
    ssaoRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ssaoRenderInfo.renderArea = renderArea;
    ssaoRenderInfo.layerCount = 2;
    ssaoRenderInfo.viewMask = kXRPipelineViewMask;
    ssaoRenderInfo.colorAttachmentCount = static_cast<uint32_t>(ssaoAttachmentInfos.size());
    ssaoRenderInfo.pColorAttachments = ssaoAttachmentInfos.data();

    vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &ssaoRenderInfo);

    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSSAOPipeline);
    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSSAOPipelineLayout, 0, 1,
      &mRenderData.rdSSAODescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3, 1, 0, 0);

    vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

    // SSAO Blur pass
    // runs always to avoid blinking when swichting off 
    VkClearValue ssaoBlurClearValue;
    ssaoBlurClearValue.color = { { 1.0f } };

    VkRenderingAttachmentInfo ssaoBlurBufferAttachmentInfo {};
    ssaoBlurBufferAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    ssaoBlurBufferAttachmentInfo.imageView = mRenderData.rdSSAOBlurBufferData.imageView;
    ssaoBlurBufferAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ssaoBlurBufferAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ssaoBlurBufferAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ssaoBlurBufferAttachmentInfo.clearValue = ssaoBlurClearValue;

    std::vector<VkRenderingAttachmentInfo> ssaoBlurAttachmentInfos {
      ssaoBlurBufferAttachmentInfo,
    };

    VkRenderingInfo ssaoBlurRenderInfo{};
    ssaoBlurRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ssaoBlurRenderInfo.renderArea = renderArea;
    ssaoBlurRenderInfo.layerCount = 2;
    ssaoBlurRenderInfo.viewMask = kXRPipelineViewMask;
    ssaoBlurRenderInfo.colorAttachmentCount = static_cast<uint32_t>(ssaoBlurAttachmentInfos.size());
    ssaoBlurRenderInfo.pColorAttachments = ssaoBlurAttachmentInfos.data();

    VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOColorBufferData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

    vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &ssaoBlurRenderInfo);

    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSSAOBlurPipeline);
    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSSAOBlurPipelineLayout, 0, 1,
      &mRenderData.rdSSAOBlurDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3, 1, 0, 0);
    vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));
  }
  else {
    // clear SSAO buffers to avoid artifacts when swichting off SSAO
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 2;

    VkClearColorValue clearValue = { { 1.0f, 1.0f, 1.0f, 1.0f } };

    VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOBlurBufferData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2);

    vkCmdClearColorImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdSSAOBlurBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &subresourceRange);

    VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOBlurBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);

    VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOColorBufferData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2);

    vkCmdClearColorImage(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdSSAOColorBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &subresourceRange);

    VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOColorBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);
  }

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdFinalImageData.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2);

  // Composite pass
  VkRenderingAttachmentInfo finalImageAttachmentInfo {};
  finalImageAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  finalImageAttachmentInfo.imageView = mRenderData.rdFinalImageData.imageView;
  finalImageAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  finalImageAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  finalImageAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  finalImageAttachmentInfo.clearValue = colorClearValue;

  // we need to retain the previous contents of the buffer
  selectionAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD ;
  depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

  std::vector<VkRenderingAttachmentInfo> compositeAttachmentInfos {
    finalImageAttachmentInfo,
    selectionAttachmentInfo
  };

  // clear depth buffer if we draw for shadow map debug
  if (mRenderData.rdCompositeDebug == compositeDebugDisplay::shadowMap) {
    VkClearValue depthClearValueZero;
    depthClearValueZero.depthStencil.depth = 0.0f;
    depthAttachmentInfo.clearValue = depthClearValueZero;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  }

  VkRenderingInfo compositeRenderInfo{};
  compositeRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  compositeRenderInfo.renderArea = renderArea;
  compositeRenderInfo.layerCount = 2;
  compositeRenderInfo.viewMask = kXRPipelineViewMask;
  compositeRenderInfo.colorAttachmentCount = static_cast<uint32_t>(compositeAttachmentInfos.size());
  compositeRenderInfo.pColorAttachments = compositeAttachmentInfos.data();
  compositeRenderInfo.pDepthAttachment = &depthAttachmentInfo;

  VkImageSubresourceRange shadowMapDepthBufferSubresourceRangeThree{};
  shadowMapDepthBufferSubresourceRangeThree.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  shadowMapDepthBufferSubresourceRangeThree.baseArrayLayer = 0;
  shadowMapDepthBufferSubresourceRangeThree.layerCount = mRenderData.SHADOW_MAP_LAYERS;
  shadowMapDepthBufferSubresourceRangeThree.baseMipLevel = 0;
  shadowMapDepthBufferSubresourceRangeThree.levelCount = 1;

  VkImageMemoryBarrier shadowMapDepthBufferImageMemoryBarrierThree {};
  shadowMapDepthBufferImageMemoryBarrierThree.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  shadowMapDepthBufferImageMemoryBarrierThree.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
  shadowMapDepthBufferImageMemoryBarrierThree.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  shadowMapDepthBufferImageMemoryBarrierThree.image = mRenderData.rdShadowMapDepthBufferData.image;
  shadowMapDepthBufferImageMemoryBarrierThree.subresourceRange = shadowMapDepthBufferSubresourceRangeThree;

  vkCmdPipelineBarrier(
    mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
    0,
    0, nullptr, 0, nullptr,
    1, &shadowMapDepthBufferImageMemoryBarrierThree // pImageMemoryBarriers
  );

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSSAOBlurBufferData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &compositeRenderInfo);

  vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdCompositePipeline);
  vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdCompositePipelineLayout, 0, 1,
    &mRenderData.rdCompositeDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

  vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3, 1, 0, 0);

  // draw skybox into swapchain image, depth writes are disabled
  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = cam->getCameraSettings();
  if (mRenderData.rdDrawSkybox) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdSkyboxPipeline);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
     mRenderData.rdSkyboxPipelineLayout, 0, 1,
     &mRenderData.rdSkyboxTexture.descriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdSkyboxPipelineLayout, 1, 1,
      &mRenderData.rdSkyboxDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdSkyboxBuffers.at(mRenderData.currentFrame).buffer, &offset);

    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mSphereModel.getVertexData().vertices.size()), 1, 0, 0);
  }

  // draw infinte grid
  if (mRenderData.rdEnableInfiniteGrid && mRenderData.rdApplicationMode == appMode::edit) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdGridLinePipeline);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            mRenderData.rdLinePipelineLayout, 0, 1, &mRenderData.rdLineDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
    vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 1.0f);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 6, 1, 0, 0);
  }

  // draw lines also into swapchain image
  mRenderData.rdCollisionDebugDrawTimer.start();
  if (mLineIndexCount > 0) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLinePipeline);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdLinePipelineLayout, 0, 1, &mRenderData.rdLineDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLineVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3.0f);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mLineMesh->vertices.size()), 1, 0, 0);
  }

  // draw colliding spheres
  if (mCollidingSphereCount > 0) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSpherePipeline);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdSpherePipelineLayout, 0, 1, &mRenderData.rdSphereDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdSphereVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3.0f);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mSphereVertexCount, mCollidingSphereCount, 0, 0);
  }

  mRenderData.rdCollisionDebugDrawTime += mRenderData.rdCollisionDebugDrawTimer.stop();

  if (mRenderData.rdDrawLevelAABB || mRenderData.rdDrawLevelWireframe ||
      mRenderData.rdDrawLevelOctree || mRenderData.rdDrawIKDebugLines ||
      mRenderData.rdDrawInstancePaths || mRenderData.rdDrawNeighborTriangles) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdLinePipeline);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdLinePipelineLayout, 0, 1, &mRenderData.rdLineDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
    vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3.0f);
  }

  mRenderData.rdLevelCollisionTimer.start();
  if (mRenderData.rdDrawLevelAABB && !mLevelAABBMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLevelAABBVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mLevelAABBMesh->vertices.size()), 1, 0, 0);
  }

  if (mRenderData.rdDrawLevelWireframe && !mLevelWireframeMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLevelWireframeVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mLevelWireframeMesh->vertices.size()), 1, 0, 0);
  }

  if (mRenderData.rdDrawLevelOctree && !mLevelOctreeMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLevelOctreeVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mLevelOctreeMesh->vertices.size()), 1, 0, 0);
  }

  if (mRenderData.rdDrawIKDebugLines && !mIKFootPointMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdIKLinesVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mIKFootPointMesh->vertices.size()), 1, 0, 0);
  }
  mRenderData.rdLevelCollisionTime += mRenderData.rdLevelCollisionTimer.stop();

  if (mRenderData.rdDrawInstancePaths && !mInstancePathMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdInstancePathVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mInstancePathMesh->vertices.size()), 1, 0, 0);
  }

  if (mRenderData.rdDrawNeighborTriangles && !mLevelGroundNeighborsMesh->vertices.empty()) {
    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdGroundMeshNeighborVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), static_cast<uint32_t>(mLevelGroundNeighborsMesh->vertices.size()), 1, 0, 0);
  }

  mRenderData.rdLevelGroundNeighborUpdateTimer.start();
  if (mRenderData.rdDrawGroundTriangles) {
    vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdGroundMeshMeshPipeline);
    VkHelper::enableBlending(mRenderData, 2);

    vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
      mRenderData.rdLinePipelineLayout, 0, 1, &mRenderData.rdGroundMeshDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdGroundMeshVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
    vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mGroundMeshVertexCount, 1, 0, 0);
  }
  mRenderData.rdLevelGroundNeighborUpdateTime += mRenderData.rdLevelGroundNeighborUpdateTimer.stop();

  // draw in forward rendering after composite pass to avoid model light problems
  if (mRenderData.rdApplicationMode == appMode::edit) {
    if (mMousePick) {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
        VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdAssimpPostCompositeSelectionPipeline);

      vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdAssimpSelectionPipelineLayout, 1, 1, &mRenderData.rdAssimpSelectionDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
    } else {
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
        VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdAssimpPostCompositePipeline);

      vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdAssimpPipelineLayout, 1, 1, &mRenderData.rdAssimpDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);
    }

    mRenderData.rdUploadToUBOTimer.start();
    mRenderData.rdModelData.pkWorldPosOffset = mWorldPosOffset;
    if (mMousePick) {
      vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpSelectionPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
    } else {
      vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdAssimpPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);
    }
    mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

    mLightModel->drawInstanced(mRenderData, mRenderData.rdNumDynamicLights, mMousePick);

    if (mRenderData.rdEnableLightDebug) {
      uint32_t dynLightVertexCount = static_cast<uint32_t>(mDynLightModel.getVertexData().vertices.size());
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSpherePipeline);

      vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdSpherePipelineLayout, 0, 1, &mRenderData.rdDynLightDebugSphereDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

      vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdDynamicLightDebugVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
      vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3.0f);
      vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), dynLightVertexCount, mRenderData.rdNumDynamicLights, 0, 1);
    }

    if (mRenderData.rdEnableLightSphereDebug) {
      uint32_t dynLightVertexCount = static_cast<uint32_t>(mFullSphereDebugMesh.vertices.size());
      vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSpherePipeline);

      vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
        mRenderData.rdSpherePipelineLayout, 0, 1, &mRenderData.rdDynLightDebugSphereDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

      vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &mRenderData.rdLightSphereDebugVertexBuffers.at(mRenderData.currentFrame).buffer, &offset);
      vkCmdSetLineWidth(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 1.0f);
      vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), dynLightVertexCount, mRenderData.rdNumDynamicLights, 0, 1);
    }
  }
  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

  if (!mRenderData.rdSkipDesktopMirror) {
  // copy images to swapchain, must be done via shaders as swapchain may be color attachment only
  VkViewport swapchainCopyViewport{};
  swapchainCopyViewport.x = 0.0f;
  swapchainCopyViewport.y = 0.0f;
  swapchainCopyViewport.width = static_cast<float>(mRenderData.rdVkbSwapchain.extent.width);
  swapchainCopyViewport.height = static_cast<float>(mRenderData.rdVkbSwapchain.extent.height);
  swapchainCopyViewport.minDepth = 0.0f;
  swapchainCopyViewport.maxDepth = 1.0f;

  vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &swapchainCopyViewport);
  vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &scissor);

  VkRenderingAttachmentInfo swapchainAttachmentInfo {};
  swapchainAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  swapchainAttachmentInfo.imageView = mRenderData.rdSwapchainImageViews.at(mImageIndex);
  swapchainAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  swapchainAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  swapchainAttachmentInfo.clearValue = colorClearValue;

  std::vector<VkRenderingAttachmentInfo> swapchainCopyAttachmentInfos {
    swapchainAttachmentInfo,
  };

  VkRect2D swapchainRenderArea = VkRect2D{VkOffset2D{}, VkExtent2D{mRenderData.rdVkbSwapchain.extent.width, mRenderData.rdVkbSwapchain.extent.height}};

  VkRenderingInfo swapchainCopyRenderInfo{};
  swapchainCopyRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  swapchainCopyRenderInfo.renderArea = swapchainRenderArea;
  swapchainCopyRenderInfo.layerCount = 1;
  swapchainCopyRenderInfo.colorAttachmentCount = static_cast<uint32_t>(swapchainCopyAttachmentInfos.size());
  swapchainCopyRenderInfo.pColorAttachments = swapchainCopyAttachmentInfos.data();

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSwapchainImages.at(mImageIndex), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdFinalImageData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &swapchainCopyRenderInfo);

  vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSwapchainCopyPipeline);
  vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSwapchainCopyPipelineLayout, 0, 1,
    &mRenderData.rdSwapchainCopyDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

  vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3, 1, 0, 0);

  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

  VkRect2D uiRenderArea = VkRect2D{VkOffset2D{}, VkExtent2D{mRenderData.rdVkbSwapchain.extent.width, mRenderData.rdVkbSwapchain.extent.height}};

  // imGui overlay needs a separate rendering pass due to a different internal pipeline
  VkRenderingAttachmentInfo swapchainUIAttachmentInfo {};
  swapchainUIAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  swapchainUIAttachmentInfo.imageView = mRenderData.rdSwapchainImageViews.at(mImageIndex);
  swapchainUIAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainUIAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  swapchainUIAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  //swapchainUIAttachmentInfo.clearValue = colorClearValue;

  std::vector<VkRenderingAttachmentInfo> uiAttachmentInfos { swapchainUIAttachmentInfo };

  VkRenderingInfo uiRenderInfo{};
  uiRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  uiRenderInfo.renderArea = uiRenderArea;
  uiRenderInfo.layerCount = 1;
  uiRenderInfo.colorAttachmentCount = static_cast<uint32_t>(uiAttachmentInfos.size());
  uiRenderInfo.pColorAttachments = uiAttachmentInfos.data();

  VkHelper::transitionImageLayout(mRenderData, mRenderData.rdSelectionImageData.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 2);

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &uiRenderInfo);

  mRenderData.rdUIDrawTimer.start();
  mUserInterface.render(mRenderData);
  mRenderData.rdUIDrawTime = mRenderData.rdUIDrawTimer.stop();

  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));
  }

  if (!mRenderData.rdSkipDesktopMirror) {
  // layout transition
  // swapchain image to present
  VkImageSubresourceRange imageSSR;
  imageSSR.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageSSR.baseMipLevel = 0;
  imageSSR.levelCount = 1;
  imageSSR.baseArrayLayer = 0;
  imageSSR.layerCount = 1;

  VkImageMemoryBarrier secondImageMemoryBarrier{};
  secondImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  secondImageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  secondImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  secondImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  secondImageMemoryBarrier.image = mRenderData.rdSwapchainImages.at(mImageIndex);
  secondImageMemoryBarrier.subresourceRange = imageSSR;

  vkCmdPipelineBarrier(
    mRenderData.rdCommandBuffers.at(mRenderData.currentFrame),
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, // dstStageMask
    0,
    0, nullptr, 0, nullptr,
    1, &secondImageMemoryBarrier // pImageMemoryBarriers
  );
  }

  return true;
}

bool VkRenderer::updateCamera(XRProjectionViewMatrices &matrices, float deltaTime) {
  mRenderData.rdMatrixGenerateTimer.start();
  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  cam->updateCamera(mRenderData, deltaTime);

  glm::mat4 camTranspose = glm::translate(glm::mat4(1.0f), cam->getWorldPosition());
  glm::mat4 camOrientation = glm::mat4(glm::mat3(cam->getViewMatrix()));

  for (int i = 0; i < 2; ++i) {
    mRenderUploadData.projectionMatrix.at(i) = matrices.projectionMat.at(i);

    mRenderUploadData.viewMatrix.at(i) = glm::inverse(camTranspose * matrices.viewTransposeMat.at(i) * camOrientation * matrices.viewOrientationMat.at(i));
  }

  mRenderUploadData.nearPlane = mRenderData.rdNearPlane;
  mRenderUploadData.farPlane = mRenderData.rdFarPlane;

  mRenderUploadData.inverseProjectionMatrix.at(0) = glm::inverse(mRenderUploadData.projectionMatrix.at(0));
  mRenderUploadData.inverseProjectionMatrix.at(1) = glm::inverse(mRenderUploadData.projectionMatrix.at(1));

  mRenderUploadData.inverseViewMatrix.at(0) = glm::inverse(mRenderUploadData.viewMatrix.at(0));
  mRenderUploadData.inverseViewMatrix.at(1) = glm::inverse(mRenderUploadData.viewMatrix.at(1));

  if (mRenderData.rdEnableTimeOfDay)  {
    // search the two values we are inbetween
    timeOfDay lowestToD = timeOfDay::midnight;
    for (const auto& tofd : mRenderData.rdTimeOfDayLightSettings) {
      if (tofd.second.timeStamp <= mRenderData.rdTimeOfDay) {
        lowestToD = tofd.first;
      }
    }

    timeOfDay nextToD = lowestToD + 1;
    if (nextToD == timeOfDay::NUM) {
      nextToD = timeOfDay::midnight;
    }

    TimeOfDayLightParameters first = mRenderData.rdTimeOfDayLightSettings[lowestToD];
    TimeOfDayLightParameters second = mRenderData.rdTimeOfDayLightSettings[nextToD];

    // scale the mix factor equally on the range between the two timestamps
    float mixFactor = (mRenderData.rdTimeOfDay - first.timeStamp) / ( second.timeStamp - first.timeStamp);

    // calculate mixed values
    mRenderData.rdLightSourceAngleEastWest = glm::mix(first.lightAngleEW, second.lightAngleEW, mixFactor);
    mRenderData.rdLightSourceAngleNorthSouth = glm::mix(first.lightAngleNS, second.lightAngleNS, mixFactor);
    mRenderData.rdLightSourceIntensity = glm::mix(first.lightIntensity, second.lightIntensity, mixFactor);
    mRenderData.rdLightSourceColor = glm::mix(first.lightColor, second.lightColor, mixFactor);
  }

  // light source angle - X axis is east-west
  glm::vec4 lightPos;
  lightPos.x = std::sin(glm::radians(mRenderData.rdLightSourceAngleEastWest)) * std::cos(glm::radians(mRenderData.rdLightSourceAngleNorthSouth));
  lightPos.y = std::sin(glm::radians(mRenderData.rdLightSourceAngleEastWest)) * std::sin(glm::radians(mRenderData.rdLightSourceAngleNorthSouth));
  lightPos.z = std::cos(glm::radians(mRenderData.rdLightSourceAngleEastWest));
  lightPos.w = 1.0f;

  glm::vec3 cameraPos = cam->getWorldPosition();
  glm::vec4 lightColor= glm::vec4(mRenderData.rdLightSourceColor * mRenderData.rdLightSourceIntensity, mRenderData.rdLightSourceIntensity);

  mRenderUploadData.cameraPos = glm::vec4(cameraPos, 1.0f);
  mRenderUploadData.lightPos = lightPos;
  mRenderUploadData.lightColor = lightColor;
  mRenderUploadData.fogDensity = mRenderData.rdFogDensity;

  mRenderUploadData.compositeDebug = static_cast<int32_t>(mRenderData.rdCompositeDebug);
  mRenderUploadData.ssaoBlurEnabled = mRenderData.rdEnableSSAOBlur;

  mRenderUploadData.shadowMapEnabled = mRenderData.rdEnableShadowMap;
  mRenderUploadData.shadowMapPCFEnabled = mRenderData.rdEnableShadowMapPCF;
  mRenderUploadData.shadowMapPCFScale = mRenderData.rdShadowMapPCFScale;
  mRenderUploadData.shadowMapPCFRange = mRenderData.rdShadowMapPCFRange;
  mRenderUploadData.colorCascadeDebug = mRenderData.rdEnableShadowMapColorCascadeDebug;

  mRenderUploadData.ssaoRadius = mRenderData.rdSSAORadius;
  mRenderUploadData.ssaoBias = mRenderData.rdSSAOBias;
  mRenderUploadData.ssaoExponent = static_cast<float>(mRenderData.rdSSAOExponent);
  mRenderUploadData.ssaoBlurRadius = static_cast<float>(mRenderData.rdSSAOBlurRadius);

  updateShaderLightData();
  updateShadowMapCascades();

  mRenderData.rdMatrixGenerateTime += mRenderData.rdMatrixGenerateTimer.stop();

  // we need to update descriptors after the upload if buffer size changed
  mRenderData.rdUploadToUBOTimer.start();
  UniformBuffer::uploadData(mRenderData, mRenderData.rdRenderUploadDataUBOs.at(mRenderData.currentFrame), mRenderUploadData);
  bool bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdShaderModelRootMatrixBuffers.at(mRenderData.currentFrame), mWorldPosMatrices);

  // light data uses different descriptors
  bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdDynamicLightBuffers.at(mRenderData.currentFrame), mRenderData.rdLightData);
  // reuse the sphere drawing shader for light debug
  bufferResized |= ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdDynamicLightDebugBuffers.at(mRenderData.currentFrame), mRenderData.rdLightDebugData);

  // TODO: Check if the descripto sets are okay
  if (bufferResized) {
    VkHelper::updateDescriptorSets(mRenderData);
    VkHelper::updateImageDescriptorSets(mRenderData);
    VkHelper::updateLevelDescriptorSets(mRenderData);
  }

  mRenderData.rdUploadToUBOTime += mRenderData.rdUploadToUBOTimer.stop();

  return true;
}

bool VkRenderer::endRendering() {
  if (!CommandBuffer::end(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame))) {
    Logger::log(1, "%s error: failed to end ImGui command buffer\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkRenderer::submitGraphics() {
  // submit command buffer
  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  std::vector<VkSemaphore> waitSemaphores{};
  std::vector<VkPipelineStageFlags> waitStages{};
  if (!mRenderData.rdSkipDesktopMirror) {
    waitSemaphores = { mRenderData.rdPresentSemaphores.at(mRenderData.currentFrame) };
    waitStages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
  }

  std::vector<VkSemaphore> signalSemaphores = { mRenderData.rdRenderSemaphores[mImageIndex] };

  submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
  submitInfo.pSignalSemaphores = signalSemaphores.data();

  std::vector<VkCommandBuffer> commandBuffers =
    { mRenderData.rdCommandBuffers.at(mRenderData.currentFrame) };

  submitInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
  submitInfo.pCommandBuffers = commandBuffers.data();

  VkResult result = vkQueueSubmit(mRenderData.rdGraphicsQueue, 1, &submitInfo, mRenderData.rdRenderFences.at(mRenderData.currentFrame));
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: failed to submit draw command buffer (%i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VkRenderer::updateXRControllerPositions(std::array<glm::mat4, 2> &transformMatrix) {
  std::shared_ptr<Camera> cam = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);

  glm::mat4 camTranspose = glm::translate(glm::mat4(1.0f), cam->getWorldPosition());
  glm::mat4 camOrientation = glm::mat4(glm::mat3(cam->getViewMatrix()));

  for (int i = 0; i < 2; ++i) {
    mVRHandWorldPosMatrices.at(i) = camTranspose * transformMatrix.at(i);
  }

  bool bufferResized = ShaderStorageBuffer::uploadSsboData(mRenderData, mRenderData.rdVRControllerModelRootMatrixBuffers.at(mRenderData.currentFrame), mVRHandWorldPosMatrices);
  if (bufferResized) {
    VkHelper::updateDescriptorSets(mRenderData);
  }

  return true;
}

bool VkRenderer::moveCamera(glm::vec3 amount) {
  std::shared_ptr<Camera> camera = mModelInstCamData.micCameras.at(mModelInstCamData.micSelectedCamera);
  CameraSettings camSettings = camera->getCameraSettings();

  camSettings.csWorldPosition.x += amount.x * 0.25f;
  camSettings.csWorldPosition.y += amount.y * 0.25f;
  camSettings.csWorldPosition.z += amount.z * 0.25f;

  camera->setCameraSettings(camSettings);

  return true;
}

void VkRenderer::setXRVisibilityMask(XRVisibilityMask visibilityMask) {
  for (int i = 0; i < 2; ++i) {
    if (visibilityMask.indices.at(i).empty() || visibilityMask.vertices.at(i).vertices.empty()) {
      mNumVisMaskTriangles.at(i) = 0;
      Logger::log(1, "%s: visibility mask %i is empty, skipping buffer allocation\n", __FUNCTION__, i);
      continue;
    }

    VertexBuffer::init(mRenderData, mVisibilityMaskVertices.at(i), visibilityMask.vertices.at(i).vertices.size() * sizeof(VkSimpleVertex));
    VertexBuffer::uploadData(mRenderData, mVisibilityMaskVertices.at(i), visibilityMask.vertices.at(i).vertices);

    IndexBuffer::init(mRenderData, mVisibilityMaskIndices.at(i), visibilityMask.indices.at(i).size() * sizeof(uint32_t));
    IndexBuffer::uploadData(mRenderData, mVisibilityMaskIndices.at(i), visibilityMask.indices.at(i));

    mNumVisMaskTriangles.at(i) = visibilityMask.indices.at(i).size();

    Logger::log(1, "%s: visibility mask %i will draw from %i triangles\n", __FUNCTION__, i, mNumVisMaskTriangles.at(i));
  }
}

void VkRenderer::drawXRVisibilityMask() {
  VkDeviceSize offset = 0;

  vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdXRVisMaskMeshPipeline);
  VkHelper::disableBlending(mRenderData, 2);

  vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS,
    mRenderData.rdLinePipelineLayout, 0, 1, &mRenderData.rdGroundMeshDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

  for (uint32_t layer = 0; layer < 2; layer++) {
    if (mNumVisMaskTriangles.at(layer) == 0) {
      continue;
    }
    mRenderData.rdModelData.pkVirtMaskLayer = layer;
    vkCmdPushConstants(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), mRenderData.rdLinePipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mRenderData.rdModelData);

    vkCmdBindVertexBuffers(mRenderData.rdCommandBuffers[mRenderData.currentFrame], 0, 1, &mVisibilityMaskVertices.at(layer).buffer, &offset);
    vkCmdBindIndexBuffer(mRenderData.rdCommandBuffers[mRenderData.currentFrame], mVisibilityMaskIndices.at(layer).buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(mRenderData.rdCommandBuffers[mRenderData.currentFrame], mNumVisMaskTriangles.at(layer), 1, 0, 0, 0);
  }
}

bool VkRenderer::copyToXRSwapchain(VkImageView imageView) {
  // copy to Swapchain
  VkClearValue colorClearValue;
  colorClearValue.color = { { 0.25f, 0.25f, 0.25f, 1.0f } };

  VkViewport swapchainCopyViewport{};
  swapchainCopyViewport.x = 0.0f;
  swapchainCopyViewport.y = 0.0f;
  swapchainCopyViewport.width = static_cast<float>(mRenderData.rdXRWidth);
  swapchainCopyViewport.height = static_cast<float>(mRenderData.rdXRHeight);
  swapchainCopyViewport.minDepth = 0.0f;
  swapchainCopyViewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = { 0, 0 };
  scissor.extent = VkExtent2D{mRenderData.rdXRWidth, mRenderData.rdXRHeight};

  vkCmdSetViewport(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &swapchainCopyViewport);
  vkCmdSetScissor(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 0, 1, &scissor);

  VkRenderingAttachmentInfo swapchainAttachmentInfo {};
  swapchainAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  swapchainAttachmentInfo.imageView = imageView;
  swapchainAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  swapchainAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  swapchainAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  swapchainAttachmentInfo.clearValue = colorClearValue;

  std::vector<VkRenderingAttachmentInfo> swapchainCopyAttachmentInfos {
    swapchainAttachmentInfo,
  };

  VkRect2D swapchainRenderArea = VkRect2D{VkOffset2D{}, VkExtent2D{mRenderData.rdXRWidth, mRenderData.rdXRHeight}};

  VkRenderingInfo swapchainCopyRenderInfo{};
  swapchainCopyRenderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  swapchainCopyRenderInfo.renderArea = swapchainRenderArea;
  swapchainCopyRenderInfo.layerCount = 2;
  swapchainCopyRenderInfo.viewMask = kXRPipelineViewMask;
  swapchainCopyRenderInfo.colorAttachmentCount = static_cast<uint32_t>(swapchainCopyAttachmentInfos.size());
  swapchainCopyRenderInfo.pColorAttachments = swapchainCopyAttachmentInfos.data();

  vkCmdBeginRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), &swapchainCopyRenderInfo);

  vkCmdBindPipeline(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdXRSwapchainCopyPipeline);

  vkCmdBindDescriptorSets(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), VK_PIPELINE_BIND_POINT_GRAPHICS, mRenderData.rdSwapchainCopyPipelineLayout, 0, 1,
    &mRenderData.rdSwapchainCopyDescriptorSets.at(mRenderData.currentFrame), 0, nullptr);

  vkCmdDraw(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame), 3, 1, 0, 0);

  vkCmdEndRendering(mRenderData.rdCommandBuffers.at(mRenderData.currentFrame));

  return true;
}

bool VkRenderer::checkForSelection() {
  // we must wait for the image to be created before we can pick 
  if (mRenderData.rdApplicationMode == appMode::edit) {
    if (mMousePick) {
      // wait for queue to be idle
      vkQueueWaitIdle(mRenderData.rdGraphicsQueue);

#if !defined(__ANDROID__)
      float xScale, yScale;
      glfwGetWindowContentScale(mRenderData.rdWindow, &xScale, &yScale);
      float selectedInstanceId = VkHelper::getPixelValueFromPos(mRenderData, mRenderData.rdSelectionImageData.image, mMouseXPos * xScale, mMouseYPos * yScale);
#else
      float selectedInstanceId = VkHelper::getPixelValueFromPos(mRenderData, mRenderData.rdSelectionImageData.image, static_cast<float>(mMouseXPos), static_cast<float>(mMouseYPos));
#endif

      mModelInstCamData.micSelectedInstance = 0;
      mModelInstCamData.micSelectedDynLight = 0;

      int instanceId = static_cast<int>(selectedInstanceId);
      if (instanceId >= 0) {
        if (instanceId >= LIGHT_OBJECT_OFFSET) {
          mModelInstCamData.micSelectedDynLight = instanceId - LIGHT_OBJECT_OFFSET;
        } else{
          mModelInstCamData.micSelectedInstance = instanceId;
        }
      }

      Logger::log(1, "%s: selected instance %i/light %i (raw: %f)\n", __FUNCTION__, mModelInstCamData.micSelectedInstance, mModelInstCamData.micSelectedDynLight, selectedInstanceId);

      mModelInstCamData.micSettingsContainer->applySelectInstance(mModelInstCamData.micSelectedInstance, mSavedSelectedInstanceId);
      mMousePick = false;
    }
  }

  return true;
}

bool VkRenderer::presentDesktopImage() {
  if (mRenderData.rdSkipDesktopMirror) {
    return true;
  }

  // trigger swapchain image presentation
  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &mRenderData.rdRenderSemaphores.at(mImageIndex);

  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &mRenderData.rdVkbSwapchain.swapchain;

  presentInfo.pImageIndices = &mImageIndex;

  VkResult result = vkQueuePresentKHR(mRenderData.rdPresentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    return recreateSwapchain();
  } else {
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to present swapchain image\n", __FUNCTION__);
      return false;
    }
  }

  return true;
}

bool VkRenderer::finishDraw() {
  mRenderData.currentFrame = (mRenderData.currentFrame + 1) % mRenderData.rdNumFramesInFlight;

  return true;
}

void VkRenderer::cleanup() {
  VkResult result = vkDeviceWaitIdle(mRenderData.rdVkbDevice.device);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
  }

  for (int i = 0; i < 2; ++i) {
    IndexBuffer::cleanup(mRenderData, mVisibilityMaskIndices.at(i));
    VertexBuffer::cleanup(mRenderData, mVisibilityMaskVertices.at(i));
  }

  // delete models and levels to destroy Vulkan objects
  for (const auto& model : mModelInstCamData.micModelList) {
    model->cleanup(mRenderData);
  }

  for (const auto& model : mModelInstCamData.micPendingDeleteAssimpModels) {
    model->cleanup(mRenderData);
  }

  for (const auto& level : mModelInstCamData.micLevels) {
    level->cleanup(mRenderData);
  }

  for (const auto& level : mModelInstCamData.micPendingDeleteAssimpLevels) {
    level->cleanup(mRenderData);
  }

  mModelInstCamData.micDynLights.erase(mModelInstCamData.micDynLights.begin(), mModelInstCamData.micDynLights.end());

  mRHandVRControllerModel->cleanup(mRenderData);
  mLHandVRControllerModel->cleanup(mRenderData);
  mLightModel->cleanup(mRenderData);

#if !defined(__ANDROID__)
  mUserInterface.cleanup(mRenderData);
#endif

  VkHelper::cleanupSSAONoiseTexture(mRenderData);

  VkHelper::cleanup(mRenderData);
}

#if defined(__ANDROID__)
void VkRenderer::setXRContext(uintptr_t instanceHandle, uint64_t systemId) {
  mRenderData.rdXRInstanceHandle = instanceHandle;
  mRenderData.rdXRSystemId = systemId;
}

bool VkRenderer::createVulkanPipelines() {
  if (!VkHelper::createPipelines(mRenderData)) {
    return false;
  }
  return true;
}
#endif


