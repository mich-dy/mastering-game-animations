// Vulkan renderer
#pragma once

#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include <memory>
#include <map>
#include <chrono>
#include <random>
#include <tuple>

#include <glm/glm.hpp>

// Vulkan also before GLFW
#include <vulkan/vulkan.h>
#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <Timer.h>
#include <Texture.h>
#include <UniformBuffer.h>
#include <UserInterface.h>
#include <VertexBuffer.h>
#include <ShaderStorageBuffer.h>
#include <CameraSettings.h>
#include <ModelSettings.h>
#include <CoordArrowsModel.h>
#include <RotationArrowsModel.h>
#include <ScaleArrowsModel.h>
#include <SimpleSphereModel.h>
#include <AssimpModel.h>
#include <AssimpInstance.h>
#include <Octree.h>
#include <BoundingBox3D.h>
#include <TriangleOctree.h>
#include <GraphEditor.h>
#include <SingleInstanceBehavior.h>
#include <BehaviorManager.h>
#include <Callbacks.h>
#include <AssimpLevel.h>
#include <IKSolver.h>
#include <PathFinder.h>
#include <SkyboxModel.h>
#include <AssimpDynLight.h>
#include <DynamicLightDebugModel.h>
#include <FullSphereModel.h>
#include <AssimpVRController.h>

#include <VkRenderData.h>
#include <ModelInstanceCamData.h>
#include <ModelInstanceCamCallbacks.h>

#include <Callbacks.h>

class VkRenderer {
  public:
#if defined(__ANDROID__)
    bool init(void *window, std::vector<std::string> &deviceExtForXR, std::vector<std::string> &instExtsForXR);
    void setXRContext(uintptr_t instanceHandle, uint64_t systemId);
    bool createVulkanPipelines();
#else
    bool init(GLFWwindow *window, std::vector<std::string> &deviceExtForXR, std::vector<std::string> &instExtsForXR);
#endif
    void setSize(unsigned int width, unsigned int height);

    VkDevice getDevice();
    VkInstance getInstance();
    VkPhysicalDevice getPhysicalDevice();
    bool createXRPipeline(VkFormat format);
    void destroyXRPipeline();

    void setPhysicalDevice(VkPhysicalDevice physDevice);
    void setRendererMICCallbacks(ModelInstanceCamCallbacks &callbacks);

    std::pair<uint32_t, uint32_t> getQueueFamilyAndIndex();
    std::pair<float, float> getNearAndFarPlane();

    void setXRSize(unsigned int width, unsigned int height);

    bool draw(float deltaTime);

    bool initDraw(float deltaTime);
    bool acquireDesktopImage();
    bool updateLevelAndModels(float deltaTime);
    bool renderGraphics();
    bool copyToXRSwapchain(VkImageView imageVIew);
    bool updateCamera(XRProjectionViewMatrices &matrices, float deltaTime);
    bool endRendering();
    bool submitGraphics();
    bool checkForSelection();
    bool presentDesktopImage();
    bool finishDraw();

    bool updateXRControllerPositions(std::array<glm::mat4, 2> &transformMatrix);
    bool drawXRControllers(bool shadowMapPass = false, bool dynamicsShadows = false, uint32_t dynLight = 0);
    bool moveCamera(glm::vec3 amount);
    void setXRVisibilityMask(XRVisibilityMask visibilityMask);
    void drawXRVisibilityMask();

    void handleKeyEvents(int key, int scancode, int action, int mods);
    void handleMouseButtonEvents(int button, int action, int mods);
    void handleMousePositionEvents(double xPos, double yPos);
    void handleMouseWheelEvents(double xOffset, double yOffset);

    void addNullModelAndInstance();
    void removeAllModelsAndInstances();

    bool hasModel(std::string modelFileName);
    std::shared_ptr<AssimpModel> getModel(std::string modelFileName);
    bool addModel(std::string modelFileName, bool addInitialInstance = true, bool withUndo = true);
    void addExistingModel(std::shared_ptr<AssimpModel> model, int indexPos);
    void deleteModel(std::string modelFileName, bool withUnd = true);

    std::shared_ptr<AssimpInstance> addInstance(std::shared_ptr<AssimpModel> model, bool withUndo = true);
    void addExistingInstance(std::shared_ptr<AssimpInstance> instance, int indexPos, int indexPerModelPos);
    void addInstances(std::shared_ptr<AssimpModel> model, int numInstances);
    void deleteInstance(std::shared_ptr<AssimpInstance> instance, bool withUndo = true);
    void cloneInstance(std::shared_ptr<AssimpInstance> instance);
    void cloneInstances(std::shared_ptr<AssimpInstance> instance, int numClones);
    std::shared_ptr<AssimpInstance> getInstanceById(int instanceId);

    void centerInstance(std::shared_ptr<AssimpInstance> instance);

    void addBehavior(std::shared_ptr<AssimpInstance> instance, std::shared_ptr<SingleInstanceBehavior> behavior);
    void delBehavior(std::shared_ptr<AssimpInstance> instance);
    void postDelNodeTree(std::string nodeTreeName);
    void updateInstanceSettings(std::shared_ptr<AssimpInstance> instance, graphNodeType nodeType, instanceUpdateType updateType, nodeCallbackVariant data, bool extraSetting);
    void addBehaviorEvent(std::shared_ptr<AssimpInstance> instance, nodeEvent event);

    void addModelBehavior(std::string modelName, std::shared_ptr<SingleInstanceBehavior> behavior);
    void delModelBehavior(std::string modelName);

    bool hasLevel(std::string levelFileName);
    std::shared_ptr<AssimpLevel> getLevel(std::string levelFileName);
    bool addLevel(std::string levelFileName, bool updateVertexData = true);
    void deleteLevel(std::string levelFileName);

    std::shared_ptr<AssimpDynLight> addDynLight();
    void deleteDynLight(std::shared_ptr<AssimpDynLight> light);
    void cloneDynLight(std::shared_ptr<AssimpDynLight> light);
    void centerDynLight(std::shared_ptr<AssimpDynLight> light);

    void requestExitApplication();
    void doExitApplication();

    ModelInstanceCamData& getModInstCamData();

    std::shared_ptr<BoundingBox3D> getWorldBoundaries();

    void cleanup();

  private:
    const int LIGHT_OBJECT_OFFSET = 1'000'000;

    void drawScene(bool shadowMapPass = false, bool dynamicsShadows = false, uint32_t dynLight = 0);

    uint32_t mImageIndex = 0;

    VkRenderData mRenderData{};
    ModelInstanceCamData mModelInstCamData{};
    ModelInstanceCamCallbacks mModelInstCamCallbacks{};

    UserInterface mUserInterface{};

    // for animated and non-animated models
    std::vector<glm::mat4> mWorldPosMatrices{};
    int mWorldPosOffset;

    // color hightlight for selection etc
    std::vector<glm::vec2> mSelectedInstance{};

    // for animated models
    std::vector<PerInstanceAnimData> mPerInstanceAnimData{};
    std::vector<glm::mat4> mShaderBoneMatrices{};

    std::vector<AABB> mPerInstanceAABB{};
    std::shared_ptr<VkSimpleMesh> mAABBMesh = nullptr;

    // bounding sphere compute shader
    std::vector<glm::mat4> mSphereWorldPosMatrices{};
    std::vector<PerInstanceAnimData> mSpherePerInstanceAnimData{};

    CoordArrowsModel mCoordArrowsModel{};
    RotationArrowsModel mRotationArrowsModel{};
    ScaleArrowsModel mScaleArrowsModel{};

    VkSimpleMesh mCoordArrowsMesh{};
    std::shared_ptr<VkSimpleMesh> mLineMesh = nullptr;

    SimpleSphereModel mSphereModel{};
    SimpleSphereModel mCollidingSphereModel{};
    FullSphereModel mFullSphereModel{};
    SimpleSphereModel mFullSphereDebugModel{};
    VkSimpleMesh mSphereMesh{};
    VkSimpleMesh mCollidingSphereMesh{};
    VkSimpleMesh mFullSphereMesh{};
    VkSimpleMesh mFullSphereDebugMesh{};

    unsigned int mLineIndexCount = 0;
    uint32_t mCollidingSphereCount = 0;
    uint32_t mSphereVertexCount = 0;

    bool mMouseLock = false;
    int mMouseXPos = 0;
    int mMouseYPos = 0;
    float mMouseWheelScale = 1.0f;
    int mMouseWheelScaleShiftKey = 0;
    bool mMouseWheelScrolling = false;
    std::chrono::time_point<std::chrono::steady_clock> mMouseWheelLastScrollTime{};
    CameraSettings mSavedCameraWheelSettings{};

    bool mMousePick = false;
    int mSavedSelectedInstanceId = 0;

    bool mMouseMove = false;
    bool mMouseMoveVertical = false;
    int mMouseMoveVerticalShiftKey = 0;
    InstanceSettings mSavedInstanceSettings{};

    void handleMovementKeys();
    void updateTriangleCount();
    void updateLevelTriangleCount();
    void assignInstanceIndices();

    // identity matrices for view and perspective, zero matrix for light and fog, etc 
    VkRenderUploadData mRenderUploadData{};

    std::string mOrigWindowTitle;
    void setModeInWindowTitle();
    void setAppMode(appMode newMode);

    void toggleFullscreen();
    void checkMouseEnable();

    bool mApplicationRunning = false;

    void undoLastOperation();
    void redoLastOperation();

    void createSettingsContainerCallbacks();
    void clearUndoRedoStacks();

    const std::string mDefaultConfigFileName = "config/_conf.acfg";
    bool loadConfigFile(std::string configFileName);
    bool saveConfigFile(std::string configFileName);
    void createEmptyConfig();

    void loadDefaultFreeCam();

    bool mConfigIsDirty = false;
    std::string mWindowTitleDirtySign;
    void setConfigDirtyFlag(bool flag);
    bool getConfigDirtyFlag();

    void cloneCamera();
    void deleteCamera();
    CameraSettings mSavedCameraSettings{};

    glm::mat4 mVulkanViewCorrectionMatrix{};

    std::string generateUniqueCameraName(std::string camBaseName);
    bool checkCameraNameUsed(std::string cameraName);

    std::vector<glm::vec3> getPositionOfAllInstances();

    void initOctree(int thresholdPerBox, int maxDepth );
    std::shared_ptr<Octree> mOctree = nullptr;
    std::shared_ptr<BoundingBox3D> mWorldBoundaries = nullptr;

    bool createAABBLookup(std::shared_ptr<AssimpModel> model);
    void createAABBDebugLiness(std::vector<std::shared_ptr<AssimpInstance>> instances, glm::vec4 aabbColor);
    void createInstanceCollisionDebug();
    bool createSelectedBoundingSpheres();
    bool createCollidingBoundingSpheres();
    bool createAllBoundingSpheres();
    std::map<int, std::vector<glm::vec4>> mBoundingSpheresPerInstance{};

    bool checkForInstanceCollisions();
    void checkForBorderCollisions();
    void checkForBoundingSphereCollisions();
    void reactToInstanceCollisions();

    void resetCollisionData();

    void findInteractionInstances();
    void createInteractionDebug();

    std::shared_ptr<GraphEditor> mGraphEditor = nullptr;
    void editGraph(std::string graphName);
    std::shared_ptr<SingleInstanceBehavior> createEmptyGraph();

    std::shared_ptr<BehaviorManager> mBehaviorManager = nullptr;
    instanceNodeActionCallback mInstanceNodeActionCallbackFunction;

    std::vector<glm::vec4> mFaceAnimPerInstanceData{};

    std::vector<glm::mat4> mLevelWorldPosMatrices{};

    void generateLevelVertexData();
    void generateLevelAABB();
    void generateLevelOctree();
    void generateLevelWireframe();

    void resetLevelData();
    void initTriangleOctree(int thresholdPerBox, int maxDepth);
    std::shared_ptr<TriangleOctree> mTriangleOctree = nullptr;

    void checkForLevelCollisions();
    const float GRAVITY_CONSTANT = 9.81f;

    AABB mAllLevelAABB{};
    std::shared_ptr<VkSimpleMesh> mLevelAABBMesh = nullptr;
    std::shared_ptr<VkSimpleMesh> mLevelOctreeMesh = nullptr;
    std::shared_ptr<VkSimpleMesh> mLevelWireframeMesh = nullptr;
    std::shared_ptr<VkSimpleMesh> mLevelCollidingTriangleMesh = nullptr;

    IKSolver mIKSolver{};
    std::shared_ptr<VkSimpleMesh> mIKFootPointMesh = nullptr;
    std::array<std::vector<glm::vec3>, 2> mNewNodePositions{};
    std::vector<glm::mat4> mIKWorldPositionsToSolve{};
    std::vector<glm::vec3> mIKSolvedPositions{};

    PathFinder mPathFinder{};
    void generateGroundTriangleData();
    std::shared_ptr<VkSimpleMesh> mLevelGroundNeighborsMesh = nullptr;
    std::shared_ptr<VkSimpleMesh> mInstancePathMesh = nullptr;

    uint32_t mGroundMeshVertexCount = 0;

    std::vector<int> getNavTargets();
    std::random_device mRandomDevice{};
    std::default_random_engine mRandomEngine{};

    SkyboxModel mSkyboxModel{};

    bool initUserInterface();
    bool recreateSwapchain();

    void updateShadowMapCascades();

    std::shared_ptr<AssimpModel> mLightModel;
    void resetLightData();
    void assignLightIndices();
    void generateShaderLightData();
    void updateShaderLightData();

    DynamicLightDebugModel mDynLightModel{};

    // OpenXR
    std::vector<glm::mat4> mVRHandWorldPosMatrices{};
    std::shared_ptr<AssimpModel> mRHandVRControllerModel;
    std::shared_ptr<AssimpModel> mLHandVRControllerModel;

    std::array<VkIndexBufferData, 2> mVisibilityMaskIndices{};
    std::array<VkVertexBufferData, 2> mVisibilityMaskVertices{};
    std::array<uint32_t, 2> mNumVisMaskTriangles{};
};
