#include <sstream>
#include <cstring>
#include <algorithm>

#include <VRHeadset.h>
#include <glm/gtx/string_cast.hpp>

#include <Logger.h>

#if defined(__ANDROID__)
#include <Platform.h>
#include <android_native_app_glue.h>
#endif

#if defined(__ANDROID__)
bool VRHeadset::init(void* window, ModelInstanceCamCallbacks callbacks) {
#else
bool VRHeadset::init(GLFWwindow* window, ModelInstanceCamCallbacks callbacks) {
#endif
  Logger::log(1, "%s: VR Headset init start\n", __FUNCTION__);
#if !defined(__ANDROID__)
  if (!window) {
    Logger::log(1, "%s error: invalid GLFWwindow handle\n", __FUNCTION__);
    return false;
  }
#endif

#if defined(__ANDROID__)
  if (!initializeAndroidLoader()) {
    return false;
  }
#endif

  if (!createXRInstance()) {
    return false;
  }

  if (!createXRDebugMessenger()) {
    return false;
  }

  if (!getInstanceID()) {
    return false;
  }

  if (!getSystemID()) {
    return false;
  }

  if (!createXRActionSet()) {
    return false;
  }

  if (!suggestXRBindings()) {
    return false;
  }

  if (!getViewConfigViews()) {
    return false;
  }

  if (!getEnvBlendModes()) {
    return false;
  }

  if (!getVulkanGraphicsRequirements()) {
    return false;
  }

  if (!getVKDeviceExtensions()) {
    return false;
  }

  if (!getVKInstanceExtension()) {
    return false;
  }

  mRenderer = std::make_shared<VkRenderer>();
  mRenderer->setRendererMICCallbacks(callbacks);
  mRenderer->setXRSize(mWidth, mHeight);

#if defined(__ANDROID__)
  mRenderer->setXRContext(reinterpret_cast<uintptr_t>(mXRInstance), static_cast<uint64_t>(mSystemID));
  if (!mRenderer->init(nullptr, mVKDeviceExtensionsForXR, mVKInstanceExtensionsForXR)) {
#else
  if (!mRenderer->init(window, mVKDeviceExtensionsForXR, mVKInstanceExtensionsForXR)) {
#endif
    return false;
  }

  mVulkanDevice = mRenderer->getDevice();

  if (!createXRSession()) {
    return false;
  }

#if defined(__ANDROID__)
  if (!mRenderer->createVulkanPipelines()) {
    Logger::log(1, "%s error: could not create Vulkan pipelines after OpenXR session\n", __FUNCTION__);
    return false;
  }
#endif

  if (!getVisibilityMask()) {
    return false;
  }

  if (!createXRActionPoses()) {
    return false;
  }

  if (!attachActionSet()) {
    return false;
  }

  if (!createXRReferenceSpace()) {
    return false;
  }

  if (!createXRSwapchain()) {
    return false;
  }

  if (!mRenderer->createXRPipeline(mSwapchainFormat)) {
    return false;
  }

  Logger::log(1, "%s: VR Headset init success\n", __FUNCTION__);

  return true;
}

void VRHeadset::cleanup() {
  VkResult result = vkDeviceWaitIdle(mVulkanDevice);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
  }

  mRenderer->destroyXRPipeline();

  destroyXRSwapchain();
  destroyXRReferenceSpace();
  destroyXRSession();

  mRenderer->cleanup();

  destroyXRDebugMessenger();
  destroyXRInstance();
}

bool VRHeadset::isXRSessionRunning() {
  return mSessionRunning;
}

bool VRHeadset::isXRApplicationRunning() {
  return mApplicationRunning;
}

std::shared_ptr<VkRenderer> VRHeadset::getVulkanRenderer() {
  return mRenderer;
}

void VRHeadset::pollEvents() {
  XrEventDataBuffer eventData{};
  eventData.type = XR_TYPE_EVENT_DATA_BUFFER;

  auto XrPollEvents = [&]() -> bool {
    eventData = {};
    eventData.type = XR_TYPE_EVENT_DATA_BUFFER;

    return xrPollEvent(mXRInstance, &eventData) == XR_SUCCESS;
  };

  while (XrPollEvents()) {
    switch (eventData.type) {
      case XR_TYPE_EVENT_DATA_EVENTS_LOST:
      {
        XrEventDataEventsLost *eventsLost = reinterpret_cast<XrEventDataEventsLost*>(&eventData);
        Logger::log(1, "%s warning: Lost %i events\n", __FUNCTION__, eventsLost->lostEventCount);
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      {
        XrEventDataInstanceLossPending *instanceLossPending = reinterpret_cast<XrEventDataInstanceLossPending*>(&eventData);
        Logger::log(1, "%s error: Instance loss pending at timestamp %lli\n", __FUNCTION__, instanceLossPending->lossTime);
        mSessionRunning = false;
        mApplicationRunning = false;
        break;
      }
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
      {
        XrEventDataInteractionProfileChanged *interactionProfileChanged = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&eventData);
        Logger::log(1, "%s info: Interaction profile changed for session %p\n", __FUNCTION__, interactionProfileChanged->session);
        if (interactionProfileChanged->session != mSession) {
          Logger::log(1, "%s error: XrEventDataInteractionProfileChanged for unknown session?!\n", __FUNCTION__);
          break;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
      {
        XrEventDataReferenceSpaceChangePending *refSpaceChangePending = reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&eventData);
        Logger::log(1, "%s info: Interaction profile changed for session %p\n", __FUNCTION__, refSpaceChangePending->session);
        if (refSpaceChangePending->session != mSession) {
          Logger::log(1, "%s error: XrEventDataReferenceSpaceChangePending for unknown session?!\n", __FUNCTION__);
          break;
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
      {
        XrEventDataSessionStateChanged *sessionStateChanged = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
        if (sessionStateChanged->session != mSession) {
          Logger::log(1, "%s error: XrEventDataSessionStateChanged for unknown session?!\n", __FUNCTION__);
          break;
        }

        switch(sessionStateChanged->state) {
          case XR_SESSION_STATE_READY:
          {
            XrSessionBeginInfo sessionInfo{};
            sessionInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
            sessionInfo.primaryViewConfigurationType = mViewConfiguration;

            XrResult result = xrBeginSession(mSession, &sessionInfo);
            if (result != XR_SUCCESS) {
              Logger::log(1, "%s error: Failed to begin Session\n", __FUNCTION__);
              break;
            }
            mSessionRunning = true;
          }
          case XR_SESSION_STATE_STOPPING:
          {
            XrResult result = xrEndSession(mSession);
            if (result != XR_SUCCESS) {
              Logger::log(1, "%s error: Failed to end Session\n", __FUNCTION__);
              break;
            }
            mSessionRunning = false;
          }
          case XR_SESSION_STATE_EXITING:
          {
            mSessionRunning = false;
            mApplicationRunning = false;
          }
          case XR_SESSION_STATE_LOSS_PENDING:
          {
            // XXX: Recovery may be possible but we stop for now
            mSessionRunning = false;
            mApplicationRunning = false;
          }
          default:
            Logger::log(1, "%s warning: Found unhandled session change state %i\n", __FUNCTION__, sessionStateChanged->state);
            break;
        }

        // Store state for use in the application
        mSessionState = sessionStateChanged->state;
        break;
      }
      default:
        Logger::log(1, "%s warning: Found unhandled event %i\n", __FUNCTION__, eventData.type);
        break;
    }
  }
}

bool VRHeadset::draw(float deltaTime) {
  if (!mRenderer->initDraw(deltaTime)) {
    return false;
  }

  if (!mRenderer->acquireDesktopImage()) {
    return false;
  }

  if (!mRenderer->updateLevelAndModels(deltaTime)) {
    return false;
  }

  if (!beginXRFrame()) {
    return false;
  }

  // Camera update
  std::tie(mNearPlane, mFarPlane) = mRenderer->getNearAndFarPlane();
  createXRCameraMatrices();
  if (!mRenderer->updateCamera(mProjViewMatrices, deltaTime)) {
    return false;
  }

  calculateXRHandPositions();
  if (!mRenderer->updateXRControllerPositions(mHandTransformMatrices)) {
    return false;
  }

  if (!mRenderer->renderGraphics()) {
    return false;
  }

  if (!renderXRFrame()) {
    return false;
  }

  if (!mRenderer->endRendering()) {
    return false;
  }

  if (!mRenderer->submitGraphics()) {
    return false;
  }

  if (!endXRFrame()) {
    return false;
  }

  if (!mRenderer->checkForSelection()) {
    return false;
  }

  if (!mRenderer->presentDesktopImage()) {
    return false;
  }

  if (!mRenderer->finishDraw()) {
    return false;
  }

  return true;
}


bool VRHeadset::createXRInstance() {
  std::strncpy(mXRAppInfo.applicationName, "Mastering C++ Game Animation Programming - VR", XR_MAX_APPLICATION_NAME_SIZE);
  mXRAppInfo.applicationVersion = 1;
  std::strncpy(mXRAppInfo.engineName, "Michael's Engine", XR_MAX_ENGINE_NAME_SIZE);
  mXRAppInfo.engineVersion = 1;
#if defined(__ANDROID__)
  mXRAppInfo.apiVersion = XR_MAKE_VERSION(1, 1, 0);
#else
  // Steam still has no support for API v1.1
  mXRAppInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
#endif

  // Get API Layers
  uint32_t apiLayerCount = 0;
  std::vector<XrApiLayerProperties> apiLayerProperties;

  XrResult result = xrEnumerateApiLayerProperties(0, &apiLayerCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1,"%s error: Failed to enumerate ApiLayerProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: found %i API layers\n", __FUNCTION__, apiLayerCount);

  apiLayerProperties.resize(apiLayerCount, {XR_TYPE_API_LAYER_PROPERTIES});
  result = xrEnumerateApiLayerProperties(apiLayerCount, &apiLayerCount, apiLayerProperties.data());
  if (result != XR_SUCCESS) {
    Logger::log(1,"%s error: Failed to enumerate ApiLayerProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (auto &layerProperty : apiLayerProperties) {
    Logger::log(1, "%s: -- Found layer %s (version %i)\n", __FUNCTION__, std::string(layerProperty.layerName).c_str(), layerProperty.layerVersion);
  }

  // Activate Layers
  Logger::log(1, "%s: Trying to activate layers\n", __FUNCTION__);
#if !defined(__ANDROID__)
  mAPILayers.push_back("XR_APILAYER_LUNARG_core_validation");
#endif

  for (auto &requestedAPILayer : mAPILayers) {
    bool found = false;
    for (auto &apiLayer : apiLayerProperties) {
      // strcmp returns 0 if the strings match.
      if (strcmp(requestedAPILayer.c_str(), apiLayer.layerName) != 0) {
        continue;
      } else {
        mActiveAPILayers.push_back(requestedAPILayer.c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      Logger::log(1, "%s: -- Failed to find OpenXR API Layer %s\n", __FUNCTION__, requestedAPILayer.c_str());
    }
  }

  for (auto &layer : mActiveAPILayers) {
    Logger::log(1, "%s: -- Active APY Layer: %s\n", __FUNCTION__, layer);
  }

  // Get all extensions
  uint32_t extensionCount = 0;
  std::vector<XrExtensionProperties> extensionProperties;

  result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate InstanceExtensionProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: found %i extensions\n", __FUNCTION__, extensionCount);

  extensionProperties.resize(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensionProperties.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate InstanceExtensionProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (auto &ext : extensionProperties) {
    Logger::log(1, "%s: -- Found extension %s (version %i)\n", __FUNCTION__, std::string(ext.extensionName).c_str(), ext.extensionVersion);
  }

  // Activate extensions
  Logger::log(1, "%s: Trying to activate extensions\n", __FUNCTION__);
  mInstanceExtensions.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
  mInstanceExtensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
#if defined(__ANDROID__)
  mInstanceExtensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
  mInstanceExtensions.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
#endif
  mInstanceExtensions.push_back(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);

  for (auto &requestedInstanceExtension : mInstanceExtensions) {
    bool found = false;
    for (auto &extensionProperty : extensionProperties) {
      // strcmp returns 0 if the strings match.
      if (strcmp(requestedInstanceExtension.c_str(), extensionProperty.extensionName) != 0) {
        continue;
      } else {
        mActiveInstanceExtensions.push_back(requestedInstanceExtension.c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      Logger::log(1, "%s: -- Failed to find OpenXR instance extension %s\n", __FUNCTION__, requestedInstanceExtension.c_str());
    }
  }

  for (auto &ext : mActiveInstanceExtensions) {
    Logger::log(1, "%s: -- Active extension: %s\n", __FUNCTION__, ext);
  }

  // Create instance
  XrInstanceCreateInfo instanceInfo{};
  instanceInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.createFlags = 0;
  instanceInfo.applicationInfo = mXRAppInfo;
  instanceInfo.enabledApiLayerCount = static_cast<uint32_t>(mActiveAPILayers.size());
  instanceInfo.enabledApiLayerNames = mActiveAPILayers.data();
  instanceInfo.enabledExtensionCount = static_cast<uint32_t>(mActiveInstanceExtensions.size());
  instanceInfo.enabledExtensionNames = mActiveInstanceExtensions.data();

#if defined(__ANDROID__)
  XrInstanceCreateInfoAndroidKHR androidCreateInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
  androidCreateInfo.applicationVM = mJavaVM;
  androidCreateInfo.applicationActivity = mActivityContext;
  instanceInfo.next = reinterpret_cast<XrBaseInStructure *>(&androidCreateInfo);
#endif

  result = xrCreateInstance(&instanceInfo, &mXRInstance);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Instance (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::getInstanceID() {
  // Instance info
  XrInstanceProperties instProperties{};
  instProperties.type = XR_TYPE_INSTANCE_PROPERTIES;
  XrResult result = xrGetInstanceProperties(mXRInstance, &instProperties);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get InstanceProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: OpenXR Runtime %s, version %i.%i.%i\n", __FUNCTION__,
    instProperties.runtimeName,
    XR_VERSION_MAJOR(instProperties.runtimeVersion),
    XR_VERSION_MINOR(instProperties.runtimeVersion),
    XR_VERSION_PATCH(instProperties.runtimeVersion)
  );

  return true;
}

bool VRHeadset::getSystemID() {
  // System info
  XrSystemGetInfo systemInfo{};
  systemInfo.type = XR_TYPE_SYSTEM_GET_INFO;
  systemInfo.formFactor = mFormFactor;

  XrResult result = xrGetSystem(mXRInstance, &systemInfo, &mSystemID);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get SystemID (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Get the System's properties for some general information about the hardware and the vendor.
  result = xrGetSystemProperties(mXRInstance, mSystemID, &mSystemProperties);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get SystemProperties (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found system '%s'\n", __FUNCTION__, mSystemProperties.systemName);

  return true;
}

bool VRHeadset::getViewConfigViews() {
  uint32_t viewConfCount = 0;
  XrResult result = xrEnumerateViewConfigurations(mXRInstance, mSystemID, 0, &viewConfCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configurations (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i view configs\n", __FUNCTION__, viewConfCount);

  mViewConfigurations.resize(viewConfCount);
  result = xrEnumerateViewConfigurations(mXRInstance, mSystemID, viewConfCount, &viewConfCount, mViewConfigurations.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configurations (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Pick first supported view config
  for (const XrViewConfigurationType &viewConfiguration : mApplicationViewConfigurations) {
    if (std::find(mViewConfigurations.begin(), mViewConfigurations.end(), viewConfiguration) != mViewConfigurations.end()) {
      mViewConfiguration = viewConfiguration;
      break;
    }
  }

  if (mViewConfiguration == XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM) {
    Logger::log(1, "%s: Failed to find a view configuration type. Defaulting to XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO.", __FUNCTION__);
    mViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  }

  Logger::log(1, "%s: Using view configuration %i\n", __FUNCTION__, mViewConfiguration);

  uint32_t viewConfViewCount = 0;
  result = xrEnumerateViewConfigurationViews(mXRInstance, mSystemID, mViewConfiguration, 0, &viewConfViewCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configuration views (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i view confuration views\n", __FUNCTION__, viewConfViewCount);
  
  mViewConfigurationViews.resize(viewConfViewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
  result = xrEnumerateViewConfigurationViews(mXRInstance, mSystemID, mViewConfiguration, viewConfViewCount, &viewConfViewCount, mViewConfigurationViews.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate view configuration views (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  mWidth = mViewConfigurationViews.at(0).recommendedImageRectWidth;
  mHeight = mViewConfigurationViews.at(0).recommendedImageRectHeight;

  mViews.resize(mViewConfigurationViews.size(), {XR_TYPE_VIEW});

  return true;
}

bool VRHeadset::getEnvBlendModes() {
  uint32_t envBlendModeCount = 0;
  XrResult result = xrEnumerateEnvironmentBlendModes(mXRInstance, mSystemID, mViewConfiguration, 0, &envBlendModeCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate blend modes (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Found %i blend modes\n", __FUNCTION__, envBlendModeCount);
  
  mEnvironmentBlendModes.resize(envBlendModeCount);
  result = xrEnumerateEnvironmentBlendModes(mXRInstance, mSystemID, mViewConfiguration, envBlendModeCount, &envBlendModeCount, mEnvironmentBlendModes.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate blend modes (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  for (const XrEnvironmentBlendMode &environmentBlendMode : mApplicationEnvironmentBlendModes) {
    if (std::find(mEnvironmentBlendModes.begin(), mEnvironmentBlendModes.end(), environmentBlendMode) != mEnvironmentBlendModes.end()) {
      mEnvironmentBlendMode = environmentBlendMode;
      break;
    }
  }
  if (mEnvironmentBlendMode == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM) {
    Logger::log(1, "%s warning: Failed to find a compatible blend mode. Defaulting to XR_ENVIRONMENT_BLEND_MODE_OPAQUE.", __FUNCTION__);
    mEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  }

  Logger::log(1, "%s: Using blend mode %i\n", __FUNCTION__, mEnvironmentBlendMode);

  return true;
}

bool VRHeadset::getVisibilityMask() {
  PFN_xrGetVisibilityMaskKHR xrGetVisibilityMaskKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVisibilityMaskKHR", (PFN_xrVoidFunction *)&xrGetVisibilityMaskKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVisibilityMaskKHR (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  XRVisibilityMask visibilityMask{};

  for (uint32_t i = 0; i < mViewConfigurationViews.size(); ++i) {
    XrVisibilityMaskTypeKHR visMaskType = XrVisibilityMaskTypeKHR::XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR;
    XrVisibilityMaskKHR visMask{};
    visMask.type = XR_TYPE_VISIBILITY_MASK_KHR;

    result = xrGetVisibilityMaskKHR(mSession, mViewConfiguration, i, visMaskType, &visMask);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to get visibility mask (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    Logger::log(1, "%s: visibility mask for eye %i has %i vertices and %i indices\n", __FUNCTION__, i, visMask.vertexCountOutput, visMask.indexCountOutput);

    std::vector<XrVector2f> vertices{};
    vertices.resize(visMask.vertexCountOutput);
    visMask.vertexCapacityInput = static_cast<uint32_t>(visMask.vertexCountOutput);
    visMask.vertices = vertices.data();

    std::vector<uint32_t> indices{};
    indices.resize(visMask.indexCountOutput);
    visMask.indexCapacityInput = static_cast<uint32_t>(visMask.indexCountOutput);
    visMask.indices = indices.data();

    result = xrGetVisibilityMaskKHR(mSession, mViewConfiguration, i, visMaskType, &visMask);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to get visibility mask (error code: %i)\n", __FUNCTION__, result);
      return false;
    }

    VkSimpleMesh mesh{};
    for (int j = 0; j < visMask.vertexCountOutput; ++j) {
      VkSimpleVertex vertex({ vertices[j].x, vertices[j].y, -1.0f}, {0.0f, 0.0f, 0.0f});
      mesh.vertices.push_back(vertex);
    }

    visibilityMask.vertices.at(i) = mesh;
    visibilityMask.indices.at(i) = indices;
  }

  mRenderer->setXRVisibilityMask(visibilityMask);

  return true;
}

bool VRHeadset::getVulkanGraphicsRequirements() {
  PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsRequirementsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanGraphicsRequirementsKHR (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrGetVulkanGraphicsRequirementsKHR(mXRInstance, mSystemID, &mGraphicsRequirements);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan graphics requirements (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Minimum OpenXR API Version is %i.%i.%i\n", __FUNCTION__,
    XR_VERSION_MAJOR(mGraphicsRequirements.minApiVersionSupported),
    XR_VERSION_MINOR(mGraphicsRequirements.minApiVersionSupported),
    XR_VERSION_PATCH(mGraphicsRequirements.minApiVersionSupported));

  Logger::log(1, "%s: Maximum OpenXR API Version is %i.%i.%i\n", __FUNCTION__,
    XR_VERSION_MAJOR(mGraphicsRequirements.maxApiVersionSupported),
    XR_VERSION_MINOR(mGraphicsRequirements.maxApiVersionSupported),
    XR_VERSION_PATCH(mGraphicsRequirements.maxApiVersionSupported));

  return true;
}


bool VRHeadset::createXRSession() {
  VkPhysicalDevice vulkanPhysDevice = mRenderer->getPhysicalDevice();
  VkInstance vulkanInstance = mRenderer->getInstance();

  PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsDeviceKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanGraphicsDeviceKHR (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrGetVulkanGraphicsDeviceKHR(mXRInstance, mSystemID, vulkanInstance, &mPhysicalDevice);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get OpenXC physical device (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  if (vulkanPhysDevice != mPhysicalDevice) {
    Logger::log(1, "%s warning: OpenXR physial device is different from Vulkan physical device\n", __FUNCTION__);
    mRenderer->setPhysicalDevice(mPhysicalDevice);
  }

  XrGraphicsBindingVulkanKHR xRGraphicsBinding{};
  xRGraphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
  xRGraphicsBinding.instance = vulkanInstance;
  xRGraphicsBinding.physicalDevice = vulkanPhysDevice;
  xRGraphicsBinding.device = mVulkanDevice;

  std::tie(xRGraphicsBinding.queueFamilyIndex, xRGraphicsBinding.queueIndex) = mRenderer->getQueueFamilyAndIndex();

  XrSessionCreateInfo sessionInfo{};
  sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
  sessionInfo.createFlags = 0;
  sessionInfo.systemId = mSystemID;
  sessionInfo.next = &xRGraphicsBinding;

  result = xrCreateSession(mXRInstance, &sessionInfo, &mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Session (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: OpenXR session created\n", __FUNCTION__);
  return true;
}

bool VRHeadset::createXRDebugMessenger() {
  XrDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
  debugMessengerCreateInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  debugMessengerCreateInfo.messageSeverities =
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  debugMessengerCreateInfo.messageTypes =
    XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
    XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
  debugMessengerCreateInfo.userCallback = handleXRErrors;
  debugMessengerCreateInfo.userData = nullptr;

  PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)&xrCreateDebugUtilsMessengerEXT);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrCreateDebugUtilsMessengerEXT (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  result = xrCreateDebugUtilsMessengerEXT(mXRInstance, &debugMessengerCreateInfo, &mDebugMessenger);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create debug messenger (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Debug utils manager created (%p)\n", __FUNCTION__, mDebugMessenger);
  return true;
}

bool VRHeadset::getVKDeviceExtensions() {
  PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanDeviceExtensionsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanDeviceExtensionsKHR\n",  __FUNCTION__, result);
    return false;
  }

  uint32_t extensionNamesSize = 0;
  result = xrGetVulkanDeviceExtensionsKHR(mXRInstance, mSystemID, 0, &extensionNamesSize, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get number of Vulkan device extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::vector<char> extensionNames(extensionNamesSize);
  result = xrGetVulkanDeviceExtensionsKHR(mXRInstance, mSystemID, extensionNamesSize, &extensionNamesSize, extensionNames.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::stringstream streamData(extensionNames.data());
  std::string extension;
  while (std::getline(streamData, extension, ' ')) {
    mVKDeviceExtensionsForXR.push_back(extension);
    Logger::log(1, "%s: Found Vulkan device extension %s\n", __FUNCTION__, extension.c_str());
  }

  return true;
}

bool VRHeadset::getVKInstanceExtension() {
  PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanInstanceExtensionsKHR);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrGetVulkanInstanceExtensionsKHR\n",  __FUNCTION__, result);
    return false;
  }

  uint32_t extensionNamesSize = 0;
  result = xrGetVulkanInstanceExtensionsKHR(mXRInstance, mSystemID, 0, &extensionNamesSize, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get number of Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::vector<char> extensionNames(extensionNamesSize);
  result = xrGetVulkanInstanceExtensionsKHR(mXRInstance, mSystemID, extensionNamesSize, &extensionNamesSize, extensionNames.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get Vulkan instance extensions\n",  __FUNCTION__, result);
    return false;
  }

  std::stringstream streamData(extensionNames.data());
  std::string extension;
  while (std::getline(streamData, extension, ' ')) {
    mVKInstanceExtensionsForXR.push_back(extension);
    Logger::log(1, "%s: Found Vulkan instance extension %s\n", __FUNCTION__, extension.c_str());
  }

  return true;
}

bool VRHeadset::createXRReferenceSpace() {
  // Fill out an XrReferenceSpaceCreateInfo structure and create a reference XrSpace, specifying a Local space with an identity pose as the origin.
  XrReferenceSpaceCreateInfo referenceSpaceCI{};
  referenceSpaceCI.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
  referenceSpaceCI.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  referenceSpaceCI.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

  XrResult result = xrCreateReferenceSpace(mSession, &referenceSpaceCI, &mLocalSpace);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create local reference space (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VRHeadset::createXRSwapchain() {
  uint32_t formatCount = 0;
  XrResult result = xrEnumerateSwapchainFormats(mSession, 0, &formatCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate number of spwachain formats (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  std::vector<int64_t> formats(formatCount);
  result = xrEnumerateSwapchainFormats(mSession, formatCount, &formatCount, formats.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to enumerate spwachain formats (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  int64_t chosenFormat = formats.front();

  for (int64_t format : formats) {
    if (format == VK_FORMAT_R8G8B8A8_UNORM) {
      chosenFormat = format;
      break;
    }
  }

  uint32_t viewCount = static_cast<uint32_t>(mViewConfigurationViews.size());

  XrSwapchainCreateInfo swapchainCreateInfo{};
  swapchainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
  swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  swapchainCreateInfo.format = chosenFormat;
  swapchainCreateInfo.sampleCount = mViewConfigurationViews.at(0).recommendedSwapchainSampleCount;
  swapchainCreateInfo.width = mViewConfigurationViews.at(0).recommendedImageRectWidth;
  swapchainCreateInfo.height = mViewConfigurationViews.at(0).recommendedImageRectHeight;

  swapchainCreateInfo.faceCount = 1;
  swapchainCreateInfo.arraySize = viewCount;
  swapchainCreateInfo.mipCount = 1;

  result = xrCreateSwapchain(mSession, &swapchainCreateInfo, &mSwapchain.swapchain);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create spwachain (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  mSwapchainFormat = static_cast<VkFormat>(chosenFormat);

  uint32_t swapchainImageCount = 0;
  result = xrEnumerateSwapchainImages(mSwapchain.swapchain, 0, &swapchainImageCount, nullptr);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get number of swapchain images (error code: %i)\n", __FUNCTION__, result);
  }

  Logger::log(1, "%s: OpenXR Swapchain has %i images\n", __FUNCTION__, swapchainImageCount);

  std::vector<XrSwapchainImageVulkanKHR> swapchainImages(swapchainImageCount, { .type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });

  result = xrEnumerateSwapchainImages(mSwapchain.swapchain, swapchainImageCount, &swapchainImageCount, (XrSwapchainImageBaseHeader*)swapchainImages.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get swapchain images (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  mSwapchain.swapchainImages = std::move(swapchainImages);

  for (uint32_t j = 0; j < swapchainImageCount; ++j) {
    VkImageViewCreateInfo imageViewCI{};
    imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCI.viewType =VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    imageViewCI.image = mSwapchain.swapchainImages.at(j).image;
    imageViewCI.format = mSwapchainFormat;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = viewCount;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageView imageView;
    VkResult vkresult = vkCreateImageView(mVulkanDevice, &imageViewCI, nullptr, &imageView);
    if (vkresult != VK_SUCCESS) {
      Logger::log(1, "%s error: Failed to create image view (error code: %i)\n", __FUNCTION__, vkresult);
      return false;
    }

    mSwapchain.swapchainImageViews.push_back(imageView);
  }

  Logger::log(1, "%s: OpenXR Swapchains created (%ix%i)\n", __FUNCTION__, mWidth, mHeight);
  return true;
}

XrPath VRHeadset::CreateXrPath(std::string pathString) {
  XrPath xrPath;

  XrResult result = xrStringToPath(mXRInstance, pathString.c_str(), &xrPath);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create XR Path (error code: %i)\n", __FUNCTION__, result);
  }

  return xrPath;
}

std::string VRHeadset::FromXrPath(XrPath path) {
  uint32_t stringLength;
  char text[XR_MAX_PATH_LENGTH];

  XrResult result = xrPathToString(mXRInstance, path, XR_MAX_PATH_LENGTH, &stringLength, text);

  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to retreive XR Path (error code: %i)\n", __FUNCTION__, result);
  } else {
    return std::string(text);
  }
  return std::string();
}

void VRHeadset::createXRAction(XrAction& xrAction, std::string name, XrActionType xrActionType, std::vector<std::string> subactionPaths) {
  XrActionCreateInfo actionCI{};
  actionCI.type = XR_TYPE_ACTION_CREATE_INFO;
  actionCI.actionType = xrActionType;

  std::vector<XrPath> subactionXRPaths;
  for (auto p : subactionPaths) {
    subactionXRPaths.push_back(CreateXrPath(p.c_str()));
  }

  actionCI.countSubactionPaths = (uint32_t)subactionXRPaths.size();
  actionCI.subactionPaths = subactionXRPaths.data();

  strncpy(actionCI.actionName, name.c_str(), XR_MAX_ACTION_NAME_SIZE);
  strncpy(actionCI.localizedActionName, name.c_str(), XR_MAX_LOCALIZED_ACTION_NAME_SIZE);

  XrResult result = xrCreateAction(mActionSet, &actionCI, &xrAction);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create Action (error code: %i)\n", __FUNCTION__, result);
  }
}

bool VRHeadset::createXRActionSet() {
  XrActionSetCreateInfo actionSetCI{};
  actionSetCI.type = XR_TYPE_ACTION_SET_CREATE_INFO;
  strncpy(actionSetCI.actionSetName, "mastering-animations-actionset", XR_MAX_ACTION_SET_NAME_SIZE);
  strncpy(actionSetCI.localizedActionSetName, "Mastering C++ Game Animation Programming ActionSet", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
  actionSetCI.priority = 0;

  XrResult result = xrCreateActionSet(mXRInstance, &actionSetCI, &mActionSet);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to create ActionSet (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  createXRAction(mPalmPoseAction, "palm-pose", XR_ACTION_TYPE_POSE_INPUT, {"/user/hand/right", "/user/hand/left"});
  createXRAction(mFlyLeftRighAction, "free-fly-lr", XR_ACTION_TYPE_FLOAT_INPUT, {"/user/hand/right"});
  createXRAction(mFlyFwdBackAction, "free-fly-fb", XR_ACTION_TYPE_FLOAT_INPUT, {"/user/hand/right"});
  createXRAction(mFlyUpDownAction, "free-fly-ud", XR_ACTION_TYPE_FLOAT_INPUT, {"/user/hand/left"});

  mHandPaths.at(0) = CreateXrPath("/user/hand/right");
  mHandPaths.at(1) = CreateXrPath("/user/hand/left");

  Logger::log(1, "%s: ActionSet created\n", __FUNCTION__);

  return true;
}

bool VRHeadset::suggestXRBinding(std::string profilePath, std::vector<XrActionSuggestedBinding> bindings) {
  XrInteractionProfileSuggestedBinding interactionProfileSuggestedBinding{};
  interactionProfileSuggestedBinding.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
  interactionProfileSuggestedBinding.interactionProfile = CreateXrPath(profilePath.c_str());
  interactionProfileSuggestedBinding.suggestedBindings = bindings.data();
  interactionProfileSuggestedBinding.countSuggestedBindings = (uint32_t)bindings.size();

  XrResult result = xrSuggestInteractionProfileBindings(mXRInstance, &interactionProfileSuggestedBinding);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to suggest bindings with '%s' (error code: %i)\n", __FUNCTION__, profilePath.c_str(), result);
    return false;
  }

  return true;
}

bool VRHeadset::suggestXRBindings() {
  bool anyOk = false;
  anyOk |= suggestXRBinding("/interaction_profiles/valve/index_controller",
  {
    { mPalmPoseAction, CreateXrPath("/user/hand/right/input/grip/pose") },
    { mPalmPoseAction, CreateXrPath("/user/hand/left/input/grip/pose") },
    { mFlyLeftRighAction, CreateXrPath("/user/hand/right/input/thumbstick/x") },
    { mFlyFwdBackAction, CreateXrPath("/user/hand/right/input/thumbstick/y") },
    { mFlyUpDownAction, CreateXrPath("/user/hand/left/input/thumbstick/y") }
  });

  anyOk |= suggestXRBinding("/interaction_profiles/oculus/touch_controller",
  {
    { mPalmPoseAction, CreateXrPath("/user/hand/right/input/grip/pose") },
    { mPalmPoseAction, CreateXrPath("/user/hand/left/input/grip/pose") },
    { mFlyLeftRighAction, CreateXrPath("/user/hand/right/input/thumbstick/x") },
    { mFlyFwdBackAction, CreateXrPath("/user/hand/right/input/thumbstick/y") },
    { mFlyUpDownAction, CreateXrPath("/user/hand/left/input/thumbstick/y") }
  });

  anyOk |= suggestXRBinding("/interaction_profiles/khr/simple_controller",
  {
    { mPalmPoseAction, CreateXrPath("/user/hand/right/input/grip/pose") },
    { mPalmPoseAction, CreateXrPath("/user/hand/left/input/grip/pose") },
    { mFlyLeftRighAction, CreateXrPath("/user/hand/right/input/select/click") },
    { mFlyFwdBackAction, CreateXrPath("/user/hand/right/input/menu/click") },
    { mFlyUpDownAction, CreateXrPath("/user/hand/left/input/select/click") }
  });

  if (!anyOk) {
    Logger::log(1, "%s error: Could not finish suggested bindings\n", __FUNCTION__);
    return false;
  }

  Logger::log(1, "%s: XR Suggested Bindings ok\n", __FUNCTION__);

  return true;
}

XrSpace VRHeadset::createXRActionPoseSpace(XrSession session, XrAction xrAction, std::string subactionPath) {
  XrSpace xrSpace;
  const XrPosef xrPoseIdentity = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

  XrActionSpaceCreateInfo actionSpaceCI{};
  actionSpaceCI.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
  actionSpaceCI.action = xrAction;
  actionSpaceCI.poseInActionSpace = xrPoseIdentity;

  if (!subactionPath.empty()) {
    actionSpaceCI.subactionPath = CreateXrPath(subactionPath.c_str());
  }

  XrResult result = xrCreateActionSpace(session, &actionSpaceCI, &xrSpace);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Could not create ActionSpace\n", __FUNCTION__);
  }

  return xrSpace;
}

bool VRHeadset::createXRActionPoses() {
  mHandPoseSpace.at(0) = createXRActionPoseSpace(mSession, mPalmPoseAction, "/user/hand/right");
  mHandPoseSpace.at(1) = createXRActionPoseSpace(mSession, mPalmPoseAction, "/user/hand/left");

  Logger::log(1, "%s: Action Poses created\n", __FUNCTION__);

  return true;
}

bool VRHeadset::attachActionSet() {
  XrSessionActionSetsAttachInfo actionSetAttachInfo{};
  actionSetAttachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
  actionSetAttachInfo.countActionSets = 1;
  actionSetAttachInfo.actionSets = &mActionSet;

  XrResult result = xrAttachSessionActionSets(mSession, &actionSetAttachInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Could not create ActionSpace\n", __FUNCTION__);
    return false;
  }

  Logger::log(1, "%s: ActionSet attached\n", __FUNCTION__);

  return true;
}

void VRHeadset::pollActions(XrTime predictedTime) {
  XrActiveActionSet activeActionSet{};
  activeActionSet.actionSet = mActionSet;
  activeActionSet.subactionPath = XR_NULL_PATH;

  XrActionsSyncInfo actionsSyncInfo{};
  actionsSyncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
  actionsSyncInfo.countActiveActionSets = 1;
  actionsSyncInfo.activeActionSets = &activeActionSet;

  XrResult result = xrSyncActions(mSession, &actionsSyncInfo);
  // silent ignore
  if (result == XR_SESSION_NOT_FOCUSED) {
    return;
  } else if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to sync Actions (error: %i)\n", __FUNCTION__, result);
    return;
  }

  XrActionStateGetInfo actionStateGetInfo{};
  actionStateGetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
  actionStateGetInfo.action = mPalmPoseAction;

  // for both hands
  for (int i = 0; i < 2; ++i) {
    actionStateGetInfo.subactionPath = mHandPaths.at(i);
    XrResult result = xrGetActionStatePose(mSession, &actionStateGetInfo, &mHandPoseState.at(i));
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to set Pose State\n", __FUNCTION__);
      break;
    }

    if (mHandPoseState.at(i).isActive) {
      XrSpaceLocation spaceLocation{};
      spaceLocation.type = XR_TYPE_SPACE_LOCATION;

      result = xrLocateSpace(mHandPoseSpace.at(i), mLocalSpace, predictedTime, &spaceLocation);

      if (result == XR_SUCCESS &&
          (spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
          (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        mHandPose.at(i) = spaceLocation.pose;
      } else {
        mHandPoseState.at(i).isActive = false;
      }
    }
  }

  // right hand only
  actionStateGetInfo.action = mFlyLeftRighAction;
  actionStateGetInfo.subactionPath = mHandPaths.at(0);

  result = xrGetActionStateFloat(mSession, &actionStateGetInfo, &mFlyLeftRightState);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get float state of fly left/right action (error: %i)\n", __FUNCTION__,result);
  }

  actionStateGetInfo.action = mFlyFwdBackAction;
  actionStateGetInfo.subactionPath = mHandPaths.at(0);

  result = xrGetActionStateFloat(mSession, &actionStateGetInfo, &mFlyFwdBackState);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get float state of fly forward/back action (error: %i)\n", __FUNCTION__,result);
  }

  // left hand
  actionStateGetInfo.action = mFlyUpDownAction;
  actionStateGetInfo.subactionPath = mHandPaths.at(1);

  result = xrGetActionStateFloat(mSession, &actionStateGetInfo, &mFlyUpDownState);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get float state of fly up/down action (error: %i)\n", __FUNCTION__,result);
  }

  if (mFlyLeftRightState.isActive || mFlyFwdBackState.isActive || mFlyUpDownState.isActive) {
    mRenderer->moveCamera(glm::vec3(mFlyLeftRightState.currentState, mFlyUpDownState.currentState, -mFlyFwdBackState.currentState));
  }
}

void VRHeadset::calculateXRHandPositions() {
  for (int i = 0; i < 2; ++i) {
    if (mHandPoseState.at(i).isActive) {
      glm::vec3 posePosition = glm::vec3(
        mHandPose.at(i).position.x,
        mHandPose.at(i).position.y,
        mHandPose.at(i).position.z);

      glm::mat4 transposeMat = glm::translate(glm::mat4(1.0f), posePosition);

      glm::quat poseOrientation = glm::quat();
      poseOrientation.x = mHandPose.at(i).orientation.x;
      poseOrientation.y = mHandPose.at(i).orientation.y;
      poseOrientation.z = mHandPose.at(i).orientation.z;
      poseOrientation.w = mHandPose.at(i).orientation.w;

      glm::mat4 orientationMat = glm::mat4_cast(poseOrientation);

      mHandTransformMatrices.at(i) = transposeMat *  orientationMat;
    }
  }
}

void VRHeadset::createXRCameraMatrices() {
  for (uint32_t i = 0; i < mViewCount; ++i) {
    glm::mat4 projMatrix = glm::mat4(0.0f);

    const float left = std::tan(mViews.at(i).fov.angleLeft);
    const float right = std::tan(mViews.at(i).fov.angleRight);
    // swap for Vulkan
    const float bottom = std::tan(mViews.at(i).fov.angleUp);
    const float top = std::tan(mViews.at(i).fov.angleDown);

    // ignore near value in first two elements
    projMatrix[0][0] = 2.0f / (right - left);
    projMatrix[1][1] = 2.0f / (top - bottom);

    projMatrix[2][0] = (right + left) / (right - left);
    projMatrix[2][1] = (top + bottom) / (top - bottom);
    projMatrix[2][2] = mFarPlane / (mNearPlane - mFarPlane);
    projMatrix[2][3] = -1.0f;
    projMatrix[3][2] = -(mFarPlane * mNearPlane) / (mFarPlane - mNearPlane);

    mProjViewMatrices.projectionMat.at(i) = projMatrix;

    glm::vec3 posePosition = glm::vec3(
      mViews.at(i).pose.position.x,
      mViews.at(i).pose.position.y,
      mViews.at(i).pose.position.z);

    // TODO: scaling
    mProjViewMatrices.viewTransposeMat.at(i) = glm::translate(glm::mat4(1.0f), posePosition);

    glm::quat poseOrientation = glm::quat();
      poseOrientation.x = mViews.at(i).pose.orientation.x;
      poseOrientation.y = mViews.at(i).pose.orientation.y;
      poseOrientation.z = mViews.at(i).pose.orientation.z;
      poseOrientation.w = mViews.at(i).pose.orientation.w;

    mProjViewMatrices.viewOrientationMat.at(i) = glm::mat4_cast(poseOrientation);
  }
}

bool VRHeadset::beginXRFrame() {
  XrFrameWaitInfo frameWaitInfo{};
  frameWaitInfo.type = XR_TYPE_FRAME_WAIT_INFO;

  XrResult result = xrWaitFrame(mSession, &frameWaitInfo, &mFrameState);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to wait for OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  XrFrameBeginInfo frameBeginInfo{};
  frameBeginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;

  result = xrBeginFrame(mSession, &frameBeginInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to begin OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  bool sessionActive = (mSessionState == XR_SESSION_STATE_SYNCHRONIZED || mSessionState == XR_SESSION_STATE_VISIBLE || mSessionState == XR_SESSION_STATE_FOCUSED);
  if (sessionActive && mFrameState.shouldRender) {
    pollActions(mFrameState.predictedDisplayTime);
  }

  XrViewState viewState{};
  viewState.type = XR_TYPE_VIEW_STATE;

  XrViewLocateInfo viewLocateInfo{};
  viewLocateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
  viewLocateInfo.viewConfigurationType = mViewConfiguration;
  viewLocateInfo.displayTime = mFrameState.predictedDisplayTime;
  viewLocateInfo.space = mLocalSpace;

  result = xrLocateViews(mSession, &viewLocateInfo, &viewState, static_cast<uint32_t>(mViews.size()), &mViewCount, mViews.data());
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to locate views (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  mRenderLayerInfos = {};
  mRenderLayerInfos.layerProjectionViews.resize(mViewCount);

  return true;
}


bool VRHeadset::renderXRFrame() {
  XrSwapchainImageAcquireInfo acquireInfo{};
  acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;

  XrResult result = xrAcquireSwapchainImage(mSwapchain.swapchain, &acquireInfo, &mColorImageIndex);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to acquire image from Swapchain (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  XrSwapchainImageWaitInfo waitInfo{};
  waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
  waitInfo.timeout = XR_INFINITE_DURATION;

  result = xrWaitSwapchainImage(mSwapchain.swapchain, &waitInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to wait for image from Swapchain (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  const uint32_t &width = mViewConfigurationViews.at(0).recommendedImageRectWidth;
  const uint32_t &height = mViewConfigurationViews.at(0).recommendedImageRectHeight;

  for (uint32_t i = 0; i < mViewCount; ++i) {
    mRenderLayerInfos.layerProjectionViews.at(i).type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    mRenderLayerInfos.layerProjectionViews.at(i).pose = mViews.at(i).pose;
    mRenderLayerInfos.layerProjectionViews.at(i).fov = mViews.at(i).fov;
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.swapchain = mSwapchain.swapchain;
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.imageRect.offset.x = 0;
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.imageRect.offset.y = 0;
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.imageRect.extent.width = static_cast<int32_t>(width);
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.imageRect.extent.height = static_cast<int32_t>(height);
    mRenderLayerInfos.layerProjectionViews.at(i).subImage.imageArrayIndex = i;  // Useful for multiview rendering.
  }


  bool sessionActive = (mSessionState == XR_SESSION_STATE_SYNCHRONIZED || mSessionState == XR_SESSION_STATE_VISIBLE || mSessionState == XR_SESSION_STATE_FOCUSED);
  if (sessionActive && mFrameState.shouldRender) {
    mRenderer->copyToXRSwapchain(mSwapchain.swapchainImageViews.at(mColorImageIndex));
  }

  return true;
}

bool VRHeadset::endXRFrame() {
  XrSwapchainImageReleaseInfo releaseInfo{};
  releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
  XrResult result = xrReleaseSwapchainImage(mSwapchain.swapchain, &releaseInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to release image back to Swapchain (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  mRenderLayerInfos.layerProjection.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
  mRenderLayerInfos.layerProjection.space = mLocalSpace;
  mRenderLayerInfos.layerProjection.viewCount = static_cast<uint32_t>(mRenderLayerInfos.layerProjectionViews.size());
  mRenderLayerInfos.layerProjection.views = mRenderLayerInfos.layerProjectionViews.data();

  mRenderLayerInfos.layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&mRenderLayerInfos.layerProjection));

  XrFrameEndInfo frameEndInfo{};
  frameEndInfo.type = XR_TYPE_FRAME_END_INFO;
  frameEndInfo.displayTime = mFrameState.predictedDisplayTime;
  frameEndInfo.environmentBlendMode = mEnvironmentBlendMode;
  frameEndInfo.layerCount = static_cast<uint32_t>(mRenderLayerInfos.layers.size());
  frameEndInfo.layers = mRenderLayerInfos.layers.data();

  result = xrEndFrame(mSession, &frameEndInfo);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s: Failed to end OpenXR frame (error code: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

void VRHeadset::destroyXRInstance() {
  XrResult result = xrDestroyInstance(mXRInstance);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Instance (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRSession() {
  XrResult result = xrDestroySession(mSession);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy Session (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRDebugMessenger() {
  PFN_xrDestroyDebugUtilsMessengerEXT xrDestroyDebugUtilsMessengerEXT = nullptr;
  XrResult result = xrGetInstanceProcAddr(mXRInstance, "xrDestroyDebugUtilsMessengerEXT", (PFN_xrVoidFunction *)&xrDestroyDebugUtilsMessengerEXT);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to get instance address of xrDestroyDebugUtilsMessengerEXT (error code: %i)\n", __FUNCTION__, result);
  }

  if (mDebugMessenger != XR_NULL_HANDLE) {
    result = xrDestroyDebugUtilsMessengerEXT(mDebugMessenger);
    if (result != XR_SUCCESS) {
      Logger::log(1, "%s error: Failed to destroy debug messenger (error code: %i)\n", __FUNCTION__, result);
    }
    mDebugMessenger = XR_NULL_HANDLE;
    Logger::log(1, "%s: Debug messenger destroyed\n", __FUNCTION__);
  }
}

void VRHeadset::destroyXRReferenceSpace() {
  XrResult result = xrDestroySpace(mLocalSpace);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy reference space (error code: %i)\n", __FUNCTION__, result);
  }
}

void VRHeadset::destroyXRSwapchain() {
  for (VkImageView &imageView : mSwapchain.swapchainImageViews) {
    vkDestroyImageView(mVulkanDevice, imageView, nullptr);
  }

  XrResult result = xrDestroySwapchain(mSwapchain.swapchain);
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: Failed to destroy swapchain (error code: %i)\n", __FUNCTION__, result);
  }
}

XrBool32 VRHeadset::handleXRErrors(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type,
    const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData) {

  std::string severityString;
  std::string typeString;

  switch (type) {
      case XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT :
        typeString = "general";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT :
        typeString = "validation";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT :
        typeString = "performance";
        break;
      case XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT :
        typeString = "conformance";
        break;
    }

    switch (severity){
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT :
        severityString = "verbose";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT :
        severityString = "info";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT :
        severityString = "warning";
        break;
      case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT :
        severityString = "error";
        break;
    }

    Logger::log(1, "%s: %s %s - '%s'\n", __FUNCTION__, typeString.c_str(), severityString.c_str(), callbackData->message);

    return XR_FALSE;
}

#if defined(__ANDROID__)
bool VRHeadset::initializeAndroidLoader() {
  android_app *app = Platform::getAndroidApp();
  if (!app || !app->activity) {
    Logger::log(1, "%s error: Android app activity unavailable\n", __FUNCTION__);
    return false;
  }

  mJavaVM = app->activity->vm;
  mActivityContext = app->activity->clazz;

  PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
  XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
    reinterpret_cast<PFN_xrVoidFunction *>(&initializeLoader));
  if (result != XR_SUCCESS || initializeLoader == nullptr) {
    Logger::log(1, "%s error: xrInitializeLoaderKHR unavailable (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  XrLoaderInitInfoAndroidKHR loaderInitInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
  loaderInitInfo.applicationVM = mJavaVM;
  loaderInitInfo.applicationContext = mActivityContext;

  result = initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&loaderInitInfo));
  if (result != XR_SUCCESS) {
    Logger::log(1, "%s error: failed to initialize Android OpenXR loader (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  Logger::log(1, "%s: Android OpenXR loader initialized\n", __FUNCTION__);
  return true;
}
#endif
