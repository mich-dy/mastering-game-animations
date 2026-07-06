#include <VkHelper.h>

#if defined(__ANDROID__)
#include <jni.h>
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif

#include <algorithm>
#include <random>
#include <glm/gtx/string_cast.hpp>

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <AssimpModel.h>

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
#include <CopyPipeline.h>
#include <XRCopyPipeline.h>
#include <UniformBuffer.h>
#include <ShaderStorageBuffer.h>
#include <VertexBuffer.h>
#include <Texture.h>

#include <Logger.h>

bool VkHelper::initVulkan(VkRenderData& renderData) {
  // Base OpenXR init as first step - Instance, SystemID, ReferenceSpace etc.

  // if (!mVRHeadset.init()) {
  //   return false;
  // }

  // Vulkan device init uses data from OpenXR Session
  if (!deviceInit(renderData)) {
    return false;
  }

 // Open XR Session init here

  if (!initVma(renderData)) {
    return false;
  }

  if (!getQueues(renderData)) {
    return false;
  }

  // VR Swapchain init
  if (!createSwapchain(renderData)) {
    return false;
  }

  // Add a desktop swapchain here too for the debug window

  if (!createCommandPools(renderData)) {
    return false;
  }

  if (!createCommandBuffers(renderData)) {
    return false;
  }

  // must be done AFTER swapchain and command pool+buffer as we need data from it
  if (!createDepthBuffer(renderData)) {
    return false;
  }

  if (!createVertexBuffers(renderData)) {
    return false;
  }

  if (!createUBOs(renderData)) {
    return false;
  }

  if (!createSSBOs(renderData)) {
    return false;
  }

  if (!createDescriptorPool(renderData)) {
    return false;
  }

  if (!createDescriptorLayouts(renderData)) {
    return false;
  }

  if (!createDescriptorSets(renderData)) {
    return false;
  }

  if (!createImages(renderData)) {
    return false;
  }

  if (!createGBuffer(renderData)) {
    return false;
  }

  if (!createShadowMapBuffer(renderData)) {
    return false;
  }

  // create a single shadow map to satisfy the descriptor sets
  if (!createDepthBufferCubeMap(renderData, renderData.rdDynamicLightShadowData, 1)) {
    return false;
  }

  initSSAO(renderData);

  updateImageDescriptorSets(renderData);

  if (!createPipelineLayouts(renderData)) {
    return false;
  }

#if !defined(__ANDROID__)
  if (!createPipelines(renderData)) {
    return false;
  }
#endif

  if (!createSyncObjects(renderData)) {
    return false;
  }

  return true;
}

#if defined(__ANDROID__)
struct AndroidVulkanDeviceFeatures {
  VkPhysicalDeviceFeatures2 features2{};
  VkPhysicalDeviceVulkan13Features vk13{};
  VkPhysicalDeviceVulkan12Features vk12{};
  VkPhysicalDeviceVulkan11Features vk11{};
  VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynState3{};
};

static bool populateAndroidXRPhysicalDevice(VkRenderData& renderData, VkPhysicalDevice xrPhysDevice,
  const VkPhysicalDeviceFeatures &vk10features,
  const VkPhysicalDeviceVulkan11Features &vk11features,
  const VkPhysicalDeviceVulkan12Features &vk12features,
  const VkPhysicalDeviceDynamicRenderingFeaturesKHR &dynamicRenderingFeature,
  const VkPhysicalDeviceExtendedDynamicState3FeaturesEXT &dynState3Feature,
  AndroidVulkanDeviceFeatures &outFeatures) {
  (void)dynamicRenderingFeature;

  vkb::PhysicalDeviceSelector bareSelector(renderData.rdVkbInstance);
  auto allDevicesRet = bareSelector
    .require_present(false)
    .select_devices(vkb::DeviceSelectionMode::partially_and_fully_suitable);
  if (!allDevicesRet) {
    Logger::log(1, "%s error: could not enumerate Vulkan physical devices\n", __FUNCTION__);
    return false;
  }

  bool foundDevice = false;
  for (const auto &candidate : allDevicesRet.value()) {
    if (candidate.physical_device == xrPhysDevice) {
      renderData.rdVkbPhysicalDevice = candidate;
      foundDevice = true;
      break;
    }
  }

  if (!foundDevice) {
    Logger::log(1, "%s error: OpenXR Vulkan device not found among %zu devices\n", __FUNCTION__, allDevicesRet.value().size());
    return false;
  }

  renderData.rdVkbPhysicalDevice.enable_features_if_present(vk10features);
  renderData.rdVkbPhysicalDevice.enable_extension_if_present(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
  for (const auto &xrExtension : renderData.rdXRDeviceExtensions) {
    renderData.rdVkbPhysicalDevice.enable_extension_if_present(xrExtension);
  }

  (void)vk12features;

  outFeatures.vk11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
  outFeatures.vk12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  outFeatures.vk13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  outFeatures.dynState3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
  outFeatures.vk13.pNext = &outFeatures.vk12;
  outFeatures.vk12.pNext = &outFeatures.vk11;
  if (renderData.rdVkbPhysicalDevice.is_extension_present(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME)) {
    outFeatures.vk11.pNext = &outFeatures.dynState3;
  } else {
    outFeatures.vk11.pNext = nullptr;
  }
  outFeatures.features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  outFeatures.features2.pNext = &outFeatures.vk13;

  vkGetPhysicalDeviceFeatures2(renderData.rdVkbPhysicalDevice.physical_device, &outFeatures.features2);
  outFeatures.features2.features = renderData.rdVkbPhysicalDevice.features;

#if !defined(__ANDROID__)
  if (vk11features.multiview && outFeatures.vk11.multiview != VK_TRUE) {
    Logger::log(1, "%s error: multiview not supported on '%s'\n", __FUNCTION__, renderData.rdVkbPhysicalDevice.name.c_str());
    return false;
  }
  outFeatures.vk11.multiview = VK_TRUE;
#else
  outFeatures.vk11.multiview = VK_FALSE;
#endif
  if (outFeatures.vk13.dynamicRendering != VK_TRUE) {
    Logger::log(1, "%s error: dynamicRendering not supported on '%s'\n", __FUNCTION__, renderData.rdVkbPhysicalDevice.name.c_str());
    return false;
  }
  outFeatures.vk13.dynamicRendering = VK_TRUE;
  outFeatures.vk13.synchronization2 = VK_TRUE;
  if (vk12features.timelineSemaphore && outFeatures.vk12.timelineSemaphore == VK_TRUE) {
    outFeatures.vk12.timelineSemaphore = VK_TRUE;
  }
  outFeatures.vk12.shaderOutputViewportIndex = VK_FALSE;
  outFeatures.vk12.shaderOutputLayer = VK_FALSE;

  bool dynState3Enabled = false;
  if (renderData.rdVkbPhysicalDevice.is_extension_present(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) &&
      dynState3Feature.extendedDynamicState3ColorBlendEquation == VK_TRUE &&
      outFeatures.dynState3.extendedDynamicState3ColorBlendEquation == VK_TRUE) {
    outFeatures.dynState3.extendedDynamicState3ColorBlendEquation = VK_TRUE;
    dynState3Enabled = true;
  }
  if (renderData.rdVkbPhysicalDevice.is_extension_present(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) &&
      dynState3Feature.extendedDynamicState3ColorBlendEnable == VK_TRUE &&
      outFeatures.dynState3.extendedDynamicState3ColorBlendEnable == VK_TRUE) {
    outFeatures.dynState3.extendedDynamicState3ColorBlendEnable = VK_TRUE;
    dynState3Enabled = true;
  }
  if (dynState3Enabled) {
    outFeatures.vk11.pNext = &outFeatures.dynState3;
    renderData.rdVkbPhysicalDevice.enable_extension_if_present(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
  } else {
    outFeatures.vk11.pNext = nullptr;
  }

  Logger::log(1, "%s: prepared multiview=%i dynamicRendering=%i dynState3Blend=%i on '%s'\n", __FUNCTION__,
    outFeatures.vk11.multiview, outFeatures.vk13.dynamicRendering,
    outFeatures.dynState3.extendedDynamicState3ColorBlendEnable, renderData.rdVkbPhysicalDevice.name.c_str());
  return true;
}

static uint32_t getQueueFamilyIndex(const std::vector<VkQueueFamilyProperties> &families, VkQueueFlags requiredFlags) {
  for (uint32_t i = 0; i < families.size(); ++i) {
    if ((families.at(i).queueFlags & requiredFlags) == requiredFlags) {
      return i;
    }
  }
  return UINT32_MAX;
}

static bool createAndroidXRVulkanDevice(VkRenderData& renderData, AndroidVulkanDeviceFeatures& androidFeatures) {
  auto xrInstance = reinterpret_cast<XrInstance>(renderData.rdXRInstanceHandle);

  PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR = nullptr;
  XrResult xrResult = xrGetInstanceProcAddr(xrInstance, "xrCreateVulkanDeviceKHR",
    reinterpret_cast<PFN_xrVoidFunction *>(&xrCreateVulkanDeviceKHR));
  if (xrResult != XR_SUCCESS || xrCreateVulkanDeviceKHR == nullptr) {
    Logger::log(1, "%s: xrCreateVulkanDeviceKHR unavailable, using vkCreateDevice\n", __FUNCTION__);
    return false;
  }

  const auto queueFamilies = renderData.rdVkbPhysicalDevice.get_queue_families();
  const uint32_t graphicsFamily = getQueueFamilyIndex(queueFamilies, VK_QUEUE_GRAPHICS_BIT);
  if (graphicsFamily == UINT32_MAX) {
    Logger::log(1, "%s error: no graphics queue family for OpenXR Vulkan device\n", __FUNCTION__);
    return false;
  }

  float graphicsPriority = 1.0f;
  VkDeviceQueueCreateInfo graphicsQueueCreateInfo{};
  graphicsQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  graphicsQueueCreateInfo.queueFamilyIndex = graphicsFamily;
  graphicsQueueCreateInfo.queueCount = 1;
  graphicsQueueCreateInfo.pQueuePriorities = &graphicsPriority;
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos = { graphicsQueueCreateInfo };

  const auto deviceExtensions = renderData.rdVkbPhysicalDevice.get_extensions();
  std::vector<const char *> enabledExtensions;
  for (const auto &extension : deviceExtensions) {
    enabledExtensions.push_back(extension.c_str());
  }

  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
  deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions.data();
  deviceCreateInfo.pNext = &androidFeatures.features2;

  XrVulkanDeviceCreateInfoKHR xrDeviceCreateInfo{};
  xrDeviceCreateInfo.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
  xrDeviceCreateInfo.systemId = static_cast<XrSystemId>(renderData.rdXRSystemId);
  xrDeviceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
  xrDeviceCreateInfo.vulkanPhysicalDevice = renderData.rdVkbPhysicalDevice.physical_device;
  xrDeviceCreateInfo.vulkanCreateInfo = &deviceCreateInfo;

  VkDevice vkDevice = VK_NULL_HANDLE;
  VkResult vkResult = VK_SUCCESS;
  xrResult = xrCreateVulkanDeviceKHR(xrInstance, &xrDeviceCreateInfo, &vkDevice, &vkResult);
  if (xrResult != XR_SUCCESS || vkResult != VK_SUCCESS) {
    Logger::log(1, "%s error: xrCreateVulkanDeviceKHR failed (xr=%i, vk=%i)\n", __FUNCTION__, xrResult, vkResult);
    return false;
  }

  auto *beginRendering = vkGetDeviceProcAddr(vkDevice, "vkCmdBeginRendering");
  if (beginRendering == nullptr) {
    Logger::log(1, "%s error: xrCreateVulkanDevice device lacks vkCmdBeginRendering, destroying device\n", __FUNCTION__);
    vkDestroyDevice(vkDevice, nullptr);
    return false;
  }

  renderData.rdVkbDevice.device = vkDevice;
  renderData.rdVkbDevice.physical_device = renderData.rdVkbPhysicalDevice;
  renderData.rdVkbDevice.queue_families = queueFamilies;
  renderData.rdVkbDevice.fp_vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  renderData.rdVkbDevice.instance_version = VK_API_VERSION_1_3;
  Logger::log(1, "%s: OpenXR Vulkan device created via xrCreateVulkanDeviceKHR\n", __FUNCTION__);
  return true;
}

#endif

bool VkHelper::deviceInit(VkRenderData& renderData) {
  /* instance and window - we need at least Vukan 1.3 for dynamic rendering */
  vkb::InstanceBuilder instBuild;
  auto instBuilder = instBuild
  .enable_extensions(renderData.rdXRInstanceExtensions)
  .require_api_version(1, 3, 0);

#if !defined(__ANDROID__)
  instBuilder = instBuilder
  .use_default_debug_messenger()
  .request_validation_layers()
  .enable_extension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) // required to use VK_EXT_swapchain_maintenance1
  .enable_extension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME); // required to use VK_EXT_surface_maintenance1
#else
  instBuilder = instBuilder
    .set_headless()
    .request_validation_layers()
    .use_default_debug_messenger();
#endif

  auto instRet = instBuilder.build();

  if (!instRet) {
    Logger::log(1, "%s error: could not build vkb instance\n", __FUNCTION__);
    return false;
  }
  renderData.rdVkbInstance = instRet.value();

#if !defined(__ANDROID__)
  VkResult result = glfwCreateWindowSurface(renderData.rdVkbInstance, renderData.rdWindow, nullptr, &renderData.rdSurface);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: Could not create Vulkan surface (error: %i)\n", __FUNCTION__);
    return false;
  }
#else
  renderData.rdSkipDesktopMirror = true;
#endif

  // enable dynamic rendering
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature{};
  dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
  dynamicRenderingFeature.dynamicRendering = VK_TRUE;

  VkPhysicalDeviceShaderObjectFeaturesEXT deviceShaderFeature{};
  deviceShaderFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
  deviceShaderFeature.shaderObject = VK_TRUE;

  VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynState3Feature{};
  dynState3Feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
  dynState3Feature.extendedDynamicState3ColorBlendEquation = VK_TRUE;
  dynState3Feature.extendedDynamicState3ColorBlendEnable = VK_TRUE;

  // force anisotropy and line width
  VkPhysicalDeviceFeatures vk10features{};
  vk10features.samplerAnisotropy = VK_TRUE;
#if !defined(__ANDROID__)
  vk10features.wideLines = VK_TRUE;
#endif
  vk10features.imageCubeArray = VK_TRUE;
#if !defined(__ANDROID__)
  vk10features.multiViewport = VK_TRUE; // required for GL_ARB_shader_viewport_layer_array
  vk10features.geometryShader = VK_TRUE; // seem to be required by latest STEAM update
#endif

  VkPhysicalDeviceVulkan11Features vk11features{};
#if !defined(__ANDROID__)
  vk11features.multiview = VK_TRUE;
#endif

  VkPhysicalDeviceVulkan12Features vk12features{};
  vk12features.timelineSemaphore = VK_TRUE;
#if !defined(__ANDROID__)
  // required from Vulkan to enable when VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME is enabled
  vk12features.shaderOutputViewportIndex = VK_TRUE;
  vk12features.shaderOutputLayer = VK_TRUE;
#endif

  vkb::PhysicalDeviceSelector physicalDevSel{renderData.rdVkbInstance};
#if !defined(__ANDROID__)
  auto physicalDevSelRet = physicalDevSel
  .set_surface(renderData.rdSurface)
  .set_required_features(vk10features)
  .set_required_features_11(vk11features)
  .set_required_features_12(vk12features)
  .add_required_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
  .add_required_extension(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) // rquired for dynamic color settings in pipeline
  .add_required_extension(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME) // required for GL_ARB_shader_viewport_layer_array
  .add_required_extension_features(dynamicRenderingFeature)
  .add_required_extension_features(deviceShaderFeature)
  .add_required_extension_features(dynState3Feature)
  .add_required_extensions(renderData.rdXRDeviceExtensions)
  .select();
#else
  VkPhysicalDevice xrPhysDevice = VK_NULL_HANDLE;
  if (renderData.rdXRInstanceHandle != 0 && renderData.rdXRSystemId != 0) {
    auto xrInstance = reinterpret_cast<XrInstance>(renderData.rdXRInstanceHandle);
    PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR = nullptr;
    XrResult xrResult = xrGetInstanceProcAddr(xrInstance, "xrGetVulkanGraphicsDeviceKHR",
      reinterpret_cast<PFN_xrVoidFunction *>(&xrGetVulkanGraphicsDeviceKHR));
    if (xrResult == XR_SUCCESS && xrGetVulkanGraphicsDeviceKHR != nullptr) {
      xrResult = xrGetVulkanGraphicsDeviceKHR(xrInstance, static_cast<XrSystemId>(renderData.rdXRSystemId),
        renderData.rdVkbInstance.instance, &xrPhysDevice);
      if (xrResult != XR_SUCCESS) {
        Logger::log(1, "%s error: xrGetVulkanGraphicsDeviceKHR failed (%i)\n", __FUNCTION__, xrResult);
        return false;
      }
    } else {
      Logger::log(1, "%s error: xrGetVulkanGraphicsDeviceKHR unavailable\n", __FUNCTION__);
      return false;
    }
  } else {
    Logger::log(1, "%s error: OpenXR instance/system not set for Android Vulkan init\n", __FUNCTION__);
    return false;
  }

  AndroidVulkanDeviceFeatures androidFeatures{};
  if (!populateAndroidXRPhysicalDevice(renderData, xrPhysDevice, vk10features, vk11features, vk12features,
    dynamicRenderingFeature, dynState3Feature, androidFeatures)) {
    Logger::log(1, "%s error: could not populate Android XR physical device\n", __FUNCTION__);
    return false;
  }
#endif

#if !defined(__ANDROID__)
  if (!physicalDevSelRet) {
    Logger::log(1, "%s error: could not get physical devices\n", __FUNCTION__);
    return false;
  }

  renderData.rdVkbPhysicalDevice = physicalDevSelRet.value();
#endif
  Logger::log(1, "%s: found physical device '%s'\n", __FUNCTION__, renderData.rdVkbPhysicalDevice.name.c_str());
  /* required for dynamic buffer with world position matrices */
  VkDeviceSize minSSBOOffsetAlignment = renderData.rdVkbPhysicalDevice.properties.limits.minStorageBufferOffsetAlignment;
  Logger::log(1, "%s: the physical device has a minimal SSBO offset of %i bytes\n", __FUNCTION__, minSSBOOffsetAlignment);
  renderData.rdMinSSBOOffsetAlignment = std::max(minSSBOOffsetAlignment, sizeof(glm::mat4));
  Logger::log(1, "%s: SSBO offset has been adjusted to %i bytes\n", __FUNCTION__, renderData.rdMinSSBOOffsetAlignment);

#if defined(__ANDROID__)
  if (!createAndroidXRVulkanDevice(renderData, androidFeatures)) {
    vkb::DeviceBuilder devBuilder{renderData.rdVkbPhysicalDevice};
    const uint32_t graphicsFamily =
      getQueueFamilyIndex(renderData.rdVkbPhysicalDevice.get_queue_families(), VK_QUEUE_GRAPHICS_BIT);
    if (graphicsFamily != UINT32_MAX) {
      devBuilder.custom_queue_setup({ vkb::CustomQueueDescription(graphicsFamily, {1.0f}) });
    }
    auto devBuilderRet = devBuilder.add_pNext(&androidFeatures.features2).build();
    if (!devBuilderRet) {
      Logger::log(1, "%s error: could not get devices (vk result %i)\n", __FUNCTION__, devBuilderRet.vk_result());
      return false;
    }
    renderData.rdVkbDevice = devBuilderRet.value();
  }
#else
  vkb::DeviceBuilder devBuilder{renderData.rdVkbPhysicalDevice};
  auto devBuilderRet = devBuilder.build();
  if (!devBuilderRet) {
    Logger::log(1, "%s error: could not get devices (vk result %i)\n", __FUNCTION__, devBuilderRet.vk_result());
    return false;
  }
  renderData.rdVkbDevice = devBuilderRet.value();
#endif

  // not found in my loader, need to get from library xD
  renderData.rdvkCmdSetColorBlendEnableEXT = reinterpret_cast<PFN_vkCmdSetColorBlendEnableEXT>(vkGetDeviceProcAddr(renderData.rdVkbDevice.device, "vkCmdSetColorBlendEnableEXT"));
  renderData.rdvkCmdSetColorBlendEquationEXT = reinterpret_cast<PFN_vkCmdSetColorBlendEquationEXT>(vkGetDeviceProcAddr(renderData.rdVkbDevice.device, "vkCmdSetColorBlendEquationEXT"));

  return true;
}

bool VkHelper::initVma(VkRenderData& renderData) {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.physicalDevice = renderData.rdVkbPhysicalDevice.physical_device;
  allocatorInfo.device = renderData.rdVkbDevice.device;
  allocatorInfo.instance = renderData.rdVkbInstance.instance;

  VkResult result = vmaCreateAllocator(&allocatorInfo, &renderData.rdAllocator);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not init VMA (error %i)\n", __FUNCTION__, result);
    return false;
  }
  return true;
}

bool VkHelper::getQueues(VkRenderData& renderData) {
#if defined(__ANDROID__)
  const auto &queueFamilies = renderData.rdVkbDevice.queue_families;
  const uint32_t graphicsFamily = getQueueFamilyIndex(queueFamilies, VK_QUEUE_GRAPHICS_BIT);
  if (graphicsFamily == UINT32_MAX) {
    Logger::log(1, "%s error: could not get graphics queue family\n", __FUNCTION__);
    return false;
  }
  vkGetDeviceQueue(renderData.rdVkbDevice.device, graphicsFamily, 0, &renderData.rdGraphicsQueue);
  renderData.rdPresentQueue = renderData.rdGraphicsQueue;
  renderData.rdComputeQueue = renderData.rdGraphicsQueue;
  renderData.rdHasDedicatedComputeQueue = false;
  Logger::log(1, "%s: Android XR using shared graphics/compute/present queue\n", __FUNCTION__);
#else
  auto graphQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::graphics);
  if (!graphQueueRet.has_value()) {
    Logger::log(1, "%s error: could not get graphics queue\n", __FUNCTION__);
    return false;
  }
  renderData.rdGraphicsQueue = graphQueueRet.value();

  if (renderData.rdSkipDesktopMirror) {
    renderData.rdPresentQueue = renderData.rdGraphicsQueue;
  } else {
    auto presentQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::present);
    if (!presentQueueRet.has_value()) {
      Logger::log(1, "%s error: could not get present queue\n", __FUNCTION__);
      return false;
    }
    renderData.rdPresentQueue = presentQueueRet.value();
  }

  auto computeQueueRet = renderData.rdVkbDevice.get_queue(vkb::QueueType::compute);
  if (!computeQueueRet.has_value()) {
    Logger::log(1, "%s: using shared graphics/compute queue\n", __FUNCTION__);
    renderData.rdComputeQueue = renderData.rdGraphicsQueue;
    renderData.rdHasDedicatedComputeQueue = false;
  } else {
    Logger::log(1, "%s: using separate compute queue\n", __FUNCTION__);
    renderData.rdComputeQueue = computeQueueRet.value();
    renderData.rdHasDedicatedComputeQueue = true;
  }
#endif

  return true;
}

bool VkHelper::createSwapchain(VkRenderData& renderData) {
#if defined(__ANDROID__)
  if (renderData.rdSkipDesktopMirror) {
    renderData.rdWidth = renderData.rdXRWidth > 0 ? renderData.rdXRWidth : 1920;
    renderData.rdHalfWidth = renderData.rdWidth / 2;
    renderData.rdHeight = renderData.rdXRHeight > 0 ? renderData.rdXRHeight : 1080;
    renderData.rdWindowWidth = static_cast<int>(renderData.rdWidth);
    renderData.rdWindowHeight = static_cast<int>(renderData.rdHeight);
    renderData.rdNumFramesInFlight = renderData.MAX_FRAMES_IN_FLIGHT;
    renderData.rdVkbSwapchain.image_format = VK_FORMAT_B8G8R8A8_UNORM;
    Logger::log(1, "%s: Android XR headless mode %ix%i, %i frames in flight\n", __FUNCTION__,
      renderData.rdWidth, renderData.rdHeight, renderData.rdNumFramesInFlight);
    return true;
  }
#endif

#if !defined(__ANDROID__)
  vkb::SwapchainBuilder swapChainBuild{renderData.rdVkbDevice};
  VkSurfaceFormatKHR surfaceFormat;

  // set surface to non-sRGB
  surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  surfaceFormat.format = VK_FORMAT_B8G8R8A8_UNORM;

  int frameBufferWidth = 0;
  int frameBufferHeight = 0;
  // get framebuffer size instead of window size in case some scaling has been applied (Wayland)
  glfwGetFramebufferSize(renderData.rdWindow, &frameBufferWidth, &frameBufferHeight);

  // VK_PRESENT_MODE_FIFO_KHR enables vsync
  auto  swapChainBuildRet = swapChainBuild
    .set_old_swapchain(renderData.rdVkbSwapchain)
    .set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
    .set_desired_format(surfaceFormat)
    .set_desired_min_image_count(renderData.rdNumFramesInFlight)
    // Wayland needs the extent here or we will get something like 256x256 pixel sized swapchain images
    .set_desired_extent(frameBufferWidth, frameBufferHeight)
    .build();

  if (!swapChainBuildRet) {
    Logger::log(1, "%s error: could not init swapchain\n", __FUNCTION__);
    return false;
  }

  vkb::destroy_swapchain(renderData.rdVkbSwapchain);
  renderData.rdVkbSwapchain = swapChainBuildRet.value();
  renderData.rdSwapchainImages = swapChainBuildRet.value().get_images().value();
  renderData.rdSwapchainImageViews = swapChainBuildRet.value().get_image_views().value();

  // set width and height from swapchain
  renderData.rdWidth = renderData.rdVkbSwapchain.extent.width;
  renderData.rdHalfWidth = renderData.rdWidth / 2;
  renderData.rdHeight = renderData.rdVkbSwapchain.extent.height;

  int windowWidth = 0;
  int windowHeight = 0;
  // get window size for UI
  glfwGetWindowSize(renderData.rdWindow, &windowWidth, &windowHeight);
  renderData.rdWindowWidth = windowWidth;
  renderData.rdWindowHeight = windowHeight;

  renderData.rdNumFramesInFlight = renderData.rdSwapchainImages.size();

  Logger::log(1, "%s: Swapchain requested %i images, got %i\n", __FUNCTION__, renderData.MAX_FRAMES_IN_FLIGHT, renderData.rdNumFramesInFlight);

  return true;
#endif
}

bool VkHelper::recreateSwapchain(VkRenderData& renderData) {
#if defined(__ANDROID__)
  if (renderData.rdSkipDesktopMirror) {
    return true;
  }
#endif
  Logger::log(1, "%s: recreate swapchain\n", __FUNCTION__);

  int screenWidth = 0;
  int screenHeight = 0;

  // handle minimize
  while (renderData.rdWidth == 0 || renderData.rdHeight == 0) {
#if !defined(__ANDROID__)
    glfwGetFramebufferSize(renderData.rdWindow, &screenWidth, &screenHeight);
    renderData.rdWidth = screenWidth;
    renderData.rdHeight = screenHeight;
    glfwWaitEvents();
#else
    break;
#endif
  }

  vkDeviceWaitIdle(renderData.rdVkbDevice.device);

  // cleanup
  cleanupDepthBuffer(renderData);
  cleanupGBuffer(renderData);
  cleanupImages(renderData);

  renderData.rdVkbSwapchain.destroy_image_views(renderData.rdSwapchainImageViews);

  // and recreate
  if (!createSwapchain(renderData)) {
    Logger::log(1, "%s error: could not recreate swapchain\n", __FUNCTION__);
    return false;
  }

  if (!createDepthBuffer(renderData)) {
    Logger::log(1, "%s error: could not recreate depth buffer\n", __FUNCTION__);
    return false;
  }

  if (!createImages(renderData)) {
    Logger::log(1, "%s error: could not recreate image buffers\n", __FUNCTION__);
    return false;
  }

  if (!createGBuffer(renderData)) {
    Logger::log(1, "%s error: could not recreate G-Buffer buffer\n", __FUNCTION__);
    return false;
  }

  updateImageDescriptorSets(renderData);

  Logger::log(1, "%s: swapchain recreated\n", __FUNCTION__);
  return true;
}


bool VkHelper::createCommandPools(VkRenderData& renderData) {
  if (!CommandPool::init(renderData, vkb::QueueType::graphics, renderData.rdCommandPool)) {
    Logger::log(1, "%s error: could not create graphics command pool\n", __FUNCTION__);
    return false;
  }

  // use graphics queue if we have a shared queue 
  vkb::QueueType computeQueue = renderData.rdHasDedicatedComputeQueue ? vkb::QueueType::compute : vkb::QueueType::graphics;
  if (!CommandPool::init(renderData, computeQueue, renderData.rdComputeCommandPool)) {
    Logger::log(1, "%s error: could not create compute command pool\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createCommandBuffers(VkRenderData& renderData) {
  renderData.rdCommandBuffers.resize(renderData.rdNumFramesInFlight);
  renderData.rdComputeCommandBuffers.resize(renderData.rdNumFramesInFlight);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    if (!CommandBuffer::init(renderData,renderData.rdCommandPool, renderData.rdCommandBuffers[i])) {
      Logger::log(1, "%s error: could not create command buffers\n", __FUNCTION__);
      return false;
    }

    if (!CommandBuffer::init(renderData,renderData.rdComputeCommandPool, renderData.rdComputeCommandBuffers[i])) {
      Logger::log(1, "%s error: could not create compute command buffers\n", __FUNCTION__);
      return false;
    }
  }

  return true;
}

bool VkHelper::createUBOs(VkRenderData& renderData) {
  if (!UniformBuffer::init(renderData, renderData.rdRenderUploadDataUBOs, sizeof(VkRenderUploadData))) {
    Logger::log(1, "%s error: could not create matrix uniform buffers\n", __FUNCTION__);
    return false;
  }
  if (!UniformBuffer::init(renderData, renderData.rdSSAOKernelSamplesUBO, sizeof(glm::vec3) * 64)) {
    Logger::log(1, "%s error: could not create SSAO kernel uniform buffers\n", __FUNCTION__);
    return false;
  }
  return true;
}

bool VkHelper::createSSBOs(VkRenderData& renderData) {
  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderTRSMatrixBuffers)) {
    Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderModelRootMatrixBuffers)) {
    Logger::log(1, "%s error: could not create nodel root position SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdPerInstanceAnimDataBuffers)) {
    Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
    return false;
  }

  // we must read back data
  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderBoneMatrixBuffers)) {
    Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSelectedInstanceBuffers)) {
    Logger::log(1, "%s error: could not create selection SSBO\n", __FUNCTION__);
    return false;
  }

  // we must read back data
  if (!ShaderStorageBuffer::init(renderData, renderData.rdBoundingSphereBuffers)) {
    Logger::log(1, "%s error: could not create bounding sphere SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereModelRootMatrixBuffers)) {
    Logger::log(1, "%s error: could not create nodel root position SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSpherePerInstanceAnimDataBuffers)) {
    Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereTRSMatrixBuffers)) {
    Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdSphereBoneMatrixBuffers)) {
    Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdFaceAnimPerInstanceDataBuffers)) {
    Logger::log(1, "%s error: could not create face anim SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdShaderLevelRootMatrixBuffers)) {
    Logger::log(1, "%s error: could not create level world pos SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdIKBoneMatrixBuffers)) {
    Logger::log(1, "%s error: could not create inverse kinematics matrix SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdIKTRSMatrixBuffers)) {
    Logger::log(1, "%s error: could not create inverse kinematics TRS data SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdShadowMapCascadeDataBuffers)) {
    Logger::log(1, "%s error: could not create shadow map data SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdDynamicLightBuffers)) {
    Logger::log(1, "%s error: could not create dynamic light data SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdDynamicLightDebugBuffers)) {
    Logger::log(1, "%s error: could not create dynamic light debug data SSBO\n", __FUNCTION__);
    return false;
  }

  if (!ShaderStorageBuffer::init(renderData, renderData.rdVRControllerModelRootMatrixBuffers)) {
    Logger::log(1, "%s error: could not create dynamic light debug data SSBO\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createDescriptorPool(VkRenderData& renderData) {
  std::vector<VkDescriptorPoolSize> poolSizes =
  {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 10000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 },
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 10000;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  VkResult result = vkCreateDescriptorPool(renderData.rdVkbDevice.device, &poolInfo, nullptr, &renderData.rdDescriptorPool);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

bool VkHelper::createDescriptorLayouts(VkRenderData& renderData) {
  VkResult result;

  {
    // texture
    VkDescriptorSetLayoutBinding assimpTextureBind{};
    assimpTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    assimpTextureBind.binding = 0;
    assimpTextureBind.descriptorCount = 1;
    assimpTextureBind.pImmutableSamplers = nullptr;
    assimpTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpTexBindings = { assimpTextureBind };

    VkDescriptorSetLayoutCreateInfo assimpTextureCreateInfo{};
    assimpTextureCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTextureCreateInfo.bindingCount = static_cast<uint32_t>(assimpTexBindings.size());
    assimpTextureCreateInfo.pBindings = assimpTexBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTextureCreateInfo,
      nullptr, &renderData.rdAssimpTextureDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp texture descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // non-animated shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind{};
    assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind.binding = 1;
    assimpSsboBind.descriptorCount = 1;
    assimpSsboBind.pImmutableSamplers = nullptr;
    assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind2{};
    assimpSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind2.binding = 2;
    assimpSsboBind2.descriptorCount = 1;
    assimpSsboBind2.pImmutableSamplers = nullptr;
    assimpSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shadowMapCascadeDataBind{};
    shadowMapCascadeDataBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDataBind.binding = 3;
    shadowMapCascadeDataBind.descriptorCount = 1;
    shadowMapCascadeDataBind.pImmutableSamplers = nullptr;
    shadowMapCascadeDataBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = {
      renderDataUboBind,
      assimpSsboBind,
      assimpSsboBind2,
      shadowMapCascadeDataBind
    };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdAssimpDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
    assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind.binding = 1;
    assimpSkinningSsboBind.descriptorCount = 1;
    assimpSkinningSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
    assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind2.binding = 2;
    assimpSkinningSsboBind2.descriptorCount = 1;
    assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
    assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind3.binding = 3;
    assimpSkinningSsboBind3.descriptorCount = 1;
    assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shadowMapCascadeDataBind{};
    shadowMapCascadeDataBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDataBind.binding = 4;
    shadowMapCascadeDataBind.descriptorCount = 1;
    shadowMapCascadeDataBind.pImmutableSamplers = nullptr;
    shadowMapCascadeDataBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { renderDataUboBind,
        assimpSkinningSsboBind,
        assimpSkinningSsboBind2,
        assimpSkinningSsboBind3,
        shadowMapCascadeDataBind
      };

    VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
    assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // non-animated selection shader
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSelSsboBind{};
    assimpSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSelSsboBind.binding = 1;
    assimpSelSsboBind.descriptorCount = 1;
    assimpSelSsboBind.pImmutableSamplers = nullptr;
    assimpSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSelSsboBind2{};
    assimpSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSelSsboBind2.binding = 2;
    assimpSelSsboBind2.descriptorCount = 1;
    assimpSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpSelUboBind, assimpSelSsboBind, assimpSelSsboBind2 };

    VkDescriptorSetLayoutCreateInfo assimpSelCreateInfo{};
    assimpSelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpSelCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSelCreateInfo,
      nullptr, &renderData.rdAssimpSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated selection shader
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
    assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind.binding = 1;
    assimpSkinningSelSsboBind.descriptorCount = 1;
    assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
    assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind2.binding = 2;
    assimpSkinningSelSsboBind2.descriptorCount = 1;
    assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
    assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind3.binding = 3;
    assimpSkinningSelSsboBind3.descriptorCount = 1;
    assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { assimpSelUboBind, assimpSkinningSelSsboBind, assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3 };

    VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
    assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated shader with morphs
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
    assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind.binding = 1;
    assimpSkinningSsboBind.descriptorCount = 1;
    assimpSkinningSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
    assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind2.binding = 2;
    assimpSkinningSsboBind2.descriptorCount = 1;
    assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
    assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind3.binding = 3;
    assimpSkinningSsboBind3.descriptorCount = 1;
    assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSsboBind4{};
    assimpSkinningSsboBind4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSsboBind4.binding = 4;
    assimpSkinningSsboBind4.descriptorCount = 1;
    assimpSkinningSsboBind4.pImmutableSamplers = nullptr;
    assimpSkinningSsboBind4.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shadowMapCascadeDataBind{};
    shadowMapCascadeDataBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDataBind.binding = 5;
    shadowMapCascadeDataBind.descriptorCount = 1;
    shadowMapCascadeDataBind.pImmutableSamplers = nullptr;
    shadowMapCascadeDataBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
      { renderDataUboBind,
        assimpSkinningSsboBind,
        assimpSkinningSsboBind2,
        assimpSkinningSsboBind3,
        assimpSkinningSsboBind4,
        shadowMapCascadeDataBind
      };

    VkDescriptorSetLayoutCreateInfo assimpMorphSkinningCreateInfo{};
    assimpMorphSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpMorphSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphSkinningCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated plus morphs selection shader
    VkDescriptorSetLayoutBinding assimpSelUboBind{};
    assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    assimpSelUboBind.binding = 0;
    assimpSelUboBind.descriptorCount = 1;
    assimpSelUboBind.pImmutableSamplers = nullptr;
    assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
    assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind.binding = 1;
    assimpSkinningSelSsboBind.descriptorCount = 1;
    assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
    assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind2.binding = 2;
    assimpSkinningSelSsboBind2.descriptorCount = 1;
    assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
    assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind3.binding = 3;
    assimpSkinningSelSsboBind3.descriptorCount = 1;
    assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind4{};
    assimpSkinningSelSsboBind4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSkinningSelSsboBind4.binding = 4;
    assimpSkinningSelSsboBind4.descriptorCount = 1;
    assimpSkinningSelSsboBind4.pImmutableSamplers = nullptr;
    assimpSkinningSelSsboBind4.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
    { assimpSelUboBind, assimpSkinningSelSsboBind,
      assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3, assimpSkinningSelSsboBind4 };

    VkDescriptorSetLayoutCreateInfo assimpMorphSkinningSelectionCreateInfo{};
    assimpMorphSkinningSelectionCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphSkinningSelectionCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
    assimpMorphSkinningSelectionCreateInfo.pBindings = assimpSkinningBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphSkinningSelectionCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphSelectionDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated plus morphs, per-model
    VkDescriptorSetLayoutBinding assimpMorphPerModelSsboBind{};
    assimpMorphPerModelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpMorphPerModelSsboBind.binding = 0;
    assimpMorphPerModelSsboBind.descriptorCount = 1;
    assimpMorphPerModelSsboBind.pImmutableSamplers = nullptr;
    assimpMorphPerModelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo assimpMorphPerModelCreateInfo{};
    assimpMorphPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMorphPerModelCreateInfo.bindingCount = 1;
    assimpMorphPerModelCreateInfo.pBindings = &assimpMorphPerModelSsboBind;

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMorphPerModelCreateInfo,
      nullptr, &renderData.rdAssimpSkinningMorphPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp morph skinning selection per-model buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // assimp level
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpSsboBind{};
    assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpSsboBind.binding = 1;
    assimpSsboBind.descriptorCount = 1;
    assimpSsboBind.pImmutableSamplers = nullptr;
    assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shadowMapCascadeDataBind{};
    shadowMapCascadeDataBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDataBind.binding = 2;
    shadowMapCascadeDataBind.descriptorCount = 1;
    shadowMapCascadeDataBind.pImmutableSamplers = nullptr;
    shadowMapCascadeDataBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = {
      renderDataUboBind,
      assimpSsboBind,
      shadowMapCascadeDataBind
    };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdAssimpLevelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp Level buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // ground mesh
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdGroundMeshDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp ground mesh buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute transformation shader, global 
    VkDescriptorSetLayoutBinding assimpTransformSsboBind{};
    assimpTransformSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTransformSsboBind.binding = 0;
    assimpTransformSsboBind.descriptorCount = 1;
    assimpTransformSsboBind.pImmutableSamplers = nullptr;
    assimpTransformSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
    assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTrsSsboBind.binding = 1;
    assimpTrsSsboBind.descriptorCount = 1;
    assimpTrsSsboBind.pImmutableSamplers = nullptr;
    assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpTransformBindings = { assimpTransformSsboBind, assimpTrsSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpTransformCreateInfo{};
    assimpTransformCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTransformCreateInfo.bindingCount = static_cast<uint32_t>(assimpTransformBindings.size());
    assimpTransformCreateInfo.pBindings = assimpTransformBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTransformCreateInfo,
      nullptr, &renderData.rdAssimpComputeTransformDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp transform global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute transformation shader, per-model 
    VkDescriptorSetLayoutBinding assimpAnimLookupSsboBind{};
    assimpAnimLookupSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpAnimLookupSsboBind.binding = 0;
    assimpAnimLookupSsboBind.descriptorCount = 1;
    assimpAnimLookupSsboBind.pImmutableSamplers = nullptr;
    assimpAnimLookupSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo assimpTransformPerModelCreateInfo{};
    assimpTransformPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpTransformPerModelCreateInfo.bindingCount = 1;
    assimpTransformPerModelCreateInfo.pBindings = &assimpAnimLookupSsboBind;

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpTransformPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeTransformPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp transform per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute matrix multiplication shader, global data
    VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
    assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpTrsSsboBind.binding = 0;
    assimpTrsSsboBind.descriptorCount = 1;
    assimpTrsSsboBind.pImmutableSamplers = nullptr;
    assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpNodeMatricesSsboBind{};
    assimpNodeMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpNodeMatricesSsboBind.binding = 1;
    assimpNodeMatricesSsboBind.descriptorCount = 1;
    assimpNodeMatricesSsboBind.pImmutableSamplers = nullptr;
    assimpNodeMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
      { assimpTrsSsboBind,assimpNodeMatricesSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
    assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
    assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultCreateInfo,
      nullptr, &renderData.rdAssimpComputeMatrixMultDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp matrix multiplication global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute matrix multiplication shader, per-model data
    VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
    assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpParentMatrixSsboBind.binding = 0;
    assimpParentMatrixSsboBind.descriptorCount = 1;
    assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpBoneOffsetSsboBind{};
    assimpBoneOffsetSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoneOffsetSsboBind.binding = 1;
    assimpBoneOffsetSsboBind.descriptorCount = 1;
    assimpBoneOffsetSsboBind.pImmutableSamplers = nullptr;
    assimpBoneOffsetSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpBoundingSpheresSsboBind{};
    assimpBoundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoundingSpheresSsboBind.binding = 2;
    assimpBoundingSpheresSsboBind.descriptorCount = 1;
    assimpBoundingSpheresSsboBind.pImmutableSamplers = nullptr;
    assimpBoundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBoundSpheresPerModelBindings =
      { assimpParentMatrixSsboBind, assimpBoneOffsetSsboBind, assimpBoundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
    assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBoundSpheresPerModelBindings.size());
    assimpMatrixMultPerModelCreateInfo.pBindings = assimpBoundSpheresPerModelBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding sphere per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute bounding spheres shader, global data
    VkDescriptorSetLayoutBinding assimpNodeMatrixSsboBind{};
    assimpNodeMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpNodeMatrixSsboBind.binding = 0;
    assimpNodeMatrixSsboBind.descriptorCount = 1;
    assimpNodeMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpNodeMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding assimpWorldPosMatricesSsboBind{};
    assimpWorldPosMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpWorldPosMatricesSsboBind.binding = 1;
    assimpWorldPosMatricesSsboBind.descriptorCount = 1;
    assimpWorldPosMatricesSsboBind.pImmutableSamplers = nullptr;
    assimpWorldPosMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding boundingSpheresSsboBind{};
    boundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSpheresSsboBind.binding = 2;
    boundingSpheresSsboBind.descriptorCount = 1;
    boundingSpheresSsboBind.pImmutableSamplers = nullptr;
    boundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
    { assimpNodeMatrixSsboBind, assimpWorldPosMatricesSsboBind, boundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
    assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
    assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultCreateInfo,
      nullptr, &renderData.rdAssimpComputeBoundingSpheresDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding spheres global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute bounding spheres shader, per-model data
    VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
    assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpParentMatrixSsboBind.binding = 0;
    assimpParentMatrixSsboBind.descriptorCount = 1;
    assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
    assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding boundingSphereAdjustmentsSsboBind{};
    boundingSphereAdjustmentsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSphereAdjustmentsSsboBind.binding = 1;
    boundingSphereAdjustmentsSsboBind.descriptorCount = 1;
    boundingSphereAdjustmentsSsboBind.pImmutableSamplers = nullptr;
    boundingSphereAdjustmentsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpMatMultPerModelBindings =
      { assimpParentMatrixSsboBind, boundingSphereAdjustmentsSsboBind};

    VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
    assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultPerModelBindings.size());
    assimpMatrixMultPerModelCreateInfo.pBindings = assimpMatMultPerModelBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpMatrixMultPerModelCreateInfo,
      nullptr, &renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding spheres per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // line shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { renderDataUboBind };

    VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
    assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
    assimpCreateInfo.pBindings = assimpBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpCreateInfo,
      nullptr, &renderData.rdLineDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp line drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // sphere shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding assimpBoundingSpheresSsboBind{};
    assimpBoundingSpheresSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    assimpBoundingSpheresSsboBind.binding = 1;
    assimpBoundingSpheresSsboBind.descriptorCount = 1;
    assimpBoundingSpheresSsboBind.pImmutableSamplers = nullptr;
    assimpBoundingSpheresSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> assimpSphereBindings =
      { renderDataUboBind, assimpBoundingSpheresSsboBind };

    VkDescriptorSetLayoutCreateInfo assimpBoundingSpheresCreateInfo{};
    assimpBoundingSpheresCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    assimpBoundingSpheresCreateInfo.bindingCount = static_cast<uint32_t>(assimpSphereBindings.size());
    assimpBoundingSpheresCreateInfo.pBindings = assimpSphereBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &assimpBoundingSpheresCreateInfo,
      nullptr, &renderData.rdSphereDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp bounding sphere drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // skybox shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
    };

    VkDescriptorSetLayoutCreateInfo skyboxCreateInfo{};
    skyboxCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    skyboxCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    skyboxCreateInfo.pBindings = layoutBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &skyboxCreateInfo,
      nullptr, &renderData.rdSkyboxDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create Assimp skybox descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // Composite shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMapDataSsboBind{};
    shadowMapDataSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapDataSsboBind.binding = 1;
    shadowMapDataSsboBind.descriptorCount = 1;
    shadowMapDataSsboBind.pImmutableSamplers = nullptr;
    shadowMapDataSsboBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding colorBinding{};
    colorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorBinding.binding = 2;
    colorBinding.descriptorCount = 1;
    colorBinding.pImmutableSamplers = nullptr;
    colorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding positionBinding{};
    positionBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    positionBinding.binding = 3;
    positionBinding.descriptorCount = 1;
    positionBinding.pImmutableSamplers = nullptr;
    positionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 4;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding selectionBinding{};
    selectionBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    selectionBinding.binding = 5;
    selectionBinding.descriptorCount = 1;
    selectionBinding.pImmutableSamplers = nullptr;
    selectionBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightSphereBinding{};
    lightSphereBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lightSphereBinding.binding = 6;
    lightSphereBinding.descriptorCount = 1;
    lightSphereBinding.pImmutableSamplers = nullptr;
    lightSphereBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoColorBinding{};
    ssaoColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorBinding.binding = 7;
    ssaoColorBinding.descriptorCount = 1;
    ssaoColorBinding.pImmutableSamplers = nullptr;
    ssaoColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoBlurBinding{};
    ssaoBlurBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBlurBinding.binding = 8;
    ssaoBlurBinding.descriptorCount = 1;
    ssaoBlurBinding.pImmutableSamplers = nullptr;
    ssaoBlurBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMapDepthBinding{};
    shadowMapDepthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapDepthBinding.binding = 9;
    shadowMapDepthBinding.descriptorCount = 1;
    shadowMapDepthBinding.pImmutableSamplers = nullptr;
    shadowMapDepthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMapCombinedDepthBinding{};
    shadowMapCombinedDepthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapCombinedDepthBinding.binding = 10;
    shadowMapCombinedDepthBinding.descriptorCount = 1;
    shadowMapCombinedDepthBinding.pImmutableSamplers = nullptr;
    shadowMapCombinedDepthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightDataSsboBind{};
    lightDataSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataSsboBind.binding = 11;
    lightDataSsboBind.descriptorCount = 1;
    lightDataSsboBind.pImmutableSamplers = nullptr;
    lightDataSsboBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      shadowMapDataSsboBind,
      colorBinding,
      positionBinding,
      normalBinding,
      selectionBinding,
      lightSphereBinding,
      ssaoColorBinding,
      ssaoBlurBinding,
      shadowMapDepthBinding,
      shadowMapCombinedDepthBinding,
      lightDataSsboBind,
    };

    VkDescriptorSetLayoutCreateInfo compositeLayoutInfo{};
    compositeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    compositeLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    compositeLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &compositeLayoutInfo,
        nullptr, &renderData.rdCompositeDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create descriptor set layout\n", __FUNCTION__);
      return false;
    }
  }

  {
    // SSAO shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 1;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 2;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoNoiseTextureBind{};
    ssaoNoiseTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoNoiseTextureBind.binding = 3;
    ssaoNoiseTextureBind.descriptorCount = 1;
    ssaoNoiseTextureBind.pImmutableSamplers = nullptr;
    ssaoNoiseTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoUboBind{};
    ssaoUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoUboBind.binding = 4;
    ssaoUboBind.descriptorCount = 1;
    ssaoUboBind.pImmutableSamplers = nullptr;
    ssaoUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      depthImageBinding,
      normalBinding,
      ssaoNoiseTextureBind,
      ssaoUboBind
    };

    VkDescriptorSetLayoutCreateInfo ssaoLayoutInfo{};
    ssaoLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssaoLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    ssaoLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &ssaoLayoutInfo,
      nullptr, &renderData.rdSSAODescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create ssao descriptor set layout\n", __FUNCTION__);
    return false;
      }
  }

  {
    // SSAO blur shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 1;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 2;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding ssaoColorBinding{};
    ssaoColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorBinding.binding = 3;
    ssaoColorBinding.descriptorCount = 1;
    ssaoColorBinding.pImmutableSamplers = nullptr;
    ssaoColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      depthImageBinding,
      normalBinding,
      ssaoColorBinding
    };

    VkDescriptorSetLayoutCreateInfo ssaoBlurLayoutInfo{};
    ssaoBlurLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssaoBlurLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    ssaoBlurLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &ssaoBlurLayoutInfo,
        nullptr, &renderData.rdSSAOBlurDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create ssao descriptor set layout\n", __FUNCTION__);
      return false;
    }
  }

  {
    // Light sphere shader
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightDataSsboBind{};
    lightDataSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataSsboBind.binding = 1;
    lightDataSsboBind.descriptorCount = 1;
    lightDataSsboBind.pImmutableSamplers = nullptr;
    lightDataSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 2;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 3;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      lightDataSsboBind,
      depthImageBinding,
      normalBinding,
    };

    VkDescriptorSetLayoutCreateInfo lightSphereLayoutInfo{};
    lightSphereLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightSphereLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    lightSphereLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &lightSphereLayoutInfo,
        nullptr, &renderData.rdLightSphereDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create light sphere descriptor set layout\n", __FUNCTION__);
      return false;
     }
  }

  {
    // Light sphere shader with shadows
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightDataSsboBind{};
    lightDataSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataSsboBind.binding = 1;
    lightDataSsboBind.descriptorCount = 1;
    lightDataSsboBind.pImmutableSamplers = nullptr;
    lightDataSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding depthImageBinding{};
    depthImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageBinding.binding = 2;
    depthImageBinding.descriptorCount = 1;
    depthImageBinding.pImmutableSamplers = nullptr;
    depthImageBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding normalBinding{};
    normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalBinding.binding = 3;
    normalBinding.descriptorCount = 1;
    normalBinding.pImmutableSamplers = nullptr;
    normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowMapDepthBinding{};
    shadowMapDepthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapDepthBinding.binding = 4;
    shadowMapDepthBinding.descriptorCount = 1;
    shadowMapDepthBinding.pImmutableSamplers = nullptr;
    shadowMapDepthBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      renderDataUboBind,
      lightDataSsboBind,
      depthImageBinding,
      normalBinding,
      shadowMapDepthBinding,
    };

    VkDescriptorSetLayoutCreateInfo lightSphereShadowLayoutInfo{};
    lightSphereShadowLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lightSphereShadowLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    lightSphereShadowLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &lightSphereShadowLayoutInfo,
        nullptr, &renderData.rdLightSphereShadowDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create light sphere shadow descriptor set layout\n", __FUNCTION__);
      return false;
     }
  }

  {
    // Swapchain copy shader
    VkDescriptorSetLayoutBinding colorBinding{};
    colorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorBinding.binding = 0;
    colorBinding.descriptorCount = 1;
    colorBinding.pImmutableSamplers = nullptr;
    colorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {
      colorBinding,
    };

    VkDescriptorSetLayoutCreateInfo swapchainCopyLayoutInfo{};
    swapchainCopyLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    swapchainCopyLayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    swapchainCopyLayoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &swapchainCopyLayoutInfo,
        nullptr, &renderData.rdSwapchainCopyDescriptorLayout) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create swapchain copy set layout\n", __FUNCTION__);
      return false;
    }
  }


  {
    // VR Controller shander
    VkDescriptorSetLayoutBinding renderDataUboBind{};
    renderDataUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderDataUboBind.binding = 0;
    renderDataUboBind.descriptorCount = 1;
    renderDataUboBind.pImmutableSamplers = nullptr;
    renderDataUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding modelRootSsboBind{};
    modelRootSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    modelRootSsboBind.binding = 1;
    modelRootSsboBind.descriptorCount = 1;
    modelRootSsboBind.pImmutableSamplers = nullptr;
    modelRootSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding shadowMapCascadeDataBind{};
    shadowMapCascadeDataBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDataBind.binding = 2;
    shadowMapCascadeDataBind.descriptorCount = 1;
    shadowMapCascadeDataBind.pImmutableSamplers = nullptr;
    shadowMapCascadeDataBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    std::vector<VkDescriptorSetLayoutBinding> vrControllerBindings = {
      renderDataUboBind,
      modelRootSsboBind,
      shadowMapCascadeDataBind
    };

    VkDescriptorSetLayoutCreateInfo vrControllerCreateInfo{};
    vrControllerCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    vrControllerCreateInfo.bindingCount = static_cast<uint32_t>(vrControllerBindings.size());
    vrControllerCreateInfo.pBindings = vrControllerBindings.data();

    result = vkCreateDescriptorSetLayout(renderData.rdVkbDevice.device, &vrControllerCreateInfo,
      nullptr, &renderData.rdVRControllerDescriptorLayout);
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create VR Controller buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  return true;
}

bool VkHelper::createDescriptorSets(VkRenderData& renderData) {
  {
    // non-animated models
    renderData.rdAssimpDescriptorSets.resize(renderData.rdNumFramesInFlight);
    // we need a matching number of layouts xD
    std::vector<VkDescriptorSetLayout> assimpLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpDescriptorLayout);

    VkDescriptorSetAllocateInfo descriptorAllocateInfo{};
    descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    descriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    descriptorAllocateInfo.pSetLayouts = assimpLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &descriptorAllocateInfo,
        renderData.rdAssimpDescriptorSets.data());
     if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated models
    renderData.rdAssimpSkinningDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpSkinningLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpSkinningDescriptorLayout);

    VkDescriptorSetAllocateInfo skinningDescriptorAllocateInfo{};
    skinningDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    skinningDescriptorAllocateInfo.pSetLayouts = assimpSkinningLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningDescriptorAllocateInfo,
      renderData.rdAssimpSkinningDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Skinning descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // selection, non-animated models
    renderData.rdAssimpSelectionDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpSelectionLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpSelectionDescriptorLayout);

    VkDescriptorSetAllocateInfo selectionDescriptorAllocateInfo{};
    selectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    selectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    selectionDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    selectionDescriptorAllocateInfo.pSetLayouts = assimpSelectionLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &selectionDescriptorAllocateInfo,
      renderData.rdAssimpSelectionDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // selection, animated models
    renderData.rdAssimpSkinningSelectionDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpSkinningSelectionLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpSkinningSelectionDescriptorLayout);

    VkDescriptorSetAllocateInfo skinningSelectionDescriptorAllocateInfo{};
    skinningSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningSelectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningSelectionDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    skinningSelectionDescriptorAllocateInfo.pSetLayouts = assimpSkinningSelectionLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningSelectionDescriptorAllocateInfo,
      renderData.rdAssimpSkinningSelectionDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // animated and morphed models
    renderData.rdAssimpSkinningMorphDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpSkinningMorphLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpSkinningMorphDescriptorLayout);

    VkDescriptorSetAllocateInfo skinningMorphDescriptorAllocateInfo{};
    skinningMorphDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningMorphDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningMorphDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    skinningMorphDescriptorAllocateInfo.pSetLayouts = assimpSkinningMorphLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningMorphDescriptorAllocateInfo,
      renderData.rdAssimpSkinningMorphDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp morph skinning descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // selection, animated and morphed models
    renderData.rdAssimpSkinningMorphSelectionDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpSkinningMorphSelectionLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpSkinningMorphSelectionDescriptorLayout);

    VkDescriptorSetAllocateInfo skinningMorphSelectionDescriptorAllocateInfo{};
    skinningMorphSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skinningMorphSelectionDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skinningMorphSelectionDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    skinningMorphSelectionDescriptorAllocateInfo.pSetLayouts = assimpSkinningMorphSelectionLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skinningMorphSelectionDescriptorAllocateInfo,
      renderData.rdAssimpSkinningMorphSelectionDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp morph skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // level
    renderData.rdAssimpLevelDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> assimpLevelLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpLevelDescriptorLayout);

    VkDescriptorSetAllocateInfo levelDescriptorAllocateInfo{};
    levelDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    levelDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    levelDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    levelDescriptorAllocateInfo.pSetLayouts = assimpLevelLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &levelDescriptorAllocateInfo,
      renderData.rdAssimpLevelDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Level descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // ground-mesh drawing
    renderData.rdGroundMeshDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> groundMeshLayouts(renderData.rdNumFramesInFlight, renderData.rdGroundMeshDescriptorLayout);

    VkDescriptorSetAllocateInfo lineAllocateInfo{};
    lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lineAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lineAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    lineAllocateInfo.pSetLayouts = groundMeshLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lineAllocateInfo,
      renderData.rdGroundMeshDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp ground-mesh drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute transform. global data
    renderData.rdAssimpComputeTransformDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeTransformLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeTransformDescriptorLayout);

    VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
    computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeTransformDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeTransformDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeTransformDescriptorAllocateInfo.pSetLayouts = computeTransformLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeTransformDescriptorAllocateInfo,
      renderData.rdAssimpComputeTransformDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // compute transform fpr bounding spheres. global data
    renderData.rdAssimpComputeSphereTransformDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeSphereTransformLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeTransformDescriptorLayout);

    VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
    computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeTransformDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeTransformDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeTransformDescriptorAllocateInfo.pSetLayouts = computeSphereTransformLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeTransformDescriptorAllocateInfo,
      renderData.rdAssimpComputeSphereTransformDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // matrix multiplication, global data
    renderData.rdAssimpComputeMatrixMultDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeMatrixMultLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeMatrixMultDescriptorLayout);

    VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};
    computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeMatrixMultDescriptorAllocateInfo.pSetLayouts = computeMatrixMultLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultDescriptorAllocateInfo,
      renderData.rdAssimpComputeMatrixMultDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // matrix multiplication bounding spheres, global data
    renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeSphereMatrixMultLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeMatrixMultDescriptorLayout);
    VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};

    computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeMatrixMultDescriptorAllocateInfo.pSetLayouts = computeSphereMatrixMultLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultDescriptorAllocateInfo,
      renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // matrix multiplication inverse kinematics, global data
    renderData.rdAssimpComputeIKDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeIKLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeMatrixMultDescriptorLayout);

    VkDescriptorSetAllocateInfo computeMatrixMultIKDescriptorAllocateInfo{};
    computeMatrixMultIKDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeMatrixMultIKDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeMatrixMultIKDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeMatrixMultIKDescriptorAllocateInfo.pSetLayouts = computeIKLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeMatrixMultIKDescriptorAllocateInfo,
      renderData.rdAssimpComputeIKDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Inverse Kinematics Matrix Mult Compute descriptor set (error: %i)\n",
        __FUNCTION__, result);
      return false;
    }
  }

  {
    // bounding spheres, global data
    renderData.rdAssimpComputeBoundingSpheresDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> computeBoundingSphereLayouts(renderData.rdNumFramesInFlight, renderData.rdAssimpComputeBoundingSpheresDescriptorLayout);

    VkDescriptorSetAllocateInfo computeBoundingSpheresDescriptorAllocateInfo{};
    computeBoundingSpheresDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    computeBoundingSpheresDescriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    computeBoundingSpheresDescriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    computeBoundingSpheresDescriptorAllocateInfo.pSetLayouts = computeBoundingSphereLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &computeBoundingSpheresDescriptorAllocateInfo,
      renderData.rdAssimpComputeBoundingSpheresDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp Bounding Sphere Compute descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // line-drawing
    renderData.rdLineDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> lineLayouts(renderData.rdNumFramesInFlight, renderData.rdLineDescriptorLayout);

    VkDescriptorSetAllocateInfo lineAllocateInfo{};
    lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lineAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lineAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    lineAllocateInfo.pSetLayouts = lineLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lineAllocateInfo,
      renderData.rdLineDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp line-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // sphere-drawing
    renderData.rdSphereDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> sphereLayouts(renderData.rdNumFramesInFlight, renderData.rdSphereDescriptorLayout);

    VkDescriptorSetAllocateInfo sphereAllocateInfo{};
    sphereAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sphereAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    sphereAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    sphereAllocateInfo.pSetLayouts = sphereLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &sphereAllocateInfo,
      renderData.rdSphereDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp bounding sphere-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // skybox
    renderData.rdSkyboxDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> skyboxLayouts(renderData.rdNumFramesInFlight, renderData.rdSkyboxDescriptorLayout);

    VkDescriptorSetAllocateInfo skyboxAllocateInfo{};
    skyboxAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    skyboxAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    skyboxAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    skyboxAllocateInfo.pSetLayouts = skyboxLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &skyboxAllocateInfo,
      renderData.rdSkyboxDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp skybox descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // composite
    renderData.rdCompositeDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> compositeLayouts(renderData.rdNumFramesInFlight, renderData.rdCompositeDescriptorLayout);

    VkDescriptorSetAllocateInfo compositeAllocInfo{};
    compositeAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    compositeAllocInfo.descriptorPool = renderData.rdDescriptorPool;
    compositeAllocInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    compositeAllocInfo.pSetLayouts = compositeLayouts.data();

    if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &compositeAllocInfo,
        renderData.rdCompositeDescriptorSets.data()) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate memory for composite descriptor set", __FUNCTION__);
      return false;
    }
  }

  {
    // SSAO
    renderData.rdSSAODescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> ssaoLayouts(renderData.rdNumFramesInFlight, renderData.rdSSAODescriptorLayout);

    VkDescriptorSetAllocateInfo ssaoAllocInfo{};
    ssaoAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ssaoAllocInfo.descriptorPool = renderData.rdDescriptorPool;
    ssaoAllocInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    ssaoAllocInfo.pSetLayouts = ssaoLayouts.data();

    if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &ssaoAllocInfo,
        renderData.rdSSAODescriptorSets.data()) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate memory for ssao descriptor set", __FUNCTION__);
      return false;
    }
  }

  {
    // SSAO blur
    renderData.rdSSAOBlurDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> ssaoBlurLayouts(renderData.rdNumFramesInFlight, renderData.rdSSAOBlurDescriptorLayout);

    VkDescriptorSetAllocateInfo ssaoBlurAllocInfo{};
    ssaoBlurAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ssaoBlurAllocInfo.descriptorPool = renderData.rdDescriptorPool;
    ssaoBlurAllocInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    ssaoBlurAllocInfo.pSetLayouts = ssaoBlurLayouts.data();

    if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &ssaoBlurAllocInfo,
        renderData.rdSSAOBlurDescriptorSets.data()) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate memory for ssao blur descriptor set", __FUNCTION__);
      return false;
    }
  }

  {
    // dyn light debug sphere-drawing
    renderData.rdDynLightDebugSphereDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> dynlightDebugLayouts(renderData.rdNumFramesInFlight, renderData.rdSphereDescriptorLayout);

    VkDescriptorSetAllocateInfo sphereAllocateInfo{};
    sphereAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sphereAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    sphereAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    sphereAllocateInfo.pSetLayouts = dynlightDebugLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &sphereAllocateInfo,
      renderData.rdDynLightDebugSphereDescriptorSets.data());

    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate Assimp dyn light debug sphere-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // light sphere drawing
    renderData.rdLightSphereDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> ligthSphereLayouts(renderData.rdNumFramesInFlight, renderData.rdLightSphereDescriptorLayout);

    VkDescriptorSetAllocateInfo lightSphereAllocateInfo{};
    lightSphereAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightSphereAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lightSphereAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    lightSphereAllocateInfo.pSetLayouts = ligthSphereLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lightSphereAllocateInfo,
      renderData.rdLightSphereDescriptorSets.data());

    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate light sphere descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // light sphere drawing with shadows
    renderData.rdLightSphereShadowsDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> ligthSphereShadowsLayouts(renderData.rdNumFramesInFlight, renderData.rdLightSphereShadowDescriptorLayout);

    VkDescriptorSetAllocateInfo lightSphereShadowAllocateInfo{};
    lightSphereShadowAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    lightSphereShadowAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    lightSphereShadowAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    lightSphereShadowAllocateInfo.pSetLayouts = ligthSphereShadowsLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &lightSphereShadowAllocateInfo,
      renderData.rdLightSphereShadowsDescriptorSets.data());

    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate light spheres shadow descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  {
    // swapchain copy
    renderData.rdSwapchainCopyDescriptorSets.resize(renderData.rdNumFramesInFlight);
    std::vector<VkDescriptorSetLayout> swapchainCopyLayouts(renderData.rdNumFramesInFlight, renderData.rdSwapchainCopyDescriptorLayout);

    VkDescriptorSetAllocateInfo swapchainCopyAllocInfo{};
    swapchainCopyAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    swapchainCopyAllocInfo.descriptorPool = renderData.rdDescriptorPool;
    swapchainCopyAllocInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    swapchainCopyAllocInfo.pSetLayouts = swapchainCopyLayouts.data();

    if (vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &swapchainCopyAllocInfo,
        renderData.rdSwapchainCopyDescriptorSets.data()) != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate memory for swapchain copy descriptor set", __FUNCTION__);
      return false;
    }
  }

  {
    // VR Controllers
    renderData.rdVRControllerDescriptorSets.resize(renderData.rdNumFramesInFlight);
    // we need a matching number of layouts xD
    std::vector<VkDescriptorSetLayout> vrControllerLayouts(renderData.rdNumFramesInFlight, renderData.rdVRControllerDescriptorLayout);

    VkDescriptorSetAllocateInfo descriptorAllocateInfo{};
    descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocateInfo.descriptorPool = renderData.rdDescriptorPool;
    descriptorAllocateInfo.descriptorSetCount = static_cast<uint32_t>(renderData.rdNumFramesInFlight);
    descriptorAllocateInfo.pSetLayouts = vrControllerLayouts.data();

    VkResult result = vkAllocateDescriptorSets(renderData.rdVkbDevice.device, &descriptorAllocateInfo,
      renderData.rdVRControllerDescriptorSets.data());
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not allocate VR Controller descriptor set (error: %i)\n", __FUNCTION__, result);
      return false;
    }
  }

  updateDescriptorSets(renderData);
  updateComputeDescriptorSets(renderData);
  updateLevelDescriptorSets(renderData);
  updateSphereComputeDescriptorSets(renderData);
  updateIKComputeDescriptorSets(renderData);

  return true;
}

void VkHelper::updateDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
  // we must update the descriptor sets whenever the buffer size has changed
  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // non-animated shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 2;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet shadowMapCascadeDescriptorSet{};
    shadowMapCascadeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDescriptorSet.dstSet = renderData.rdAssimpDescriptorSets.at(i);
    shadowMapCascadeDescriptorSet.dstBinding = 3;
    shadowMapCascadeDescriptorSet.descriptorCount = 1;
    shadowMapCascadeDescriptorSet.pBufferInfo = &shadowMapCascadeInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      posWriteDescriptorSet,
      selectionWriteDescriptorSet,
      shadowMapCascadeDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
       writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // animated shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet shadowMapCascadeDescriptorSet{};
    shadowMapCascadeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDescriptorSet.dstSet = renderData.rdAssimpSkinningDescriptorSets.at(i);
    shadowMapCascadeDescriptorSet.dstBinding = 4;
    shadowMapCascadeDescriptorSet.descriptorCount = 1;
    shadowMapCascadeDescriptorSet.pBufferInfo = &shadowMapCascadeInfo;

    std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets = {
      matrixWriteDescriptorSet,
      boneMatrixWriteDescriptorSet,
      posWriteDescriptorSet,
      selectionWriteDescriptorSet,
      shadowMapCascadeDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
       skinningWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // selection shader, non-animated 
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSelectionDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 2;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> selectionWriteDescriptorSets =
      { matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(selectionWriteDescriptorSets.size()),
       selectionWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // selection shader, animated 
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningSelectionDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
      { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
        posWriteDescriptorSet, selectionWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
       skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // animated plus morph shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo faceAnimInfo{};
    faceAnimInfo.buffer = renderData.rdFaceAnimPerInstanceDataBuffers.at(i).buffer;
    faceAnimInfo.offset = 0;
    faceAnimInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet faceAnimWriteDescriptorSet{};
    faceAnimWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    faceAnimWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    faceAnimWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    faceAnimWriteDescriptorSet.dstBinding = 4;
    faceAnimWriteDescriptorSet.descriptorCount = 1;
    faceAnimWriteDescriptorSet.pBufferInfo = &faceAnimInfo;

    VkWriteDescriptorSet shadowMapCascadeDescriptorSet{};
    shadowMapCascadeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphDescriptorSets.at(i);
    shadowMapCascadeDescriptorSet.dstBinding = 5;
    shadowMapCascadeDescriptorSet.descriptorCount = 1;
    shadowMapCascadeDescriptorSet.pBufferInfo = &shadowMapCascadeInfo;

    std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets = {
      matrixWriteDescriptorSet,
      boneMatrixWriteDescriptorSet,
      posWriteDescriptorSet,
      selectionWriteDescriptorSet,
      faceAnimWriteDescriptorSet,
      shadowMapCascadeDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
       skinningWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // selection shader, animated plus morph
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo selectionInfo{};
    selectionInfo.buffer = renderData.rdSelectedInstanceBuffers.at(i).buffer;
    selectionInfo.offset = 0;
    selectionInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo faceAnimInfo{};
    faceAnimInfo.buffer = renderData.rdFaceAnimPerInstanceDataBuffers.at(i).buffer;
    faceAnimInfo.offset = 0;
    faceAnimInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 2;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet selectionWriteDescriptorSet{};
    selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    selectionWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(i);
    selectionWriteDescriptorSet.dstBinding = 3;
    selectionWriteDescriptorSet.descriptorCount = 1;
    selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

    VkWriteDescriptorSet faceAnimWriteDescriptorSet{};
    faceAnimWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    faceAnimWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    faceAnimWriteDescriptorSet.dstSet = renderData.rdAssimpSkinningMorphSelectionDescriptorSets.at(i);
    faceAnimWriteDescriptorSet.dstBinding = 4;
    faceAnimWriteDescriptorSet.descriptorCount = 1;
    faceAnimWriteDescriptorSet.pBufferInfo = &faceAnimInfo;

    std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
    { matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
      posWriteDescriptorSet, selectionWriteDescriptorSet, faceAnimWriteDescriptorSet };

      vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
        skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // line-drawing shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdLineDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // ground-mesh-drawing shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdGroundMeshDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // skybox shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdSkyboxDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // VR Controllers
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdVRControllerModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdVRControllerDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdVRControllerDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet shadowMapCascadeDescriptorSet{};
    shadowMapCascadeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDescriptorSet.dstSet = renderData.rdVRControllerDescriptorSets.at(i);
    shadowMapCascadeDescriptorSet.dstBinding = 2;
    shadowMapCascadeDescriptorSet.descriptorCount = 1;
    shadowMapCascadeDescriptorSet.pBufferInfo = &shadowMapCascadeInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      posWriteDescriptorSet,
      shadowMapCascadeDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating compute descriptor sets\n", __FUNCTION__);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // transform compute shader
    VkDescriptorBufferInfo transformInfo{};
    transformInfo.buffer = renderData.rdPerInstanceAnimDataBuffers.at(i).buffer;
    transformInfo.offset = 0;
    transformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdShaderTRSMatrixBuffers.at(i).buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet transformWriteDescriptorSet{};
    transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformWriteDescriptorSet.dstSet = renderData.rdAssimpComputeTransformDescriptorSets.at(i);
    transformWriteDescriptorSet.dstBinding = 0;
    transformWriteDescriptorSet.descriptorCount = 1;
    transformWriteDescriptorSet.pBufferInfo = &transformInfo;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeTransformDescriptorSets.at(i);
    trsWriteDescriptorSet.dstBinding = 1;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
      { transformWriteDescriptorSet, trsWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(transformWriteDescriptorSets.size()),
       transformWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // matrix multiplication compute shader, global data
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdShaderTRSMatrixBuffers.at(i).buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdShaderBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeMatrixMultDescriptorSets.at(i);
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeMatrixMultDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateLevelDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating level descriptor sets\n", __FUNCTION__);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // level shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdShaderLevelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdAssimpLevelDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet posWriteDescriptorSet{};
    posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    posWriteDescriptorSet.dstSet = renderData.rdAssimpLevelDescriptorSets.at(i);
    posWriteDescriptorSet.dstBinding = 1;
    posWriteDescriptorSet.descriptorCount = 1;
    posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet shadowMapCascadeDescriptorSet{};
    shadowMapCascadeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeDescriptorSet.dstSet = renderData.rdAssimpLevelDescriptorSets.at(i);
    shadowMapCascadeDescriptorSet.dstBinding = 2;
    shadowMapCascadeDescriptorSet.descriptorCount = 1;
    shadowMapCascadeDescriptorSet.pBufferInfo = &shadowMapCascadeInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      posWriteDescriptorSet,
      shadowMapCascadeDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateSphereComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating sphere descriptor sets\n", __FUNCTION__);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // transform compute shader for bounding spheres
    VkDescriptorBufferInfo transformInfo{};
    transformInfo.buffer = renderData.rdSpherePerInstanceAnimDataBuffers.at(i).buffer;
    transformInfo.offset = 0;
    transformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdSphereTRSMatrixBuffers.at(i).buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet transformWriteDescriptorSet{};
    transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    transformWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereTransformDescriptorSets.at(i);
    transformWriteDescriptorSet.dstBinding = 0;
    transformWriteDescriptorSet.descriptorCount = 1;
    transformWriteDescriptorSet.pBufferInfo = &transformInfo;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereTransformDescriptorSets.at(i);
    trsWriteDescriptorSet.dstBinding = 1;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
      { transformWriteDescriptorSet, trsWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(transformWriteDescriptorSets.size()),
       transformWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // matrix multiplication bounding spheres compute shader, global data
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdSphereTRSMatrixBuffers.at(i).buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdSphereBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.at(i);
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // bounding spheres compute shader, global data
    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdSphereBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo worldPosInfo{};
    worldPosInfo.buffer = renderData.rdSphereModelRootMatrixBuffers.at(i).buffer;
    worldPosInfo.offset = 0;
    worldPosInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boundingSphereInfo{};
    boundingSphereInfo.buffer = renderData.rdBoundingSphereBuffers.at(i).buffer;
    boundingSphereInfo.offset = 0;
    boundingSphereInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 0;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    VkWriteDescriptorSet worldPosWriteDescriptorSet{};
    worldPosWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    worldPosWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    worldPosWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSets.at(i);
    worldPosWriteDescriptorSet.dstBinding = 1;
    worldPosWriteDescriptorSet.descriptorCount = 1;
    worldPosWriteDescriptorSet.pBufferInfo = &worldPosInfo;

    VkWriteDescriptorSet boundingSphereWriteDescriptorSet{};
    boundingSphereWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boundingSphereWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSphereWriteDescriptorSet.dstSet = renderData.rdAssimpComputeBoundingSpheresDescriptorSets.at(i);
    boundingSphereWriteDescriptorSet.dstBinding = 2;
    boundingSphereWriteDescriptorSet.descriptorCount = 1;
    boundingSphereWriteDescriptorSet.pBufferInfo = &boundingSphereInfo;

    std::vector<VkWriteDescriptorSet> boundingSphereWriteDescriptorSets =
      { boneMatrixWriteDescriptorSet, worldPosWriteDescriptorSet, boundingSphereWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(boundingSphereWriteDescriptorSets.size()),
       boundingSphereWriteDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // sphere-drawing shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boundingSpheresInfo{};
    boundingSpheresInfo.buffer = renderData.rdBoundingSphereBuffers.at(i).buffer;
    boundingSpheresInfo.offset = 0;
    boundingSpheresInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdSphereDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet boundingSpheresWriteDescriptorSet{};
    boundingSpheresWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boundingSpheresWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boundingSpheresWriteDescriptorSet.dstSet = renderData.rdSphereDescriptorSets.at(i);
    boundingSpheresWriteDescriptorSet.dstBinding = 1;
    boundingSpheresWriteDescriptorSet.descriptorCount = 1;
    boundingSpheresWriteDescriptorSet.pBufferInfo = &boundingSpheresInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets =
    { matrixWriteDescriptorSet, boundingSpheresWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateIKComputeDescriptorSets(VkRenderData& renderData) {
  Logger::log(1, "%s: updating IK descriptor sets\n", __FUNCTION__);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // matrix multiplication inverse kinematics compute shader, global data
    VkDescriptorBufferInfo trsInfo{};
    trsInfo.buffer = renderData.rdIKTRSMatrixBuffers.at(i).buffer;
    trsInfo.offset = 0;
    trsInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boneMatrixInfo{};
    boneMatrixInfo.buffer = renderData.rdIKBoneMatrixBuffers.at(i).buffer;
    boneMatrixInfo.offset = 0;
    boneMatrixInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet trsWriteDescriptorSet{};
    trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    trsWriteDescriptorSet.dstSet = renderData.rdAssimpComputeIKDescriptorSets.at(i);
    trsWriteDescriptorSet.dstBinding = 0;
    trsWriteDescriptorSet.descriptorCount = 1;
    trsWriteDescriptorSet.pBufferInfo = &trsInfo;

    VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
    boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    boneMatrixWriteDescriptorSet.dstSet = renderData.rdAssimpComputeIKDescriptorSets.at(i);
    boneMatrixWriteDescriptorSet.dstBinding = 1;
    boneMatrixWriteDescriptorSet.descriptorCount = 1;
    boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

    std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
      { trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
       matrixMultWriteDescriptorSets.data(), 0, nullptr);
  }
}

void VkHelper::updateImageDescriptorSets(VkRenderData& renderData) {
  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // composite
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo shadowMapCascadeInfo{};
    shadowMapCascadeInfo.buffer = renderData.rdShadowMapCascadeDataBuffers.at(i).buffer;
    shadowMapCascadeInfo.offset = 0;
    shadowMapCascadeInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo colorInfo{};
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorInfo.imageView = renderData.rdGBuffer.color.imageView;
    colorInfo.sampler = renderData.rdGBuffer.color.sampler;

    VkDescriptorImageInfo positionInfo{};
    positionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    positionInfo.imageView = renderData.rdGBuffer.depth.imageView;
    positionInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo selectionInfo{};
    selectionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    selectionInfo.imageView = renderData.rdSelectionImageData.imageView;
    selectionInfo.sampler = renderData.rdSelectionImageData.sampler;

    VkDescriptorImageInfo lightSphereInfo{};
    lightSphereInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lightSphereInfo.imageView = renderData.rdLightSpheresBufferData.imageView;
    lightSphereInfo.sampler = renderData.rdLightSpheresBufferData.sampler;

    VkDescriptorImageInfo ssaoColorInfo{};
    ssaoColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoColorInfo.imageView = renderData.rdSSAOColorBufferData.imageView;
    ssaoColorInfo.sampler = renderData.rdSSAOColorBufferData.sampler;

    VkDescriptorImageInfo ssaoBlurInfo{};
    ssaoBlurInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoBlurInfo.imageView = renderData.rdSSAOBlurBufferData.imageView;
    ssaoBlurInfo.sampler = renderData.rdSSAOBlurBufferData.sampler;

    VkDescriptorImageInfo shadowMapDepthInfo{};
    shadowMapDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowMapDepthInfo.imageView = renderData.rdShadowMapDepthBufferData.imageView;
    shadowMapDepthInfo.sampler = renderData.rdShadowMapDepthBufferData.sampler;

    VkDescriptorImageInfo shadowMapCombinedDepthInfo{};
    shadowMapCombinedDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowMapCombinedDepthInfo.imageView = renderData.rdShadowMapCombinedDepthBufferData.imageView;
    shadowMapCombinedDepthInfo.sampler = renderData.rdShadowMapCombinedDepthBufferData.sampler;

    VkDescriptorBufferInfo lightDataInfo{};
    lightDataInfo.buffer = renderData.rdDynamicLightBuffers.at(i).buffer;
    lightDataInfo.offset = 0;
    lightDataInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet shadowMapCascadeWrite{};
    shadowMapCascadeWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCascadeWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shadowMapCascadeWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    shadowMapCascadeWrite.dstBinding = 1;
    shadowMapCascadeWrite.descriptorCount = 1;
    shadowMapCascadeWrite.pBufferInfo = &shadowMapCascadeInfo;

    VkWriteDescriptorSet colorWrite{};
    colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorWrite.descriptorCount = 1;
    colorWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    colorWrite.dstBinding = 2;
    colorWrite.pImageInfo = &colorInfo;

    VkWriteDescriptorSet positionWrite{};
    positionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    positionWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    positionWrite.descriptorCount = 1;
    positionWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    positionWrite.dstBinding = 3;
    positionWrite.pImageInfo = &positionInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    normalWrite.dstBinding = 4;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet selectionWrite{};
    selectionWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    selectionWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    selectionWrite.descriptorCount = 1;
    selectionWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    selectionWrite.dstBinding = 5;
    selectionWrite.pImageInfo = &selectionInfo;

    VkWriteDescriptorSet lightSphereWrite{};
    lightSphereWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightSphereWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lightSphereWrite.descriptorCount = 1;
    lightSphereWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    lightSphereWrite.dstBinding = 6;
    lightSphereWrite.pImageInfo = &lightSphereInfo;

    VkWriteDescriptorSet ssaoColorWrite{};
    ssaoColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorWrite.descriptorCount = 1;
    ssaoColorWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    ssaoColorWrite.dstBinding = 7;
    ssaoColorWrite.pImageInfo = &ssaoColorInfo;

    VkWriteDescriptorSet ssaoBlurWrite{};
    ssaoBlurWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoBlurWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoBlurWrite.descriptorCount = 1;
    ssaoBlurWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    ssaoBlurWrite.dstBinding = 8;
    ssaoBlurWrite.pImageInfo = &ssaoBlurInfo;

    VkWriteDescriptorSet shadowMapDepthWrite{};
    shadowMapDepthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapDepthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapDepthWrite.descriptorCount = 1;
    shadowMapDepthWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    shadowMapDepthWrite.dstBinding = 9;
    shadowMapDepthWrite.pImageInfo = &shadowMapDepthInfo;

    VkWriteDescriptorSet shadowMapCombinedDepthWrite{};
    shadowMapCombinedDepthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapCombinedDepthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapCombinedDepthWrite.descriptorCount = 1;
    shadowMapCombinedDepthWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    shadowMapCombinedDepthWrite.dstBinding = 10;
    shadowMapCombinedDepthWrite.pImageInfo = &shadowMapCombinedDepthInfo;

    VkWriteDescriptorSet lightDataWrite{};
    lightDataWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightDataWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataWrite.dstSet = renderData.rdCompositeDescriptorSets.at(i);
    lightDataWrite.dstBinding = 11;
    lightDataWrite.descriptorCount = 1;
    lightDataWrite.pBufferInfo = &lightDataInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      shadowMapCascadeWrite,
      colorWrite,
      positionWrite,
      normalWrite,
      selectionWrite,
      lightSphereWrite,
      ssaoColorWrite,
      ssaoBlurWrite,
      shadowMapDepthWrite,
      shadowMapCombinedDepthWrite,
      lightDataWrite,
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // SSAO
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthImageInfo.imageView = renderData.rdGBuffer.depth.imageView;
    depthImageInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo ssaoNoiseInfo{};
    ssaoNoiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoNoiseInfo.imageView = renderData.rdSSAONoiseBufferData.imageView;
    ssaoNoiseInfo.sampler = renderData.rdSSAONoiseBufferData.sampler;

    VkDescriptorBufferInfo ssaoKernelInfo{};
    ssaoKernelInfo.buffer = renderData.rdSSAOKernelSamplesUBO.buffer;
    ssaoKernelInfo.offset = 0;
    ssaoKernelInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdSSAODescriptorSets.at(i);
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet depthImageWrite{};
    depthImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWrite.descriptorCount = 1;
    depthImageWrite.dstSet = renderData.rdSSAODescriptorSets.at(i);
    depthImageWrite.dstBinding = 1;
    depthImageWrite.pImageInfo = &depthImageInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdSSAODescriptorSets.at(i);
    normalWrite.dstBinding = 2;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet ssaoNoiseWrite{};
    ssaoNoiseWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoNoiseWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoNoiseWrite.descriptorCount = 1;
    ssaoNoiseWrite.dstSet = renderData.rdSSAODescriptorSets.at(i);
    ssaoNoiseWrite.dstBinding = 3;
    ssaoNoiseWrite.pImageInfo = &ssaoNoiseInfo;

    VkWriteDescriptorSet ssaoKernelWrite{};
    ssaoKernelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoKernelWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssaoKernelWrite.descriptorCount = 1;
    ssaoKernelWrite.dstSet = renderData.rdSSAODescriptorSets.at(i);
    ssaoKernelWrite.dstBinding = 4;
    ssaoKernelWrite.pBufferInfo = &ssaoKernelInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      depthImageWrite,
      normalWrite,
      ssaoNoiseWrite,
      ssaoKernelWrite };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // SSAO blur
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo positionInfo{};
    positionInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    positionInfo.imageView = renderData.rdGBuffer.depth.imageView;
    positionInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo ssaoColorInfo{};
    ssaoColorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ssaoColorInfo.imageView = renderData.rdSSAOColorBufferData.imageView;
    ssaoColorInfo.sampler = renderData.rdSSAOColorBufferData.sampler;

    VkWriteDescriptorSet matrixWrite{};
    matrixWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWrite.descriptorCount = 1;
    matrixWrite.dstSet = renderData.rdSSAOBlurDescriptorSets.at(i);
    matrixWrite.dstBinding = 0;
    matrixWrite.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet depthImageWrite{};
    depthImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWrite.descriptorCount = 1;
    depthImageWrite.dstSet = renderData.rdSSAOBlurDescriptorSets.at(i);
    depthImageWrite.dstBinding = 1;
    depthImageWrite.pImageInfo = &positionInfo;

    VkWriteDescriptorSet normalWrite{};
    normalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWrite.descriptorCount = 1;
    normalWrite.dstSet = renderData.rdSSAOBlurDescriptorSets.at(i);
    normalWrite.dstBinding = 2;
    normalWrite.pImageInfo = &normalInfo;

    VkWriteDescriptorSet ssaoColorWrite{};
    ssaoColorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ssaoColorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoColorWrite.descriptorCount = 1;
    ssaoColorWrite.dstSet = renderData.rdSSAOBlurDescriptorSets.at(i);
    ssaoColorWrite.dstBinding = 3;
    ssaoColorWrite.pImageInfo = &ssaoColorInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      matrixWrite,
      depthImageWrite,
      normalWrite,
      ssaoColorWrite
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // sphere-drawing shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo debugSpheresInfo{};
    debugSpheresInfo.buffer = renderData.rdDynamicLightDebugBuffers.at(i).buffer;
    debugSpheresInfo.offset = 0;
    debugSpheresInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdDynLightDebugSphereDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet debugSpheresWriteDescriptorSet{};
    debugSpheresWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    debugSpheresWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    debugSpheresWriteDescriptorSet.dstSet = renderData.rdDynLightDebugSphereDescriptorSets.at(i);
    debugSpheresWriteDescriptorSet.dstBinding = 1;
    debugSpheresWriteDescriptorSet.descriptorCount = 1;
    debugSpheresWriteDescriptorSet.pBufferInfo = &debugSpheresInfo;


    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      debugSpheresWriteDescriptorSet
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // light sphere drawing shader
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo lightDataInfo{};
    lightDataInfo.buffer = renderData.rdDynamicLightBuffers.at(i).buffer;
    lightDataInfo.offset = 0;
    lightDataInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthImageInfo.imageView = renderData.rdGBuffer.depth.imageView;
    depthImageInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdLightSphereDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet lightDataWriteDescriptorSet{};
    lightDataWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightDataWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataWriteDescriptorSet.dstSet = renderData.rdLightSphereDescriptorSets.at(i);
    lightDataWriteDescriptorSet.dstBinding = 1;
    lightDataWriteDescriptorSet.descriptorCount = 1;
    lightDataWriteDescriptorSet.pBufferInfo = &lightDataInfo;

    VkWriteDescriptorSet depthImageWriteDescriptorSet{};
    depthImageWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWriteDescriptorSet.dstSet = renderData.rdLightSphereDescriptorSets.at(i);
    depthImageWriteDescriptorSet.dstBinding = 2;
    depthImageWriteDescriptorSet.descriptorCount = 1;
    depthImageWriteDescriptorSet.pImageInfo = &depthImageInfo;

    VkWriteDescriptorSet normalWriteDescriptorSet{};
    normalWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWriteDescriptorSet.dstSet = renderData.rdLightSphereDescriptorSets.at(i);
    normalWriteDescriptorSet.dstBinding = 3;
    normalWriteDescriptorSet.descriptorCount = 1;
    normalWriteDescriptorSet.pImageInfo = &normalInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      lightDataWriteDescriptorSet,
      depthImageWriteDescriptorSet,
      normalWriteDescriptorSet,
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // light sphere drawing shader with shadows
    VkDescriptorBufferInfo matrixInfo{};
    matrixInfo.buffer = renderData.rdRenderUploadDataUBOs.at(i).buffer;
    matrixInfo.offset = 0;
    matrixInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo lightDataInfo{};
    lightDataInfo.buffer = renderData.rdDynamicLightBuffers.at(i).buffer;
    lightDataInfo.offset = 0;
    lightDataInfo.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depthImageInfo.imageView = renderData.rdGBuffer.depth.imageView;
    depthImageInfo.sampler = renderData.rdGBuffer.depth.sampler;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = renderData.rdGBuffer.normal.imageView;
    normalInfo.sampler = renderData.rdGBuffer.normal.sampler;

    VkDescriptorImageInfo shadowMapDepthInfo{};
    shadowMapDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    shadowMapDepthInfo.imageView = renderData.rdDynamicLightShadowData.imageView;
    shadowMapDepthInfo.sampler = renderData.rdDynamicLightShadowData.sampler;

    VkWriteDescriptorSet matrixWriteDescriptorSet{};
    matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    matrixWriteDescriptorSet.dstSet = renderData.rdLightSphereShadowsDescriptorSets.at(i);
    matrixWriteDescriptorSet.dstBinding = 0;
    matrixWriteDescriptorSet.descriptorCount = 1;
    matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

    VkWriteDescriptorSet lightDataWriteDescriptorSet{};
    lightDataWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    lightDataWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    lightDataWriteDescriptorSet.dstSet = renderData.rdLightSphereShadowsDescriptorSets.at(i);
    lightDataWriteDescriptorSet.dstBinding = 1;
    lightDataWriteDescriptorSet.descriptorCount = 1;
    lightDataWriteDescriptorSet.pBufferInfo = &lightDataInfo;

    VkWriteDescriptorSet depthImageWriteDescriptorSet{};
    depthImageWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    depthImageWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthImageWriteDescriptorSet.dstSet = renderData.rdLightSphereShadowsDescriptorSets.at(i);
    depthImageWriteDescriptorSet.dstBinding = 2;
    depthImageWriteDescriptorSet.descriptorCount = 1;
    depthImageWriteDescriptorSet.pImageInfo = &depthImageInfo;

    VkWriteDescriptorSet normalWriteDescriptorSet{};
    normalWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    normalWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalWriteDescriptorSet.dstSet = renderData.rdLightSphereShadowsDescriptorSets.at(i);
    normalWriteDescriptorSet.dstBinding = 3;
    normalWriteDescriptorSet.descriptorCount = 1;
    normalWriteDescriptorSet.pImageInfo = &normalInfo;

    VkWriteDescriptorSet shadowMapDepthWriteDescriptorSet{};
    shadowMapDepthWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    shadowMapDepthWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowMapDepthWriteDescriptorSet.dstSet = renderData.rdLightSphereShadowsDescriptorSets.at(i);
    shadowMapDepthWriteDescriptorSet.dstBinding = 4;
    shadowMapDepthWriteDescriptorSet.descriptorCount = 1;
    shadowMapDepthWriteDescriptorSet.pImageInfo = &shadowMapDepthInfo;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
      matrixWriteDescriptorSet,
      lightDataWriteDescriptorSet,
      depthImageWriteDescriptorSet,
      normalWriteDescriptorSet,
      shadowMapDepthWriteDescriptorSet,
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
  }

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    // swapchain copy
    VkDescriptorImageInfo colorInfo{};
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorInfo.imageView = renderData.rdFinalImageData.imageView;
    colorInfo.sampler = renderData.rdFinalImageData.sampler;

    VkWriteDescriptorSet colorWrite{};
    colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    colorWrite.descriptorCount = 1;
    colorWrite.dstSet = renderData.rdSwapchainCopyDescriptorSets.at(i);
    colorWrite.dstBinding = 0;
    colorWrite.pImageInfo = &colorInfo;

    std::vector<VkWriteDescriptorSet> writeSets = {
      colorWrite,
    };

    vkUpdateDescriptorSets(renderData.rdVkbDevice.device, static_cast<uint32_t>(writeSets.size()), writeSets.data(), 0, nullptr);
  }
}

bool VkHelper::createPipelineLayouts(VkRenderData& renderData) {
  // non-animated model
  std::vector<VkDescriptorSetLayout> layouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpDescriptorLayout };

  std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants) } };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpPipelineLayout, layouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp pipeline layout\n", __FUNCTION__);
    return false;
  }

  // animated model, needs push constant
  std::vector<VkDescriptorSetLayout> skinningLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningPipelineLayout, skinningLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp Skinning pipeline layout\n", __FUNCTION__);
    return false;
  }

  // selection, non-animated
  std::vector<VkDescriptorSetLayout> selectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSelectionDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSelectionPipelineLayout, selectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  // selection, animated
  std::vector<VkDescriptorSetLayout> skinningSelectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningSelectionDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningSelectionPipelineLayout, skinningSelectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp skinning selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  // animated model plus morph
  std::vector<VkDescriptorSetLayout> skinningMorphLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningMorphDescriptorLayout,
    renderData.rdAssimpSkinningMorphPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningMorphPipelineLayout, skinningMorphLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp morph skinning pipeline layout\n", __FUNCTION__);
    return false;
  }

  // selection, animated, morphs
  std::vector<VkDescriptorSetLayout> skinningMorphSelectionLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpSkinningMorphSelectionDescriptorLayout,
    renderData.rdAssimpSkinningMorphPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpSkinningMorphSelectionPipelineLayout, skinningMorphSelectionLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp morph skinning selection pipeline layout\n", __FUNCTION__);
    return false;
  }

  // level
  std::vector<VkDescriptorSetLayout> levelLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdAssimpLevelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpLevelPipelineLayout, levelLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp Level pipeline layout\n", __FUNCTION__);
    return false;
  }

  // ground mesh drawing
  std::vector<VkDescriptorSetLayout> groundMeshLayouts = {
    renderData.rdGroundMeshDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdGroundMeshPipelineLayout, groundMeshLayouts)) {
    Logger::log(1, "%s error: could not init Assimp ground mesh drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  // transform compute
  std::vector<VkPushConstantRange> computePushConstants = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkComputePushConstants) } };

  std::vector<VkDescriptorSetLayout> transformLayouts = {
    renderData.rdAssimpComputeTransformDescriptorLayout,
    renderData.rdAssimpComputeTransformPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout, transformLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp transform compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  // matrix mult compute
  std::vector<VkDescriptorSetLayout> matrixMultLayouts = {
    renderData.rdAssimpComputeMatrixMultDescriptorLayout,
    renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout, matrixMultLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp matrix multiplication compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  // bounding spheres compute
  std::vector<VkDescriptorSetLayout> boundingSpheresLayouts = {
    renderData.rdAssimpComputeBoundingSpheresDescriptorLayout,
    renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout, boundingSpheresLayouts, computePushConstants)) {
    Logger::log(1, "%s error: could not init Assimp bounding spheres compute pipeline layout\n", __FUNCTION__);
    return false;
  }

  // line drawing
  std::vector<VkDescriptorSetLayout> lineLayouts = {
    renderData.rdLineDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdLinePipelineLayout, lineLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp line drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  // sphere drawing
  std::vector<VkDescriptorSetLayout> sphereLayouts = {
    renderData.rdSphereDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSpherePipelineLayout, sphereLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init Assimp sphere drawing pipeline layout\n", __FUNCTION__);
    return false;
  }

  // skybox
  std::vector<VkDescriptorSetLayout> skyboxLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdSkyboxDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSkyboxPipelineLayout, skyboxLayouts)) {
    Logger::log(1, "%s error: could not init Assimp skybox pipeline layout\n", __FUNCTION__);
    return false;
  }

  // composite pass of deferred shading
  std::vector<VkDescriptorSetLayout> compositeLayout = {
    renderData.rdCompositeDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdCompositePipelineLayout, compositeLayout)) {
    Logger::log(1, "%s error: could not init Composite pipeline layout\n", __FUNCTION__);
    return false;
  }

  // ssao pass
  std::vector<VkDescriptorSetLayout> ssaoLayout = {
    renderData.rdSSAODescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSSAOPipelineLayout, ssaoLayout)) {
    Logger::log(1, "%s error: could not init SSAO pipeline layout\n", __FUNCTION__);
    return false;
  }

  // ssao blur pass
  std::vector<VkDescriptorSetLayout> ssaoBlurLayout = {
    renderData.rdSSAOBlurDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSSAOBlurPipelineLayout, ssaoBlurLayout)) {
    Logger::log(1, "%s error: could not init SSAO blur pipeline layout\n", __FUNCTION__);
    return false;
  }

  // light sphere pass pass
  std::vector<VkDescriptorSetLayout> lightSphereLayout = {
    renderData.rdLightSphereDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdLightSpherePipelineLayout, lightSphereLayout)) {
    Logger::log(1, "%s error: could not init light sphere pipeline layout\n", __FUNCTION__);
    return false;
  }

  // light sphere pass pass with shadows
  std::vector<VkDescriptorSetLayout> lightSphereShadowLayout = {
    renderData.rdLightSphereShadowDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdLightSphereShadowPipelineLayout, lightSphereShadowLayout)) {
    Logger::log(1, "%s error: could not init light sphere shadow pipeline layout\n", __FUNCTION__);
    return false;
  }

  // swapchain copy
  std::vector<VkDescriptorSetLayout> swapchainCopyLayout = {
    renderData.rdSwapchainCopyDescriptorLayout };

  if (!PipelineLayout::init(renderData, renderData.rdSwapchainCopyPipelineLayout, swapchainCopyLayout)) {
    Logger::log(1, "%s error: could not init swapchain copy pipeline layout\n", __FUNCTION__);
    return false;
  }

  std::vector<VkDescriptorSetLayout> vrLayouts = {
    renderData.rdAssimpTextureDescriptorLayout,
    renderData.rdVRControllerDescriptorLayout };

  // VR Controllers, need separate buffer
  if (!PipelineLayout::init(renderData, renderData.rdVRControllerPipelineLayout, vrLayouts, pushConstants)) {
    Logger::log(1, "%s error: could not init VR Controller pipeline layout\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createPipelines(VkRenderData& renderData) {
  std::vector<VkFormat> assimpGBufferAttachmentFormats {
    renderData.rdGBuffer.color.format,
    renderData.rdGBuffer.depth.format,
    renderData.rdGBuffer.normal.format,
  };
  std::vector<VkFormat> assimpAttachmentFormats = assimpGBufferAttachmentFormats;
  assimpAttachmentFormats.push_back(renderData.rdSelectionImageData.format);
#if defined(__ANDROID__)
  (void)assimpGBufferAttachmentFormats;
#endif

  std::vector<VkFormat> ssaoAttachmentFormats {
    renderData.rdSSAOColorBufferData.format,
  };

  std::vector<VkFormat> ssaoBlurAttachmentFormats {
    renderData.rdSSAOBlurBufferData.format,
  };

  std::vector<VkFormat> compositeAttachmentFormats {
    renderData.rdVkbSwapchain.image_format,
    renderData.rdSelectionImageData.format,
  };

  std::vector<VkFormat> lightSphereColorAttachmentFormats {
    renderData.rdLightSpheresBufferData.format,
  };

  std::vector<VkFormat> swapchainCopyAttachmentFormats {
    renderData.rdVkbSwapchain.image_format,
  };

  std::vector<VkFormat> colorAndDepthAttachmentFormats {
    renderData.rdGBuffer.color.format,
    renderData.rdGBuffer.depth.format,
  };

  std::string vertexShaderFile = "shader/assimp.vert.spv";
  std::string fragmentShaderFile = "shader/assimp.frag.spv";
#if defined(__ANDROID__)
  const auto &assimpBaseFormats = assimpGBufferAttachmentFormats;
#else
  const auto &assimpBaseFormats = assimpAttachmentFormats;
#endif
  if (!ModelLevelPipeline::init(renderData, assimpBaseFormats, renderData.rdAssimpPipelineLayout,
      renderData.rdAssimpPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpBaseFormats, renderData.rdAssimpSkinningPipelineLayout,
      renderData.rdAssimpSkinningPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_selection.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpAttachmentFormats, renderData.rdAssimpSelectionPipelineLayout,
      renderData.rdAssimpSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_selection.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpAttachmentFormats, renderData.rdAssimpSkinningSelectionPipelineLayout,
      renderData.rdAssimpSkinningSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_morph.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_morph.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpBaseFormats, renderData.rdAssimpSkinningMorphPipelineLayout,
      renderData.rdAssimpSkinningMorphPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_skinning_morph_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_skinning_morph_selection.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpAttachmentFormats, renderData.rdAssimpSkinningMorphSelectionPipelineLayout,
      renderData.rdAssimpSkinningMorphSelectionPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_level.vert.spv";
  fragmentShaderFile = "shader/assimp_level.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpBaseFormats, renderData.rdAssimpLevelPipelineLayout,
      renderData.rdAssimpLevelPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Level shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_groundmesh.vert.spv";
  fragmentShaderFile = "shader/assimp_groundmesh.frag.spv";
  if (!SimpleVertexMeshPipeline::init(renderData, compositeAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdGroundMeshMeshPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Ground Mesh drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  std::string computeShaderFile = "shader/assimp_instance_transform.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout,
      renderData.rdAssimpComputeTransformPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Transform compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_matrix_mult.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout,
      renderData.rdAssimpComputeMatrixMultPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Matrix Mult compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_bounding_spheres.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout,
      renderData.rdAssimpComputeBoundingSpheresPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Bounding Spheres compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  computeShaderFile = "shader/assimp_instance_headmove_transform.comp.spv";
  if (!ComputePipeline::init(renderData, renderData.rdAssimpComputeTransformaPipelineLayout,
      renderData.rdAssimpComputeHeadMoveTransformPipeline, computeShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Head Movement Transform compute shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/line.vert.spv";
  fragmentShaderFile = "shader/line.frag.spv";
  if (!LinePipeline::init(renderData, compositeAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdLinePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/line_sphere.vert.spv";
  fragmentShaderFile = "shader/line_sphere.frag.spv";
  if (!LinePipeline::init(renderData, compositeAttachmentFormats, renderData.rdSpherePipelineLayout,
      renderData.rdSpherePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp sphere drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/grid.vert.spv";
  fragmentShaderFile = "shader/grid.frag.spv";
  if (!GridLinePipeline::init(renderData, compositeAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdGridLinePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp grid line drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/skybox.vert.spv";
  fragmentShaderFile = "shader/skybox.frag.spv";
  if (!SkyboxPipeline::init(renderData, compositeAttachmentFormats, renderData.rdSkyboxPipelineLayout,
      renderData.rdSkyboxPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp skybox shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/composite.vert.spv";
  fragmentShaderFile = "shader/composite.frag.spv";
  if (!CompositePipeline::init(renderData, compositeAttachmentFormats, renderData.rdCompositePipelineLayout,
    renderData.rdCompositePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Composite pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/ssao.vert.spv";
  fragmentShaderFile = "shader/ssao.frag.spv";
  if (!SSAOPipeline::init(renderData, ssaoAttachmentFormats, renderData.rdSSAOPipelineLayout,
    renderData.rdSSAOPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init SSAO pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/ssao_blur.vert.spv";
  fragmentShaderFile = "shader/ssao_blur.frag.spv";
  if (!SSAOPipeline::init(renderData, ssaoBlurAttachmentFormats, renderData.rdSSAOBlurPipelineLayout,
    renderData.rdSSAOBlurPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init SSAO blur pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/shadowmap_assimp.vert.spv";
  fragmentShaderFile = "shader/shadowmap_assimp.frag.spv";
  if (!ShadowMapPipeline::init(renderData, renderData.rdAssimpPipelineLayout,
    renderData.rdShadowMapAssimpPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  if (!DynamicShadowMapPipeline::init(renderData, renderData.rdAssimpPipelineLayout,
    renderData.rdDynamicShadowMapAssimpPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp dynamic shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/shadowmap_assimp_skinning.vert.spv";
  fragmentShaderFile = "shader/shadowmap_assimp_skinning.frag.spv";
  if (!ShadowMapPipeline::init(renderData, renderData.rdAssimpSkinningPipelineLayout,
    renderData.rdShadowMapAssimpSkinningPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  if (!DynamicShadowMapPipeline::init(renderData, renderData.rdAssimpSkinningPipelineLayout,
    renderData.rdDynamicShadowMapAssimpSkinningPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Skinning dynamic shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/shadowmap_assimp_skinning_morph.vert.spv";
  fragmentShaderFile = "shader/shadowmap_assimp_skinning_morph.frag.spv";
  if (!ShadowMapPipeline::init(renderData, renderData.rdAssimpSkinningMorphPipelineLayout,
    renderData.rdShadowMapAssimpSkinningMorphPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  if (!DynamicShadowMapPipeline::init(renderData, renderData.rdAssimpSkinningMorphPipelineLayout,
    renderData.rdDynamicShadowMapAssimpSkinningMorphPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Morph Anim Skinning dynamic shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/shadowmap_assimp_level.vert.spv";
  fragmentShaderFile = "shader/shadowmap_assimp_level.frag.spv";
  if (!ShadowMapPipeline::init(renderData, renderData.rdAssimpLevelPipelineLayout,
    renderData.rdShadowMapAssimpLevelPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Level shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  if (!DynamicShadowMapPipeline::init(renderData, renderData.rdAssimpLevelPipelineLayout,
    renderData.rdDynamicShadowMapAssimpLevelPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp Level dynamic shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_post_composite.vert.spv";
  fragmentShaderFile = "shader/assimp_post_composite.frag.spv";
  if (!ModelLevelPipeline::init(renderData, compositeAttachmentFormats, renderData.rdAssimpPipelineLayout,
    renderData.rdAssimpPostCompositePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp post composite shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/assimp_post_composite_selection.vert.spv";
  fragmentShaderFile = "shader/assimp_post_composite_selection.frag.spv";
  if (!ModelLevelPipeline::init(renderData, compositeAttachmentFormats, renderData.rdAssimpSelectionPipelineLayout,
    renderData.rdAssimpPostCompositeSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Assimp post composite Selection shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/light_sphere.vert.spv";
  fragmentShaderFile = "shader/light_sphere.frag.spv";
  if (!LightSpherePipeline::init(renderData, lightSphereColorAttachmentFormats, renderData.rdLightSpherePipelineLayout,
    renderData.rdLightSpherePipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init light sphere shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/light_sphere_shadow.vert.spv";
  fragmentShaderFile = "shader/light_sphere_shadow.frag.spv";
  if (!LightSpherePipeline::init(renderData, lightSphereColorAttachmentFormats, renderData.rdLightSphereShadowPipelineLayout,
    renderData.rdLightSphereShadowPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init light sphere shadow shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/swapchain_copy.vert.spv";
  fragmentShaderFile = "shader/swapchain_copy.frag.spv";
  if (!CopyPipeline::init(renderData, swapchainCopyAttachmentFormats, renderData.rdSwapchainCopyPipelineLayout,
      renderData.rdSwapchainCopyPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Swapchain Copy shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/vr_controller.vert.spv";
  fragmentShaderFile = "shader/vr_controller.frag.spv";
  if (!ModelLevelPipeline::init(renderData, assimpBaseFormats, renderData.rdVRControllerPipelineLayout,
      renderData.rdVRControllerPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init VR Controller shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/shadowmap_vr_controller.vert.spv";
  fragmentShaderFile = "shader/shadowmap_vr_controller.frag.spv";
  if (!ShadowMapPipeline::init(renderData, renderData.rdVRControllerPipelineLayout,
    renderData.rdShadowMapVRControllerPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init VR Controller shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  if (!DynamicShadowMapPipeline::init(renderData, renderData.rdVRControllerPipelineLayout,
    renderData.rdDynamicShadowMapVRControllerPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init VR Controller dynamic shadow map shader pipeline\n", __FUNCTION__);
    return false;
  }

  vertexShaderFile = "shader/vr_vis_mask.vert.spv";
  fragmentShaderFile = "shader/vr_vis_mask.frag.spv";
  if (!SimpleVertexMeshPipeline::init(renderData, colorAndDepthAttachmentFormats, renderData.rdLinePipelineLayout,
      renderData.rdXRVisMaskMeshPipeline, vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init Visibility Mask Mesh drawing shader pipeline\n", __FUNCTION__);
    return false;
  }

  return true;
}

bool VkHelper::createXRPipeline(VkRenderData& renderData, VkFormat format) {
  std::vector<VkFormat> xrSwapchainCopyAttachmentFormats {
    format
  };

  std::string vertexShaderFile = "shader/xr_swapchain_copy.vert.spv";
  std::string fragmentShaderFile = "shader/xr_swapchain_copy.frag.spv";
  if (!XRCopyPipeline::init(renderData, xrSwapchainCopyAttachmentFormats, renderData.rdSwapchainCopyPipelineLayout,
      renderData.rdXRSwapchainCopyPipeline,
      vertexShaderFile, fragmentShaderFile)) {
    Logger::log(1, "%s error: could not init XR Swapchain Copy shader pipeline\n", __FUNCTION__);
    return false;
  }

  return true;
}

void VkHelper::destroyXRPipeline(VkRenderData& renderData) {
  XRCopyPipeline::cleanup(renderData, renderData.rdXRSwapchainCopyPipeline);
}

void VkHelper::enableBlending(VkRenderData& renderData, uint32_t attachmentCount) {
  if (renderData.rdvkCmdSetColorBlendEquationEXT == nullptr || renderData.rdvkCmdSetColorBlendEnableEXT == nullptr) {
    return;
  }

  std::vector<VkColorBlendEquationEXT> blendEquations{};
  for (uint32_t i = 0; i < attachmentCount; ++i) {
    VkColorBlendEquationEXT colorBlendEquation{};
    colorBlendEquation.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendEquation.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendEquation.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendEquation.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendEquation.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendEquation.alphaBlendOp = VK_BLEND_OP_ADD;

    blendEquations.emplace_back(colorBlendEquation);
  }

  renderData.rdvkCmdSetColorBlendEquationEXT(renderData.rdCommandBuffers.at(renderData.currentFrame), 0, attachmentCount, blendEquations.data());

  std::vector<VkBool32> colorBlendEnable{};
  colorBlendEnable.resize(attachmentCount, VK_TRUE);

  renderData.rdvkCmdSetColorBlendEnableEXT(renderData.rdCommandBuffers.at(renderData.currentFrame), 0, attachmentCount, colorBlendEnable.data());

  vkCmdSetDepthCompareOp(renderData.rdCommandBuffers.at(renderData.currentFrame), VK_COMPARE_OP_LESS);
}

void VkHelper::disableBlending(VkRenderData& renderData, uint32_t attachmentCount) {
  if (renderData.rdvkCmdSetColorBlendEnableEXT == nullptr) {
    return;
  }

  std::vector<VkBool32> colorBlendEnable{};
  colorBlendEnable.resize(attachmentCount, VK_FALSE);

  renderData.rdvkCmdSetColorBlendEnableEXT(renderData.rdCommandBuffers.at(renderData.currentFrame), 0, attachmentCount, colorBlendEnable.data());

  vkCmdSetDepthCompareOp(renderData.rdCommandBuffers.at(renderData.currentFrame), VK_COMPARE_OP_ALWAYS);
}

bool VkHelper::createSyncObjects(VkRenderData& renderData) {
  if (!SyncObjects::init(renderData)) {
    Logger::log(1, "%s error: could not create sync objects\n", __FUNCTION__);
    return false;
  }
  return true;
}


bool VkHelper::createVertexBuffers(VkRenderData& renderData) {
  if (!VertexBuffer::init(renderData, renderData.rdLineVertexBuffers)) {
    Logger::log(1, "%s error: could not create line vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdSphereVertexBuffers)) {
    Logger::log(1, "%s error: could not create sphere vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelAABBVertexBuffers)) {
    Logger::log(1, "%s error: could not create level AABB vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelOctreeVertexBuffers)) {
    Logger::log(1, "%s error: could not create level octree vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLevelWireframeVertexBuffers)) {
    Logger::log(1, "%s error: could not create level wireframe vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdIKLinesVertexBuffers)) {
    Logger::log(1, "%s error: could not create IK Lines vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdGroundMeshVertexBuffers)) {
    Logger::log(1, "%s error: could not create ground mesh vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdGroundMeshNeighborVertexBuffers)) {
    Logger::log(1, "%s error: could not create ground mesh neighbor triangles vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdInstancePathVertexBuffers)) {
    Logger::log(1, "%s error: could not create instance path vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdSkyboxBuffers)) {
    Logger::log(1, "%s error: could not create skybox vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdDynamicLightDebugVertexBuffers)) {
    Logger::log(1, "%s error: could not create light debug vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLightSphereVertexBuffers)) {
    Logger::log(1, "%s error: could not create light sphere vertex buffer\n", __FUNCTION__);
    return false;
  }

  if (!VertexBuffer::init(renderData, renderData.rdLightSphereDebugVertexBuffers)) {
    Logger::log(1, "%s error: could not create light sphere debug vertex buffer\n", __FUNCTION__);
    return false;
  }

  return true;
}


bool VkHelper::createDepthBuffer(VkRenderData& renderData) {
  VkExtent3D depthImageExtent = {
    renderData.rdXRWidth,
    renderData.rdXRHeight,
    1
  };

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = renderData.rdDepthBufferData.format;
  imageInfo.extent = depthImageExtent;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 2;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  VmaAllocationCreateInfo depthAllocInfo{};
  depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  depthAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &depthAllocInfo, &renderData.rdDepthBufferData.image, &renderData.rdDepthBufferData.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate depth buffer memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  VkImageViewCreateInfo imageViewinfo{};
  imageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  imageViewinfo.image = renderData.rdDepthBufferData.image;
  imageViewinfo.format = renderData.rdDepthBufferData.format;
  imageViewinfo.subresourceRange.baseMipLevel = 0;
  imageViewinfo.subresourceRange.levelCount = 1;
  imageViewinfo.subresourceRange.baseArrayLayer = 0;
  imageViewinfo.subresourceRange.layerCount = 2;
  imageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewinfo, nullptr, &renderData.rdDepthBufferData.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create depth buffer image view (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  VkCommandBuffer imageTransitionBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  VkImageSubresourceRange imageSSR;
  imageSSR.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  imageSSR.baseMipLevel = 0;
  imageSSR.levelCount = 1;
  imageSSR.baseArrayLayer = 0;
  imageSSR.layerCount = 2;

  VkImageMemoryBarrier imageMemoryBarrier{};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  imageMemoryBarrier.image = renderData.rdDepthBufferData.image;
  imageMemoryBarrier.subresourceRange = imageSSR;

  vkCmdPipelineBarrier(
    imageTransitionBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
    0,
    0, nullptr, 0, nullptr,
    1, &imageMemoryBarrier // pImageMemoryBarriers
  );

  if (!CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool,
      imageTransitionBuffer, renderData.rdGraphicsQueue)) {
    Logger::log(1, "%s error: could not transition depth image\n", __FUNCTION__);
    return false;
  }

  return true;
}

void VkHelper::cleanupDepthBuffer(VkRenderData& renderData) {
  vkDestroyImageView(renderData.rdVkbDevice.device, renderData.rdDepthBufferData.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, renderData.rdDepthBufferData.image, renderData.rdDepthBufferData.alloc);
}

bool VkHelper::createDepthBufferCubeMap(VkRenderData& renderData, VkImageData& imageData, uint32_t numDynamicLightsWithShadow) {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = renderData.rdDynLightShadowMapSize.width;
  imageInfo.extent.height = renderData.rdDynLightShadowMapSize.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.format = VK_FORMAT_D16_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  // every face is a layer
  imageInfo.arrayLayers = 6 * numDynamicLightsWithShadow;
  imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

  VmaAllocationCreateInfo imageAllocInfo{};
  imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &imageAllocInfo, &imageData.image,  &imageData.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate texture image via VMA (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  VkImageSubresourceRange subresourceRange{};
  subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  subresourceRange.baseMipLevel = 0;
  subresourceRange.levelCount = 1;
  subresourceRange.baseArrayLayer = 0;
  subresourceRange.layerCount = 6 * numDynamicLightsWithShadow;

  // 1st barrier, we need to transfer to attachment optimal
  VkImageMemoryBarrier formatChangeBarrier{};
  formatChangeBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  formatChangeBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  formatChangeBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  formatChangeBarrier.image = imageData.image;
  formatChangeBarrier.subresourceRange = subresourceRange;
  formatChangeBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkCommandBuffer uploadCommandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0,
    0, nullptr,
    0, nullptr,
    1, &formatChangeBarrier);

  bool commandResult = CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool, uploadCommandBuffer, renderData.rdGraphicsQueue);

  if (!commandResult) {
    Logger::log(1, "%s error: could not submit texture transfer commands\n", __FUNCTION__);
    return false;
  }

  // image view and sampler
  VkImageViewCreateInfo cubeViewInfo{};
  cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  cubeViewInfo.image = imageData.image;
  cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  cubeViewInfo.format = VK_FORMAT_D16_UNORM;
  cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  cubeViewInfo.subresourceRange.baseMipLevel = 0;
  cubeViewInfo.subresourceRange.levelCount = 1;
  cubeViewInfo.subresourceRange.baseArrayLayer = 0;
  cubeViewInfo.subresourceRange.layerCount = 6 * numDynamicLightsWithShadow ;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &cubeViewInfo, nullptr, &imageData.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image view for texture\n", __FUNCTION__);
    return false;
  }

  imageData.imageViews.resize(numDynamicLightsWithShadow);
  for (int i = 0; i < numDynamicLightsWithShadow; ++i) {
    VkImageViewCreateInfo cubeViewInfo{};
    cubeViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cubeViewInfo.image = imageData.image;
    cubeViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    cubeViewInfo.format = VK_FORMAT_D16_UNORM;
    cubeViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    cubeViewInfo.subresourceRange.baseMipLevel = 0;
    cubeViewInfo.subresourceRange.levelCount = 1;
    cubeViewInfo.subresourceRange.baseArrayLayer = 6 * i;
    cubeViewInfo.subresourceRange.layerCount = 6;

    result = vkCreateImageView(renderData.rdVkbDevice.device, &cubeViewInfo, nullptr, &imageData.imageViews.at(i));
    if (result != VK_SUCCESS) {
      Logger::log(1, "%s error: could not create image view for texture\n", __FUNCTION__);
      return false;
    }
  }

  VkSamplerCreateInfo cubeSamplerInfo{};
  cubeSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  cubeSamplerInfo.magFilter = VK_FILTER_LINEAR;
  cubeSamplerInfo.minFilter = VK_FILTER_LINEAR;
  cubeSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  cubeSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  cubeSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  cubeSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  cubeSamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
  cubeSamplerInfo.compareEnable = VK_FALSE;
  cubeSamplerInfo.compareOp = VK_COMPARE_OP_NEVER;
  cubeSamplerInfo.mipLodBias = 0.0f;
  cubeSamplerInfo.minLod = 0.0f;
  cubeSamplerInfo.maxLod = 1.0f;
  cubeSamplerInfo.anisotropyEnable = VK_FALSE;

  result = vkCreateSampler(renderData.rdVkbDevice.device, &cubeSamplerInfo, nullptr, &imageData.sampler);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create sampler for texture (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  imageData.numLayers = 6 * numDynamicLightsWithShadow;

  return true;
}

void VkHelper::cleanupDepthBufferCubeMap(VkRenderData& renderData, VkImageData& imageData) {
  vkDestroySampler(renderData.rdVkbDevice.device, imageData.sampler, nullptr);
  vkDestroyImageView(renderData.rdVkbDevice.device, imageData.imageView, nullptr);
  for (size_t i = 0; i < imageData.imageViews.size(); ++i) {
    vkDestroyImageView(renderData.rdVkbDevice.device, imageData.imageViews.at(i), nullptr);
  }
  vmaDestroyImage(renderData.rdAllocator, imageData.image, imageData.alloc);
}

bool VkHelper::createImages(VkRenderData& renderData) {
  VkExtent2D xrExtent = { renderData.rdXRWidth, renderData.rdXRHeight };

  if (!Image::create(renderData, renderData.rdFinalImageData, VK_FORMAT_B8G8R8A8_UNORM,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      xrExtent, 2)) {
    return false;
  }

  if (!Image::create(renderData, renderData.rdSelectionImageData, VK_FORMAT_R32_SFLOAT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      xrExtent, 2)) {
    return false;
  }

  // we are missing half float support, so use 32 bit here
  if (!Image::create(renderData, renderData.rdSSAOColorBufferData, VK_FORMAT_R32_SFLOAT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      xrExtent, 2)) {
    return false;
  }

  if (!Image::create(renderData, renderData.rdSSAOBlurBufferData, VK_FORMAT_R32_SFLOAT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      xrExtent, 2)) {
    return false;
  }

  if (!Image::create(renderData, renderData.rdLightSpheresBufferData, VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      xrExtent, 2)) {
    return false;
  }

  return true;
}

void VkHelper::cleanupImages(VkRenderData& renderData) {
  if (renderData.rdFinalImageData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdFinalImageData);
  }

  if (renderData.rdSelectionImageData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdSelectionImageData);
  }

  if (renderData.rdSelectionImageData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdSSAOColorBufferData);
  }

  if (renderData.rdSelectionImageData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdSSAOBlurBufferData);
  }

  if (renderData.rdSelectionImageData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdLightSpheresBufferData);
  }
}

float VkHelper::getPixelValueFromPos(VkRenderData& renderData, VkImage srcImage, int xPos, int yPos) {
  // random default value to detect errors
  float pixelColor = -444.0f;

  if (xPos < 0 || yPos < 0 || xPos >= renderData.rdWidth || yPos >= renderData.rdHeight) {
    return pixelColor;
  }

  VkImage readbackImage;
  VmaAllocation readbackImageAlloc;

  // create local image
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = renderData.rdHalfWidth;
  imageInfo.extent.height = renderData.rdHeight;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R32_SFLOAT;
  imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo imageAllocInfo{};
  imageAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
  imageAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &imageAllocInfo, &readbackImage,  &readbackImageAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate read back image image via VMA (error: %i)\n", __FUNCTION__, result);
    return pixelColor;
  }

  VkCommandBuffer readbackCommandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  VkImageSubresourceRange layoutTransferRange{};
  layoutTransferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  layoutTransferRange.baseMipLevel = 0;
  layoutTransferRange.levelCount = 1;
  layoutTransferRange.baseArrayLayer = 0;
  layoutTransferRange.layerCount = 1;

  // transition destination (local) image to transfer destination layout
  VkImageMemoryBarrier layoutTransferBarrier{};
  layoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  layoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  layoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  layoutTransferBarrier.image = readbackImage;
  layoutTransferBarrier.subresourceRange = layoutTransferRange;
  layoutTransferBarrier.srcAccessMask = 0;
  layoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  // transition source (selection) image to transfer source optimal layout
  VkImageMemoryBarrier srcLayoutTransferBarrier{};
  srcLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  srcLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  srcLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  srcLayoutTransferBarrier.image = srcImage;
  srcLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  srcLayoutTransferBarrier.srcAccessMask = 0;
  srcLayoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  VkOffset3D blitSizeSrc{};
  blitSizeSrc.x = renderData.rdXRWidth;
  blitSizeSrc.y = renderData.rdXRHeight;
  blitSizeSrc.z = 1;

  VkOffset3D blitSizeDest{};
  blitSizeDest.x = renderData.rdHalfWidth;
  blitSizeDest.y = renderData.rdHeight;
  blitSizeDest.z = 1;

  // copy selection image to local image
  VkImageBlit imageCopyRegion{};
  imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageCopyRegion.srcSubresource.layerCount = 1;
  imageCopyRegion.srcOffsets[1] = blitSizeSrc;
  imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  imageCopyRegion.dstSubresource.layerCount = 1;
  imageCopyRegion.dstOffsets[1] = blitSizeDest;

  // transition destination (local) image to general layout to allow mapping
  VkImageMemoryBarrier destLayoutTransferBarrier{};
  destLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  destLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  destLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  destLayoutTransferBarrier.image = readbackImage;
  destLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  destLayoutTransferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  destLayoutTransferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

  /* selection image back to old layout*/
  VkImageMemoryBarrier srcBackLayoutTransferBarrier{};
  srcBackLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  srcBackLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  srcBackLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  srcBackLayoutTransferBarrier.image = srcImage;
  srcBackLayoutTransferBarrier.subresourceRange = layoutTransferRange;
  srcBackLayoutTransferBarrier.srcAccessMask = 0;
  srcBackLayoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &layoutTransferBarrier);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &srcLayoutTransferBarrier);
  vkCmdBlitImage(readbackCommandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    readbackImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion, VK_FILTER_NEAREST);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
    0, 0, nullptr, 0, nullptr, 1, &destLayoutTransferBarrier);
  vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 1, &srcBackLayoutTransferBarrier);

  bool commandResult = CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool,
    readbackCommandBuffer, renderData.rdGraphicsQueue);

  if (!commandResult) {
    Logger::log(1, "%s error: could not submit readback transfer commands\n", __FUNCTION__);
    return pixelColor;
  }

  // get image layout
  VkImageSubresource subResource{};
  subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubresourceLayout subResourceLayout{};

  vkGetImageSubresourceLayout(renderData.rdVkbDevice.device, readbackImage, &subResource, &subResourceLayout);

  // map and read data
  const float* data;
  result = vmaMapMemory(renderData.rdAllocator, readbackImageAlloc, (void**)&data);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not map readback image memory (error: %i)\n", __FUNCTION__, result);
    return pixelColor;
  }

  data += yPos * subResourceLayout.rowPitch / sizeof(float) + xPos;
  pixelColor = *data;

  vmaUnmapMemory(renderData.rdAllocator, readbackImageAlloc);

  // destroy local image, no longer needed
  vmaDestroyImage(renderData.rdAllocator, readbackImage, readbackImageAlloc);

  return pixelColor;
}

bool VkHelper::createGBuffer(VkRenderData& renderData) {
  VkExtent2D xrExtent = { renderData.rdXRWidth, renderData.rdXRHeight };

  // init framebuffer attachments
  Logger::log(1, "%s: create GBuffer color attachment (RGBA, 4x 8 bit int)\n", __FUNCTION__);
  if (!Image::create(renderData, renderData.rdGBuffer.color,
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    xrExtent, 2)) {
    return false;
  }
  Logger::log(1, "%s: create GBuffer depth attachment (1x 32 bit float)\n", __FUNCTION__);
  const VkFormat gbufferDepthFormat = VK_FORMAT_R32_SFLOAT;
  if (!Image::create(renderData, renderData.rdGBuffer.depth,
    gbufferDepthFormat,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    xrExtent, 2)) {
    return false;
  }
  Logger::log(1, "%s: create GBuffer normal attachment (4x 8 bit int)\n", __FUNCTION__);
  if (!Image::create(renderData, renderData.rdGBuffer.normal,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    xrExtent, 2)) {
    return false;
  }

  return true;
}

void VkHelper::cleanupGBuffer(VkRenderData& renderData) {
  if (renderData.rdGBuffer.color.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdGBuffer.color);
    Logger::log(1, "%s: deleted GBuffer color attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer color attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdGBuffer.depth.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdGBuffer.depth);
    Logger::log(1, "%s: deleted GBuffer depth attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer depth attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdGBuffer.normal.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdGBuffer.normal);
    Logger::log(1, "%s: deleted GBuffer normal attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: GBuffer normal attachment is null\n",  __FUNCTION__);
  }
}

bool VkHelper::createShadowMapBuffer(VkRenderData& renderData) {
  Logger::log(1, "%s: create shadow map depth attachment (16 bit depth)\n", __FUNCTION__);
  if (!Image::create(renderData, renderData.rdShadowMapDepthBufferData,
    VK_FORMAT_D16_UNORM,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    renderData.rdShadowMapSize, renderData.SHADOW_MAP_LAYERS)) {
    return false;
  }

  Logger::log(1, "%s: create combined shadow map depth attachment (16 bit depth)\n", __FUNCTION__);
  if (!Image::create(renderData, renderData.rdShadowMapCombinedDepthBufferData,
    VK_FORMAT_D16_UNORM,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    renderData.rdShadowMapSize)) {
    return false;
  }

  Logger::log(1, "%s: create combined shadow map depth attachment for dynamic lights (16 bit depth)\n", __FUNCTION__);
  if (!Image::create(renderData, renderData.rdDynamicLightCombinedShadowData,
    VK_FORMAT_D16_UNORM,
    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    renderData.rdDynLightShadowMapSize)) {
    return false;
  }
  return true;
}

void VkHelper::cleanupShadowMapBuffer(VkRenderData& renderData) {
  if (renderData.rdShadowMapDepthBufferData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdShadowMapDepthBufferData);
    Logger::log(1, "%s: deleted shadow map depth attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: shadow map depth attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdShadowMapCombinedDepthBufferData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdShadowMapCombinedDepthBufferData);
    Logger::log(1, "%s: deleted combined shadow map depth attachment\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: combined shadow map depth attachment is null\n",  __FUNCTION__);
  }
  if (renderData.rdDynamicLightCombinedShadowData.image != VK_NULL_HANDLE) {
    Image::cleanup(renderData, renderData.rdDynamicLightCombinedShadowData);
    Logger::log(1, "%s: deleted combined shadow map depth attachment for dynamic lights\n", __FUNCTION__);
  } else {
    Logger::log(1,"%s error: combined shadow map depth attachment for dynamic lights is null\n",  __FUNCTION__);
  }
}

void VkHelper::transitionImageLayout(VkRenderData& renderData, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount)
{
  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL || newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  VkImageSubresourceRange imageSSR;
  imageSSR.aspectMask = aspectMask;
  imageSSR.baseMipLevel = 0;
  imageSSR.levelCount = 1;
  imageSSR.baseArrayLayer = 0;
  imageSSR.layerCount = layerCount;

  VkImageMemoryBarrier imageMemoryBarrier{};
  imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  imageMemoryBarrier.oldLayout = oldLayout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.image = image;
  imageMemoryBarrier.subresourceRange = imageSSR;

  vkCmdPipelineBarrier(
    renderData.rdCommandBuffers.at(renderData.currentFrame),
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
    0,
    0, nullptr, 0, nullptr,
    1, &imageMemoryBarrier // pImageMemoryBarriers
  );
}

void VkHelper::initSSAO(VkRenderData& renderData) {
  // samples for kernel 
  std::vector<glm::vec4> ssaoKernel;
  std::random_device randomDevice{};
  unsigned int seed = randomDevice();
  std::default_random_engine randomEngine(seed);

  std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f); // random floats between [0.0, 1.0]

  auto lerp = [](float a, float b, float f) {
    return a + f * (b - 1);
  };

  for (unsigned int i = 0; i < 64; ++i)  {
    glm::vec4 sample(
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine), 0.0f
    );

    sample  = glm::normalize(sample);
    sample *= randomFloats(randomEngine);

    float scale = static_cast<float>(i) / 64.0;
    scale   = lerp(0.1f, 1.0f, scale * scale);
    sample *= scale;

    ssaoKernel.emplace_back(sample);
  }

  UniformBuffer::uploadData(renderData, renderData.rdSSAOKernelSamplesUBO, ssaoKernel);

  // noise for texture
  std::vector<glm::vec4> ssaoNoise;
  for (unsigned int i = 0; i < 16; i++)  {
    glm::vec4 noise(
      randomFloats(randomEngine) * 2.0f - 1.0f,
      randomFloats(randomEngine) * 2.0f - 1.0f,
      0.0f, 1.0f);

    ssaoNoise.push_back(noise);
  }

  createSSAONoiseTexture(renderData, ssaoNoise);
}

bool VkHelper::createSSAONoiseTexture(VkRenderData& renderData, std::vector<glm::vec4> noiseData) {
  // noise texture is 4x4 pixels
  VkExtent3D imageExtent = {
        4,
        4,
        1
  };

  int32_t imageSize = imageExtent.width * imageExtent.height * imageExtent.depth * sizeof(glm::vec4);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = renderData.rdSSAONoiseBufferData.format;
  imageInfo.extent = imageExtent;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo ssaoAllocInfo{};
  ssaoAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  ssaoAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VkResult result = vmaCreateImage(renderData.rdAllocator, &imageInfo, &ssaoAllocInfo,
    &renderData.rdSSAONoiseBufferData.image, &renderData.rdSSAONoiseBufferData.alloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate image memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // staging buffer
  VkBufferCreateInfo stagingBufferInfo{};
  stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingBufferInfo.size = imageSize;
  stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  VkBuffer stagingBuffer;
  VmaAllocation stagingBufferAlloc;

  VmaAllocationCreateInfo stagingAllocInfo{};
  stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  result = vmaCreateBuffer(renderData.rdAllocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer,  &stagingBufferAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate texture staging buffer via VMA (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  void* uploadData;
  result = vmaMapMemory(renderData.rdAllocator, stagingBufferAlloc, &uploadData);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not map texture memory (error: %i)\n", __FUNCTION__, result);
    return false;
  }
  std::memcpy(uploadData, noiseData.data(), imageSize);
  vmaUnmapMemory(renderData.rdAllocator, stagingBufferAlloc);
  vmaFlushAllocation(renderData.rdAllocator, stagingBufferAlloc, 0, imageSize);

  VkImageSubresourceRange stagingBufferRange{};
  stagingBufferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  stagingBufferRange.baseMipLevel = 0;
  stagingBufferRange.levelCount = 1;
  stagingBufferRange.baseArrayLayer = 0;
  stagingBufferRange.layerCount = 1;

  // 1st barrier, undefined to transfer optimal
  VkImageMemoryBarrier stagingBufferTransferBarrier{};
  stagingBufferTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  stagingBufferTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  stagingBufferTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  stagingBufferTransferBarrier.image = renderData.rdSSAONoiseBufferData.image;
  stagingBufferTransferBarrier.subresourceRange = stagingBufferRange;
  stagingBufferTransferBarrier.srcAccessMask = 0;
  stagingBufferTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  VkBufferImageCopy stagingBufferCopy{};
  stagingBufferCopy.bufferOffset = 0;
  stagingBufferCopy.bufferRowLength = 0;
  stagingBufferCopy.bufferImageHeight = 0;
  stagingBufferCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  stagingBufferCopy.imageSubresource.mipLevel = 0;
  stagingBufferCopy.imageSubresource.baseArrayLayer = 0;
  stagingBufferCopy.imageSubresource.layerCount = 1;
  stagingBufferCopy.imageExtent = imageExtent;

  // 2nd barrier, transfer optimal to shader optimal
  VkImageMemoryBarrier stagingBufferShaderBarrier{};
  stagingBufferShaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  stagingBufferShaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  stagingBufferShaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  stagingBufferShaderBarrier.image = renderData.rdSSAONoiseBufferData.image;
  stagingBufferShaderBarrier.subresourceRange = stagingBufferRange;
  stagingBufferShaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  stagingBufferShaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkCommandBuffer uploadCommandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferTransferBarrier);
  vkCmdCopyBufferToImage(uploadCommandBuffer, stagingBuffer, renderData.rdSSAONoiseBufferData.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &stagingBufferCopy);
  vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferShaderBarrier);

  bool commandResult = CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool, uploadCommandBuffer, renderData.rdGraphicsQueue);

  vmaDestroyBuffer(renderData.rdAllocator, stagingBuffer, stagingBufferAlloc);

  if (!commandResult) {
    Logger::log(1, "%s error: could not submit texture transfer commands\n", __FUNCTION__);
    return false;
  }

  VkImageViewCreateInfo imageViewinfo{};
  imageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewinfo.image = renderData.rdSSAONoiseBufferData.image;
  imageViewinfo.format = renderData.rdSSAONoiseBufferData.format;
  imageViewinfo.subresourceRange.baseMipLevel = 0;
  imageViewinfo.subresourceRange.levelCount = 1;
  imageViewinfo.subresourceRange.baseArrayLayer = 0;
  imageViewinfo.subresourceRange.layerCount = 1;
  imageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

  result = vkCreateImageView(renderData.rdVkbDevice.device, &imageViewinfo, nullptr, &renderData.rdSSAONoiseBufferData.imageView);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image view (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // Sampler for ImGui and Debug
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = 0.0f;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;

  if (vkCreateSampler(renderData.rdVkbDevice.device, &samplerInfo, nullptr, &renderData.rdSSAONoiseBufferData.sampler) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create sampler\n", __FUNCTION__);
    return false;
  }

  return true;
}

void VkHelper::cleanupSSAONoiseTexture(VkRenderData& renderData) {
  vkDestroySampler(renderData.rdVkbDevice.device, renderData.rdSSAONoiseBufferData.sampler, nullptr);
  vkDestroyImageView(renderData.rdVkbDevice.device, renderData.rdSSAONoiseBufferData.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, renderData.rdSSAONoiseBufferData.image, renderData.rdSSAONoiseBufferData.alloc);
}

void VkHelper::runComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances, uint32_t modelOffset, uint32_t instanceOffset, bool useEmptyBoneOffsets) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  // node transformation
  if (model->hasHeadMovementAnimationsMapped()) {
    vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeHeadMoveTransformPipeline);
  } else {
    vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeTransformPipeline);
  }

  VkDescriptorSet &modelTransformDescriptorSet = model->getTransformDescriptorSet();
  std::vector<VkDescriptorSet> transformComputeSets = { renderData.rdAssimpComputeTransformDescriptorSets.at(renderData.currentFrame), modelTransformDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformaPipelineLayout, 0, static_cast<uint32_t>(transformComputeSets.size()), transformComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  renderData.rdComputeModelData.pkInstanceOffset = instanceOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeTransformaPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for TRS buffer to be written  */
  VkMemoryBarrier trsBufferBarrier{};
  trsBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &trsBufferBarrier, 0, nullptr, 0, nullptr);

  // matrix multiplication
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  if (useEmptyBoneOffsets) {
    VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultEmptyOffsetDescriptorSet();
    std::vector<VkDescriptorSet> matrixMultComputeSets =
      { renderData.rdAssimpComputeMatrixMultDescriptorSets.at(renderData.currentFrame), modelMatrixMultDescriptorSet };
    vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);
  } else {
    VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultDescriptorSet();
    std::vector<VkDescriptorSet> matrixMultComputeSets =
      { renderData.rdAssimpComputeMatrixMultDescriptorSets.at(renderData.currentFrame), modelMatrixMultDescriptorSet };
    vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
      renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);
  }

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);
}

void VkHelper::runBoundingSphereComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances,
    uint32_t modelOffset) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  // node transformation
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformPipeline);

  VkDescriptorSet &modelTransformDescriptorSet = model->getTransformDescriptorSet();
  std::vector<VkDescriptorSet> transformComputeSets = { renderData.rdAssimpComputeSphereTransformDescriptorSets.at(renderData.currentFrame), modelTransformDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeTransformaPipelineLayout, 0, static_cast<uint32_t>(transformComputeSets.size()), transformComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = 0;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeTransformaPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for TRS buffer to be written  */
  VkMemoryBarrier trsBufferBarrier{};
  trsBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &trsBufferBarrier, 0, nullptr, 0, nullptr);

  // matrix multiplication
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultEmptyOffsetDescriptorSet();
  std::vector<VkDescriptorSet> matrixMultComputeSets =
    { renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.at(renderData.currentFrame), modelMatrixMultDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = 0;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);

  vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
                    renderData.rdAssimpComputeBoundingSpheresPipeline);

  VkDescriptorSet &boundingSpheresDescriptorSet = model->getBoundingSphereDescriptorSet();
  std::vector<VkDescriptorSet> boundingSphereComputeSets = { renderData.rdAssimpComputeBoundingSpheresDescriptorSets.at(renderData.currentFrame), boundingSpheresDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeBoundingSpheresPipelineLayout, 0, static_cast<uint32_t>(boundingSphereComputeSets.size()), boundingSphereComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  renderData.rdComputeModelData.pkInstanceOffset = 0;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeBoundingSpheresPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier between the compute shaders
   * wait for bounding sphere buffer to be written  */
  VkMemoryBarrier boundingSphereBufferBarrier{};
  boundingSphereBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boundingSphereBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boundingSphereBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
    &boundingSphereBufferBarrier, 0, nullptr, 0, nullptr);
}

bool VkHelper::runIKComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances, uint32_t modelOffset) {
  uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

  // upload changed TRS data of this model only
  renderData.rdUploadToUBOTimer.start();
  ShaderStorageBuffer::uploadSsboData(renderData, renderData.rdIKTRSMatrixBuffers.at(renderData.currentFrame), renderData.rdTRSData, modelOffset);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  VkResult result = vkResetFences(renderData.rdVkbDevice.device, 1, &renderData.rdComputeFences.at(renderData.currentFrame));
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }
  if (!CommandBuffer::reset(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), 0)) {
    Logger::log(1, "%s error: failed to reset compute command buffer\n", __FUNCTION__);
    return false;
  }

  if (!CommandBuffer::beginSingleShot(renderData.rdComputeCommandBuffers.at(renderData.currentFrame))) {
    Logger::log(1, "%s error: failed to begin compute command buffer\n", __FUNCTION__);
    return false;
  }

  // recalculate all TRS matrices
  vkCmdBindPipeline(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipeline);

  VkDescriptorSet &modelMatrixMultDescriptorSet = model->getMatrixMultDescriptorSet();
  std::vector<VkDescriptorSet> matrixMultComputeSets =
    { renderData.rdAssimpComputeIKDescriptorSets.at(renderData.currentFrame), modelMatrixMultDescriptorSet };
  vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_BIND_POINT_COMPUTE,
    renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(matrixMultComputeSets.size()), matrixMultComputeSets.data(), 0, 0);

  renderData.rdUploadToUBOTimer.start();
  renderData.rdComputeModelData.pkModelOffset = modelOffset;
  vkCmdPushConstants(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), renderData.rdAssimpComputeMatrixMultPipelineLayout,
    VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &renderData.rdComputeModelData);
  renderData.rdUploadToUBOTime += renderData.rdUploadToUBOTimer.stop();

  vkCmdDispatch(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

  /* memroy barrier after compute shader
   * wait for bone matrix buffer to be written  */
  VkMemoryBarrier boneMatrixBufferBarrier{};
  boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

  vkCmdPipelineBarrier(renderData.rdComputeCommandBuffers.at(renderData.currentFrame), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
     &boneMatrixBufferBarrier, 0, nullptr, 0, nullptr);

  if (!CommandBuffer::end(renderData.rdComputeCommandBuffers.at(renderData.currentFrame))) {
    Logger::log(1, "%s error: failed to end compute command buffer\n", __FUNCTION__);
    return false;
  }

  // submit compute commands
  VkSubmitInfo computeSubmitInfo{};
  computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  computeSubmitInfo.commandBufferCount = 1;
  computeSubmitInfo.pCommandBuffers = &renderData.rdComputeCommandBuffers.at(renderData.currentFrame);

  result = vkQueueSubmit(renderData.rdComputeQueue, 1, &computeSubmitInfo, renderData.rdComputeFences.at(renderData.currentFrame));
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
    return false;
  };

  // we must wait for the compute shaders to finish before we can read the data
  result = vkWaitForFences(renderData.rdVkbDevice.device, 1, &renderData.rdComputeFences.at(renderData.currentFrame), VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: waiting for compute fence failed (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // read (new) bone positions of this model only
  renderData.rdDownloadFromUBOTimer.start();
  renderData.rdIKModelMatrices = ShaderStorageBuffer::getSsboDataMat4(renderData, renderData.rdIKBoneMatrixBuffers.at(renderData.currentFrame),
    modelOffset, numInstances * numberOfBones);
  std::memcpy(renderData.rdIKMatrices.data() + modelOffset, renderData.rdIKModelMatrices.data(), numInstances * numberOfBones * sizeof(glm::mat4));
  renderData.rdDownloadFromUBOTime += renderData.rdDownloadFromUBOTimer.stop();

  return true;
}

void VkHelper::cleanup(VkRenderData& renderData) {
  SyncObjects::cleanup(renderData);

  for (int i = 0; i < renderData.rdNumFramesInFlight; ++i) {
    CommandBuffer::cleanup(renderData, renderData.rdCommandPool, renderData.rdCommandBuffers[i]);
    CommandBuffer::cleanup(renderData, renderData.rdComputeCommandPool, renderData.rdComputeCommandBuffers[i]);
  }
  CommandPool::cleanup(renderData, renderData.rdCommandPool);
  CommandPool::cleanup(renderData, renderData.rdComputeCommandPool);

  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpSkinningPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpSelectionPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpSkinningSelectionPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpSkinningMorphPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpSkinningMorphSelectionPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpLevelPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpPostCompositePipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdAssimpPostCompositeSelectionPipeline);
  ModelLevelPipeline::cleanup(renderData, renderData.rdVRControllerPipeline);
  LinePipeline::cleanup(renderData, renderData.rdLinePipeline);
  LinePipeline::cleanup(renderData, renderData.rdSpherePipeline);
  GridLinePipeline::cleanup(renderData, renderData.rdGridLinePipeline);
  SimpleVertexMeshPipeline::cleanup(renderData, renderData.rdGroundMeshMeshPipeline);
  SimpleVertexMeshPipeline::cleanup(renderData, renderData.rdXRVisMaskMeshPipeline);
  SkyboxPipeline::cleanup(renderData, renderData.rdSkyboxPipeline);
  SSAOPipeline::cleanup(renderData, renderData.rdSSAOPipeline);
  SSAOPipeline::cleanup(renderData, renderData.rdSSAOBlurPipeline);
  ShadowMapPipeline::cleanup(renderData, renderData.rdShadowMapAssimpPipeline);
  ShadowMapPipeline::cleanup(renderData, renderData.rdShadowMapAssimpSkinningPipeline);
  ShadowMapPipeline::cleanup(renderData, renderData.rdShadowMapAssimpSkinningMorphPipeline);
  ShadowMapPipeline::cleanup(renderData, renderData.rdShadowMapAssimpLevelPipeline);
  ShadowMapPipeline::cleanup(renderData, renderData.rdShadowMapVRControllerPipeline);
  DynamicShadowMapPipeline::cleanup(renderData, renderData.rdDynamicShadowMapAssimpPipeline);
  DynamicShadowMapPipeline::cleanup(renderData, renderData.rdDynamicShadowMapAssimpSkinningPipeline);
  DynamicShadowMapPipeline::cleanup(renderData, renderData.rdDynamicShadowMapAssimpSkinningMorphPipeline);
  DynamicShadowMapPipeline::cleanup(renderData, renderData.rdDynamicShadowMapAssimpLevelPipeline);
  DynamicShadowMapPipeline::cleanup(renderData, renderData.rdDynamicShadowMapVRControllerPipeline);
  LightSpherePipeline::cleanup(renderData, renderData.rdLightSpherePipeline);
  LightSpherePipeline::cleanup(renderData, renderData.rdLightSphereShadowPipeline);
  CompositePipeline::cleanup(renderData, renderData.rdCompositePipeline);
  CopyPipeline::cleanup(renderData, renderData.rdSwapchainCopyPipeline);

  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeTransformPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeHeadMoveTransformPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeMatrixMultPipeline);
  ComputePipeline::cleanup(renderData, renderData.rdAssimpComputeBoundingSpheresPipeline);

  PipelineLayout::cleanup(renderData, renderData.rdAssimpPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeTransformaPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeMatrixMultPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpComputeBoundingSpheresPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningMorphPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpSkinningMorphSelectionPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdAssimpLevelPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdLinePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSpherePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdGroundMeshPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSkyboxPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdCompositePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSSAOPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSSAOBlurPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdLightSpherePipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdLightSphereShadowPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdSwapchainCopyPipelineLayout);
  PipelineLayout::cleanup(renderData, renderData.rdVRControllerPipelineLayout);

  UniformBuffer::cleanup(renderData, renderData.rdRenderUploadDataUBOs);
  UniformBuffer::cleanup(renderData, renderData.rdSSAOKernelSamplesUBO);

  VertexBuffer::cleanup(renderData, renderData.rdLineVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdSphereVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdLevelAABBVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdLevelOctreeVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdLevelWireframeVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdIKLinesVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdGroundMeshVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdGroundMeshNeighborVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdInstancePathVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdSkyboxBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdDynamicLightDebugVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdLightSphereVertexBuffers);
  VertexBuffer::cleanup(renderData, renderData.rdLightSphereDebugVertexBuffers);

  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderTRSMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdPerInstanceAnimDataBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderModelRootMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderBoneMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSelectedInstanceBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdBoundingSphereBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereModelRootMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSpherePerInstanceAnimDataBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereTRSMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdSphereBoneMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdFaceAnimPerInstanceDataBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShaderLevelRootMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdIKBoneMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdIKTRSMatrixBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdShadowMapCascadeDataBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdDynamicLightBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdDynamicLightDebugBuffers);
  ShaderStorageBuffer::cleanup(renderData, renderData.rdVRControllerModelRootMatrixBuffers);

  Texture::cleanup(renderData, renderData.rdSkyboxTexture);

  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpSkinningDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeTransformDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeMatrixMultDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpSelectionDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpSkinningSelectionDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpSkinningMorphDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpSkinningMorphSelectionDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpLevelDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdLineDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdSphereDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdGroundMeshDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdSkyboxDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdCompositeDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdSSAODescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdSSAOBlurDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdLightSphereDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdLightSphereShadowsDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdDynLightDebugSphereDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeSphereTransformDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeSphereMatrixMultDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeIKDescriptorSets.data());
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdAssimpComputeBoundingSpheresDescriptorSets.data());;
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdSwapchainCopyDescriptorSets.data());;
  vkFreeDescriptorSets(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, renderData.rdNumFramesInFlight,
    renderData.rdVRControllerDescriptorSets.data());;

  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpTextureDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeTransformDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeTransformPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeBoundingSpheresDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeBoundingSpheresPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphSelectionDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningMorphPerModelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpLevelDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdLineDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSphereDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdGroundMeshDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSkyboxDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdCompositeDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSSAODescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSSAOBlurDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdLightSphereDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdLightSphereShadowDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdSwapchainCopyDescriptorLayout, nullptr);
  vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdVRControllerDescriptorLayout, nullptr);

  vkDestroyDescriptorPool(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, nullptr);

  cleanupGBuffer(renderData);
  cleanupShadowMapBuffer(renderData);
  cleanupDepthBuffer(renderData);
  cleanupImages(renderData);
  cleanupDepthBufferCubeMap(renderData, renderData.rdDynamicLightShadowData);

  vmaDestroyAllocator(renderData.rdAllocator);

  renderData.rdVkbSwapchain.destroy_image_views(renderData.rdSwapchainImageViews);
  vkb::destroy_swapchain(renderData.rdVkbSwapchain);

#if defined(__ANDROID__)
  if (renderData.rdVkbDevice.device != VK_NULL_HANDLE) {
    vkDestroyDevice(renderData.rdVkbDevice.device, nullptr);
    renderData.rdVkbDevice.device = VK_NULL_HANDLE;
  }
#else
  vkb::destroy_device(renderData.rdVkbDevice);
  vkb::destroy_surface(renderData.rdVkbInstance.instance, renderData.rdSurface);
#endif
  vkb::destroy_instance(renderData.rdVkbInstance);

  Logger::log(1, "%s: Vulkan renderer destroyed\n", __FUNCTION__);
}
