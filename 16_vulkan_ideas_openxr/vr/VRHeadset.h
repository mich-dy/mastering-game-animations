#pragma once

#include <vector>
#include <string>
#include <tuple>

#if defined(__ANDROID__)
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#else
#define VK_USE_PLATFORM_XLIB_KHR
#endif

#define XR_USE_GRAPHICS_API_VULKAN
// include Vulkan before GLFW
#include <vulkan/vulkan.h>

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

// Define any XR_USE_PLATFORM_... / XR_USE_GRAPHICS_API_... before this header file.
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <VkRenderData.h>
#include <VkRenderer.h>
#include <ModelInstanceCamCallbacks.h>

class VRHeadset {
  public:
#if defined(__ANDROID__)
    bool init(void *window, ModelInstanceCamCallbacks callbacks);
#else
    bool init(GLFWwindow *window, ModelInstanceCamCallbacks callbacks);
#endif

    std::shared_ptr<VkRenderer> getVulkanRenderer();

    bool isXRApplicationRunning();
    bool isXRSessionRunning();

    void pollEvents();
    bool draw(float deltaTime);

    void cleanup();

  private:
    std::shared_ptr<VkRenderer> mRenderer = nullptr;
    VkDevice mVulkanDevice = VK_NULL_HANDLE;

    bool beginXRFrame();
    bool renderXRFrame();
    bool endXRFrame();

    void pollActions(XrTime predictedTime);

    void createXRCameraMatrices();
    XRProjectionViewMatrices mProjViewMatrices{};

    bool createXRInstance();
    bool createXRDebugMessenger();
    bool createXRSession();
    bool createXRReferenceSpace();
    bool createXRSwapchain();

    bool createXRActionSet();
    bool suggestXRBindings();
    bool createXRActionPoses();
    bool attachActionSet();

    bool getInstanceID();
    bool getSystemID();
    bool getViewConfigViews();
    bool getEnvBlendModes();
    bool getVisibilityMask();
    bool getVulkanGraphicsRequirements();
    bool getVKDeviceExtensions();
    bool getVKInstanceExtension();

    void destroyXRInstance();
    void destroyXRDebugMessenger();
    void destroyXRReferenceSpace();
    void destroyXRSession();
    void destroyXRSwapchain();

    bool mSessionRunning = false;
    bool mApplicationRunning = true;

    unsigned int mWidth = 1920;
    unsigned int mHeight = 1080;

    float mNearPlane = 0.4f;
    float mFarPlane = 500.0f;

    XrApplicationInfo mXRAppInfo{};
    std::vector<const char *> mActiveAPILayers{};
    std::vector<const char *> mActiveInstanceExtensions{};

    std::vector<std::string> mInstanceExtensions{};
    std::vector<std::string> mAPILayers{};

    std::vector<std::string> mVKDeviceExtensionsForXR{};
    std::vector<std::string> mVKInstanceExtensionsForXR{};

    XrInstance mXRInstance = XR_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> mSwapchainCopyDescriptorSets{};

    XrFormFactor mFormFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId mSystemID{};
    XrSystemProperties mSystemProperties = { XR_TYPE_SYSTEM_PROPERTIES };

    XrSession mSession{};
    XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;

    XrSpace mLocalSpace = XR_NULL_HANDLE;
    XrSpace mViewSpace = XR_NULL_HANDLE;
    XrSpaceLocation mViewSpaceLocation{};
    XrFrameState mFrameState{XR_TYPE_FRAME_STATE};

    struct XRSwapchainInfo {
      XrSwapchain swapchain = XR_NULL_HANDLE;
      std::vector<XrSwapchainImageVulkanKHR> swapchainImages{};
      std::vector<VkImageView> swapchainImageViews{};
    };

    VkFormat mSwapchainFormat = VK_FORMAT_R8G8B8A8_UNORM;
    XRSwapchainInfo mSwapchain{};

    struct XRRenderLayerInfo {
      std::vector<XrCompositionLayerBaseHeader *> layers;
      XrCompositionLayerProjection layerProjection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
      std::vector<XrCompositionLayerProjectionView> layerProjectionViews;
    };

    XRRenderLayerInfo mRenderLayerInfos;
    uint32_t mColorImageIndex = 0;

    std::vector<XrView> mViews{};
    uint32_t mViewCount = 0;

    XrViewConfigurationType mViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;
    std::vector<XrViewConfigurationType> mApplicationViewConfigurations = { XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO };
    std::vector<XrViewConfigurationType> mViewConfigurations{};
    std::vector<XrViewConfigurationView> mViewConfigurationViews{};

    XrEnvironmentBlendMode mEnvironmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;
    std::vector<XrEnvironmentBlendMode> mApplicationEnvironmentBlendModes = { XR_ENVIRONMENT_BLEND_MODE_OPAQUE, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE };
    std::vector<XrEnvironmentBlendMode> mEnvironmentBlendModes{};

    XrGraphicsRequirementsVulkanKHR mGraphicsRequirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };

    XrPath CreateXrPath(std::string pathString);
    std::string FromXrPath(XrPath path);

    XrActionSet mActionSet{};
    void createXRAction(XrAction &xrAction, std::string name, XrActionType xrActionType, std::vector<std::string> subactionPaths = {});
    bool suggestXRBinding(std::string profilePath, std::vector<XrActionSuggestedBinding> bindings);
    XrSpace createXRActionPoseSpace(XrSession session, XrAction xrAction, std::string subactionPath);

    XrAction mPalmPoseAction{};
    std::array<XrPath, 2> mHandPaths{};
    std::array<XrSpace, 2> mHandPoseSpace{};
    std::vector<XrActionStatePose> mHandPoseState = { { .type = XR_TYPE_ACTION_STATE_POSE } , { .type = XR_TYPE_ACTION_STATE_POSE } };

    // In STAGE space, viewHeightM should be 0. In LOCAL space, it should be offset downwards, below the viewer's initial position.
    float mViewHeightM = 1.5f;
    std::vector<XrPosef> mHandPose = {
      {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -mViewHeightM}},
      {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -mViewHeightM}}
    };

    void calculateXRHandPositions();
    std::array<glm::mat4, 2> mHandTransformMatrices{};

    XrAction mFlyLeftRighAction{};
    XrActionStateFloat mFlyLeftRightState = { .type = XR_TYPE_ACTION_STATE_FLOAT };
    XrAction mFlyFwdBackAction{};
    XrActionStateFloat mFlyFwdBackState = { .type = XR_TYPE_ACTION_STATE_FLOAT };
    XrAction mFlyUpDownAction{};
    XrActionStateFloat mFlyUpDownState = { .type = XR_TYPE_ACTION_STATE_FLOAT };

    XrDebugUtilsMessengerEXT mDebugMessenger{};
    static XrBool32 handleXRErrors(XrDebugUtilsMessageSeverityFlagsEXT severity, XrDebugUtilsMessageTypeFlagsEXT type,
      const XrDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);

#if defined(__ANDROID__)
    bool initializeAndroidLoader();
    JavaVM *mJavaVM = nullptr;
    jobject mActivityContext = nullptr;
#endif
};
