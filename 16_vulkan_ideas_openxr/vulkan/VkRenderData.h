// Vulkan
#pragma once

#include <vector>
#include <array>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <set>
#include <map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vulkan/vulkan.h>
#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>

#include <assimp/material.h>

#include <Enums.h>
#include <Callbacks.h>
#include <BoundingBox3D.h>
#include <Timer.h>

#if defined(__ANDROID__)
inline constexpr uint32_t kXRPipelineViewMask = 0;
inline constexpr uint32_t kXRShadowMapViewMask = 0;
inline constexpr uint32_t kXRDynamicShadowMapViewMask = 0;
#else
inline constexpr uint32_t kXRPipelineViewMask = 0b11;
inline constexpr uint32_t kXRShadowMapViewMask = 0b00001111;
inline constexpr uint32_t kXRDynamicShadowMapViewMask = 0b00111111;
#endif

// morph animations only need those two
struct VkMorphVertex {
  glm::vec4 position = glm::vec4(0.0f);
  glm::vec4 normal = glm::vec4(0.0f);
};

struct VkMorphMesh {
  std::vector<VkMorphVertex> morphVertices{};
};

struct VkVertex {
  glm::vec4 position = glm::vec4(0.0f); // last float is uv.x
  glm::vec4 color = glm::vec4(1.0f);
  glm::vec4 normal = glm::vec4(0.0f); // last float is uv.y
  glm::uvec4 boneNumber = glm::uvec4(0);
  glm::vec4 boneWeight = glm::vec4(0.0f);
};

struct VkMesh {
  std::vector<VkVertex> vertices{};
  std::vector<uint32_t> indices{};
  std::unordered_map<aiTextureType, std::string> textures{};
  bool usesPBRColors = false;
  // store optional morph meshes directly in renderer mesh
  std::vector<VkMorphMesh> morphMeshes{};
};

struct VkSimpleVertex {
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 color = glm::vec3(0.0f);

  VkSimpleVertex() {};
  VkSimpleVertex(glm::vec3 pos, glm::vec3 col) : position(pos), color(col) {};
};

struct VkSimpleMesh {
  std::vector<VkSimpleVertex> vertices{};
};

struct VkSkyboxVertex {
  glm::vec4 position = glm::vec4(0.0f);
};

struct VkSkyboxMesh {
  std::vector<VkSkyboxVertex> vertices{};
};

struct PerInstanceAnimData {
  uint32_t firstAnimClipNum;
  uint32_t secondAnimClipNum;
  uint32_t headLeftRightAnimClipNum;
  uint32_t headUpDownAnimClipNum;
  float firstClipReplayTimestamp;
  float secondClipReplayTimestamp;
  float headLeftRightReplayTimestamp;
  float headUpDownReplayTimestamp;
  float blendFactor;
};

struct MeshTriangle {
  int index;
  std::array<glm::vec3, 3> points{};
  glm::vec3 normal{};
  BoundingBox3D boundingBox{};
  std::array<glm::vec3, 3> edges{};
  std::array<float, 3> edgeLengths{};
};

struct TRSMatrixData{
  glm::vec4 translation{};
  glm::quat rotation{};
  glm::vec4 scale{};
};

struct TimeOfDayLightParameters {
  float timeStamp;
  float lightAngleEW;
  float lightAngleNS;
  float lightIntensity;
  glm::vec3 lightColor{};
};

struct VkRenderUploadData {
  std::array<glm::mat4, 2> viewMatrix{};
  std::array<glm::mat4, 2> projectionMatrix{};
  std::array<glm::mat4, 2> inverseViewMatrix{};
  std::array<glm::mat4, 2> inverseProjectionMatrix{};
  glm::vec4 cameraPos = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  glm::vec4 lightPos = glm::vec4(5.0f, 3.0f, 1.0f, 1.0f);
  glm::vec4 lightColor = glm::vec4(1.0f);
  float nearPlane = 0.1f;
  float farPlane =  500.0f;
  float fogDensity = 0.0f;
  float ssaoRadius = 5.0f;
  float ssaoBias = 0.1f;
  float shadowMapPCFScale = 1.0f;
  int32_t compositeDebug = 0;
  int32_t ssaoBlurEnabled = 0;
  int32_t ssaoExponent = 4;
  int32_t ssaoBlurRadius = 3;
  int32_t shadowMapEnabled = 0;
  int32_t shadowMapPCFEnabled = 0;
  int32_t shadowMapPCFRange = 1;
  int32_t colorCascadeDebug = 0;
  int32_t numDynamicLights = 0;
};

struct ShadowMapCascades {
  glm::mat4 lightViewProjectionMat{};
  glm::vec4 splitDepth{};
};

struct ShadowMapCascadeData {
  std::vector<ShadowMapCascades> cascades{};
};

struct DynamicLightData {
  glm::vec4 position = glm::vec4(0.0f);
  glm::vec4 rotation = glm::vec4(0.0f);
  glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
  float distance = 10.0f;
  float maxDistance = 32.0f;
  uint32_t type = 0;
  float cutoff = 12.5f;
  float outerCutoff = 17.5f;
  float constantAttFactor = 1.0f;
  float linearAttFactor = 0.045f;
  float quadraticAttFactor = 0.0075f;
  float shadowMapOffset = 0.001f;
  float dummy[3] = {};
};

struct VkTextureData {
  VkImage image = VK_NULL_HANDLE;
  VkImageView imageView = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VmaAllocation imageAlloc = nullptr;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct VkVertexBufferData {
  unsigned int bufferSize = 0;
  void* data = nullptr;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation bufferAlloc = VK_NULL_HANDLE;
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
};

struct VkIndexBufferData {
  size_t bufferSize = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation bufferAlloc = nullptr;
  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingBufferAlloc = nullptr;
};

struct VkUniformBufferData {
  size_t bufferSize = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation bufferAlloc = nullptr;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct VkShaderStorageBufferData {
  size_t bufferSize = 0;
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation bufferAlloc = nullptr;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

struct VkImageData {
  VkFormat format;
  VkImage image = VK_NULL_HANDLE;
  VkImageView imageView = VK_NULL_HANDLE;
  std::vector<VkImageView> imageViews{};
  VkImageView uiImageView = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VmaAllocation alloc = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  uint32_t numLayers = 1;
};

struct VulkanGBuffer {
  VkImageData color;
  VkImageData normal;
  VkImageData depth;
  uint32_t width;
  uint32_t height;
};

struct VkPushConstants {
  uint32_t pkModelStride;
  uint32_t pkWorldPosOffset;
  uint32_t pkSkinMatOffset;
  int32_t pShadowMapLayerIndex;
  uint32_t pkVirtMaskLayer;
};

struct VkComputePushConstants {
  uint32_t pkModelOffset;
  uint32_t pkInstanceOffset;
};

// OpenXR
struct XRProjectionViewMatrices {
  std::array<glm::mat4, 2> projectionMat{};
  std::array<glm::mat4, 2> viewOrientationMat{};
  std::array<glm::mat4, 2> viewTransposeMat{};
};

struct XRVisibilityMask {
  std::array<VkSimpleMesh, 2> vertices{};
  std::array<std::vector<uint32_t>, 2> indices{};
};

struct VkRenderData {
#if !defined(__ANDROID__)
  GLFWwindow *rdWindow = nullptr;
#endif
  bool rdSkipDesktopMirror = false;
  bool rdWaylandFound = false;

  uint32_t rdWidth = 0;
  uint32_t rdHalfWidth = 0;
  uint32_t rdHeight = 0;
  // window size may differ from framebuffer width/height
  int rdWindowWidth = 0;
  int rdWindowHeight = 0;
  bool rdFullscreen = false;

  unsigned int rdTriangleCount = 0;
  unsigned int rdLevelTriangleCount = 0;
  unsigned int rdMatricesSize = 0;
  unsigned int rdNumDynamicLights = 0;
  unsigned int rdNumDynamicLightsWithShadow = 0;

  float rdNearPlane = 0.1f;
  float rdFarPlane = 500.0f;

  float rdEyeSeparation = 0.1f;
  float rdFocalLength = 0.5f;

  bool rdEnableSSAO = true;
  bool rdEnableSSAOBlur = true;
  float rdSSAORadius = 5.0f;
  float rdSSAOBias = 0.1f;
  int rdSSAOExponent = 4;
  int rdSSAOBlurRadius = 3;

  bool rdEnableShadowMap = false;
  bool rdEnableShadowMapPCF = false;
  float rdShadowMapSplitLambda = 0.95f;
  float rdShadowMapPCFScale = 1.0f;
  int rdShadowMapPCFRange = 1;
  bool rdEnableShadowMapColorCascadeDebug = false;

  const uint32_t SHADOW_MAP_LAYERS = 4;
  float rdShadowMapConstantDepthBias = 1.25f;
  float rdShadowMapSlopeDepthBias = 1.75f;
  VkExtent2D rdShadowMapSize { 4096, 4096 };

  float rdDynLightShadowMapConstantDepthBias = 1.25f;
  float rdDynLightShadowMapSlopeDepthBias = 1.75f;
  VkExtent2D rdDynLightShadowMapSize { 1024, 1024 };

  ShadowMapCascadeData rdShadowMapCascadeData{};
  ShadowMapCascadeData rdDynamicLightShadowMapData{};

  std::vector<int> rdLightIndices;

  std::vector<DynamicLightData> rdLightData{};
  bool rdEnableLightDebug = false;
  std::vector<glm::vec4> rdLightDebugData{};
  bool rdEnableLightSphereDebug = false;

  compositeDebugDisplay rdCompositeDebug = compositeDebugDisplay::composite;

  float rdFrameTime = 0.0f;
  float rdMatrixGenerateTime = 0.0f;
  float rdUploadToVBOTime = 0.0f;
  float rdUploadToUBOTime = 0.0f;
  float rdDownloadFromUBOTime = 0.0f;
  float rdUIGenerateTime = 0.0f;
  float rdUIDrawTime = 0.0f;
  float rdCollisionDebugDrawTime = 0.0f;
  float rdCollisionCheckTime = 0.0f;
  float rdBehaviorTime = 0.0f;
  float rdInteractionTime = 0.0f;
  float rdFaceAnimTime = 0.0f;
  float rdLevelCollisionTime = 0.0f;
  float rdIKTime = 0.0f;
  float rdLevelGroundNeighborUpdateTime = 0.0f;
  float rdPathFindingTime = 0.0f;

  int rdMoveForward = 0;
  int rdMoveRight = 0;
  int rdMoveUp = 0;

  bool rdHighlightSelectedInstance = false;
  float rdSelectedInstanceHighlightValue = 1.0f;

  appMode rdApplicationMode = appMode::edit;
  std::unordered_map<appMode, std::string> rdAppModeMap{};

  instanceEditMode rdInstanceEditMode = instanceEditMode::move;

  appExitCallback rdAppExitCallbackFunction;
  bool rdRequestApplicationExit = false;
  bool rdNewConfigRequest = false;
  bool rdLoadConfigRequest = false;
  bool rdSaveConfigRequest = false;
  bool rdShowControlsHelpRequest = false;

  glm::vec3 rdDefaultWorldStartPos = glm::vec3(-160.0f);
  glm::vec3 rdDefaultWorldSize = glm::vec3(320.0f);
  glm::vec3 rdWorldStartPos = rdDefaultWorldStartPos;
  glm::vec3 rdWorldSize = rdDefaultWorldSize;

  collisionChecks rdCheckCollisions = collisionChecks::none;
  size_t rdNumberOfCollisions = 0;

  collisionDebugDraw rdDrawCollisionAABBs = collisionDebugDraw::none;
  collisionDebugDraw rdDrawBoundingSpheres = collisionDebugDraw::none;

  bool rdInteraction = false;
  float rdInteractionMaxRange = 10.0f;
  float rdInteractionMinRange = 1.5f;
  float rdInteractionFOV = 45.0f;
  size_t rdNumberOfInteractionCandidates = 0;
  std::set<int> rdInteractionCandidates{};
  int rdInteractWithInstanceId = 0;

  interactionDebugDraw rdDrawInteractionAABBs = interactionDebugDraw::none;
  bool rdDrawInteractionRange = false;
  bool rdDrawInteractionFOV = false;

  int rdOctreeThreshold = 10;
  int rdOctreeMaxDepth = 5;

  int rdLevelOctreeThreshold = 10;
  int rdLevelOctreeMaxDepth = 5;

  bool rdDrawLevelAABB = false;
  bool rdDrawLevelWireframe = false;
  bool rdDrawLevelOctree = false;
  bool rdDrawLevelCollisionTriangles = false;

  bool rdDrawLevelWireframeMiniMap = false;
  std::shared_ptr<VkSimpleMesh> rdLevelWireframeMiniMapMesh = nullptr;

  float rdMaxLevelGroundSlopeAngle = 0.0f;
  float rdMaxStairstepHeight = 1.0f;
  glm::vec3 rdLevelCollisionAABBExtension = glm::vec3(0.0f, 1.0f, 0.0f);

  int rdNumberOfCollidingTriangles = 0;
  int rdNumberOfCollidingGroundTriangles = 0;

  bool rdEnableSimpleGravity = false;

  bool rdEnableFeetIK = false;
  int rdNumberOfIkIteratons = 10;
  bool rdDrawIKDebugLines = false;

  std::vector<glm::mat4> rdIKMatrices{};
  std::vector<glm::mat4> rdIKModelMatrices{};

  bool rdEnableNavigation = false;

  bool rdDrawNeighborTriangles = false;
  bool rdDrawGroundTriangles = false;
  bool rdDrawInstancePaths = false;

  int rdMusicFadeOutSeconds = 0;
  int rdMusicVolume = 0;

  bool rdDrawSkybox = false;

  float rdLightSourceAngleEastWest = 40.0f;
  float rdLightSourceAngleNorthSouth = 40.0f;
  glm::vec3 rdLightSourceColor = glm::vec3(1.0f);
  float rdLightSourceIntensity = 1.0f;

  float rdFogDensity = 0.0f;

  bool rdEnableTimeOfDay = false;

  // we start at noon
  float rdTimeOfDay = 720.0f;
  float rdTimeScaleFactor = 10.0f;
  int rdLengthOfDay = 24 * 60;
  timeOfDay rdTimeOfDayPreset = timeOfDay::fullLight;

  std::map<timeOfDay, TimeOfDayLightParameters> rdTimeOfDayLightSettings{};

  bool rdEnableInfiniteGrid = true;
  std::vector<TRSMatrixData> rdTRSData{};

  Timer rdFrameTimer{};
  Timer rdMatrixGenerateTimer{};
  Timer rdUploadToVBOTimer{};
  Timer rdUploadToUBOTimer{};
  Timer rdDownloadFromUBOTimer{};
  Timer rdUIGenerateTimer{};
  Timer rdUIDrawTimer{};
  Timer rdCollisionDebugDrawTimer{};
  Timer rdCollisionCheckTimer{};
  Timer rdBehviorTimer{};
  Timer rdInteractionTimer{};
  Timer rdFaceAnimTimer{};
  Timer rdLevelCollisionTimer{};
  Timer rdIKTimer{};
  Timer rdLevelGroundNeighborUpdateTimer{};
  Timer rdPathFindingTimer{};

  VkPushConstants rdModelData{};
  VkComputePushConstants rdComputeModelData{};

  VulkanGBuffer rdGBuffer{};

  std::vector<VkUniformBufferData> rdRenderUploadDataUBOs{};
  VkUniformBufferData rdSSAOKernelSamplesUBO{};

  VkTextureData rdSkyboxTexture{};

  VkVertexBufferData rdFullscreenQuadBufferData{};

  std::vector<VkVertexBufferData> rdLineVertexBuffers{};
  std::vector<VkVertexBufferData> rdSphereVertexBuffers{};
  std::vector<VkVertexBufferData> rdLevelAABBVertexBuffers{};
  std::vector<VkVertexBufferData> rdLevelOctreeVertexBuffers{};
  std::vector<VkVertexBufferData> rdLevelWireframeVertexBuffers{};
  std::vector<VkVertexBufferData> rdIKLinesVertexBuffers{};
  std::vector<VkVertexBufferData> rdGroundMeshVertexBuffers{};
  std::vector<VkVertexBufferData> rdGroundMeshNeighborVertexBuffers{};
  std::vector<VkVertexBufferData> rdInstancePathVertexBuffers{};
  std::vector<VkVertexBufferData> rdSkyboxBuffers{};
  std::vector<VkVertexBufferData> rdDynamicLightDebugVertexBuffers{};
  std::vector<VkVertexBufferData> rdLightSphereVertexBuffers{};
  std::vector<VkVertexBufferData> rdLightSphereDebugVertexBuffers{};

  std::vector<VkShaderStorageBufferData> rdShaderModelRootMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdSelectedInstanceBuffers{};
  std::vector<VkShaderStorageBufferData> rdShaderBoneMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdPerInstanceAnimDataBuffers{};
  std::vector<VkShaderStorageBufferData> rdShaderTRSMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdSphereModelRootMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdSpherePerInstanceAnimDataBuffers{};
  std::vector<VkShaderStorageBufferData> rdSphereTRSMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdSphereBoneMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdBoundingSphereBuffers{};
  std::vector<VkShaderStorageBufferData> rdFaceAnimPerInstanceDataBuffers{};
  std::vector<VkShaderStorageBufferData> rdShaderLevelRootMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdIKBoneMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdIKTRSMatrixBuffers{};
  std::vector<VkShaderStorageBufferData> rdShadowMapCascadeDataBuffers{};
  std::vector<VkShaderStorageBufferData> rdDynamicLightBuffers{};
  std::vector<VkShaderStorageBufferData> rdDynamicLightDebugBuffers{};
  std::vector<VkShaderStorageBufferData> rdVRControllerModelRootMatrixBuffers{};

  // OpenXR specific stuff
  uint32_t rdXRWidth = 0;
  uint32_t rdXRHeight = 0;

  // Vulkan specific stuff
  const int MAX_FRAMES_IN_FLIGHT = 3;
  int rdNumFramesInFlight = 1;
  int currentFrame = 0;

  VkSurfaceKHR rdSurface = VK_NULL_HANDLE;
  VmaAllocator rdAllocator = nullptr;
  VkDeviceSize rdMinSSBOOffsetAlignment = 0;

  bool rdHasDedicatedComputeQueue = false;

  vkb::Instance rdVkbInstance{};
  vkb::PhysicalDevice rdVkbPhysicalDevice{};
  vkb::Device rdVkbDevice{};
  vkb::Swapchain rdVkbSwapchain{};

  std::vector<const char*> rdXRDeviceExtensions{};
  std::vector<const char*> rdXRInstanceExtensions{};
#if defined(__ANDROID__)
  uintptr_t rdXRInstanceHandle = 0;
  uint64_t rdXRSystemId = 0;
#endif

  std::vector<VkImage> rdSwapchainImages{};
  std::vector<VkImageView> rdSwapchainImageViews{};
  std::vector<VkFramebuffer> rdFramebuffers{};
  std::vector<VkFramebuffer> rdSelectionFramebuffers{};

  VkQueue rdGraphicsQueue = VK_NULL_HANDLE;
  VkQueue rdPresentQueue = VK_NULL_HANDLE;
  VkQueue rdComputeQueue = VK_NULL_HANDLE;

  VkImageData rdFinalImageData{};
  VkImageData rdDepthBufferData{};
  VkImageData rdSelectionImageData{};
  VkImageData rdSSAOColorBufferData{};
  VkImageData rdSSAONoiseBufferData{};
  VkImageData rdSSAOBlurBufferData{};
  VkImageData rdShadowMapCombinedDepthBufferData{};
  VkImageData rdShadowMapDepthBufferData{};
  VkImageData rdLightSpheresBufferData{};
  VkImageData rdDynamicLightShadowData{};
  VkImageData rdDynamicLightCombinedShadowData{};

  VkPipelineLayout rdAssimpPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpSkinningPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpComputeTransformaPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpComputeMatrixMultPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpComputeBoundingSpheresPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpSelectionPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpSkinningSelectionPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpSkinningMorphPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpSkinningMorphSelectionPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdAssimpLevelPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdLinePipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdSpherePipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdGroundMeshPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdSkyboxPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdCompositePipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdSSAOPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdSSAOBlurPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdLightSpherePipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdLightSphereShadowPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdSwapchainCopyPipelineLayout = VK_NULL_HANDLE;
  VkPipelineLayout rdVRControllerPipelineLayout = VK_NULL_HANDLE;

  VkPipeline rdAssimpPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpSkinningPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpComputeTransformPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpComputeHeadMoveTransformPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpComputeMatrixMultPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpComputeBoundingSpheresPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpComputeIKMatrixMultPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpSelectionPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpSkinningSelectionPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpSkinningMorphPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpSkinningMorphSelectionPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpLevelPipeline = VK_NULL_HANDLE;
  VkPipeline rdLinePipeline = VK_NULL_HANDLE;
  VkPipeline rdSpherePipeline = VK_NULL_HANDLE;
  VkPipeline rdGridLinePipeline = VK_NULL_HANDLE;
  VkPipeline rdGroundMeshMeshPipeline = VK_NULL_HANDLE;
  VkPipeline rdSkyboxPipeline = VK_NULL_HANDLE;
  VkPipeline rdCompositePipeline = VK_NULL_HANDLE;
  VkPipeline rdSSAOPipeline = VK_NULL_HANDLE;
  VkPipeline rdSSAOBlurPipeline = VK_NULL_HANDLE;
  VkPipeline rdShadowMapAssimpPipeline = VK_NULL_HANDLE;
  VkPipeline rdShadowMapAssimpSkinningPipeline = VK_NULL_HANDLE;
  VkPipeline rdShadowMapAssimpSkinningMorphPipeline = VK_NULL_HANDLE;
  VkPipeline rdShadowMapAssimpLevelPipeline = VK_NULL_HANDLE;
  VkPipeline rdDynamicShadowMapAssimpPipeline = VK_NULL_HANDLE;
  VkPipeline rdDynamicShadowMapAssimpSkinningPipeline = VK_NULL_HANDLE;
  VkPipeline rdDynamicShadowMapAssimpSkinningMorphPipeline = VK_NULL_HANDLE;
  VkPipeline rdDynamicShadowMapAssimpLevelPipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpPostCompositePipeline = VK_NULL_HANDLE;
  VkPipeline rdAssimpPostCompositeSelectionPipeline = VK_NULL_HANDLE;
  VkPipeline rdLightSpherePipeline = VK_NULL_HANDLE;
  VkPipeline rdLightSphereShadowPipeline = VK_NULL_HANDLE;
  VkPipeline rdSwapchainCopyPipeline = VK_NULL_HANDLE;
  VkPipeline rdXRSwapchainCopyPipeline = VK_NULL_HANDLE;
  VkPipeline rdXRVisMaskMeshPipeline = VK_NULL_HANDLE;

  VkPipeline rdVRControllerPipeline = VK_NULL_HANDLE;
  VkPipeline rdShadowMapVRControllerPipeline = VK_NULL_HANDLE;
  VkPipeline rdDynamicShadowMapVRControllerPipeline = VK_NULL_HANDLE;

  VkCommandPool rdCommandPool = VK_NULL_HANDLE;
  VkCommandPool rdComputeCommandPool = VK_NULL_HANDLE;

  std::vector<VkCommandBuffer> rdCommandBuffers{};
  std::vector<VkCommandBuffer> rdComputeCommandBuffers{};

  std::vector<VkSemaphore> rdPresentSemaphores{};
  std::vector<VkSemaphore> rdRenderSemaphores{};
  std::vector<VkSemaphore> rdComputeSemaphores{};
  std::vector<VkSemaphore> rdCollisionSemaphores{};

  std::vector<VkFence> rdRenderFences{};
  std::vector<VkFence> rdComputeFences{};

  VkDescriptorSetLayout rdAssimpDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSkinningDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpTextureDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeTransformDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeTransformPerModelDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeMatrixMultDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeMatrixMultPerModelDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeBoundingSpheresDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpComputeBoundingSpheresPerModelDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSelectionDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSkinningSelectionDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSkinningMorphDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSkinningMorphSelectionDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpSkinningMorphPerModelDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdAssimpLevelDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdLineDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdSphereDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdGroundMeshDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdSkyboxDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdCompositeDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdSSAODescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdSSAOBlurDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdLightSphereDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdLightSphereShadowDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdSwapchainCopyDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout rdVRControllerDescriptorLayout = VK_NULL_HANDLE;

  std::vector<VkDescriptorSet> rdAssimpDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpSkinningDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeTransformDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeMatrixMultDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpSelectionDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpSkinningSelectionDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpSkinningMorphDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpSkinningMorphSelectionDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeSphereTransformDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeSphereMatrixMultDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeBoundingSpheresDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpComputeIKDescriptorSets{};
  std::vector<VkDescriptorSet> rdAssimpLevelDescriptorSets{};
  std::vector<VkDescriptorSet> rdLineDescriptorSets{};
  std::vector<VkDescriptorSet> rdSphereDescriptorSets{};
  std::vector<VkDescriptorSet> rdGroundMeshDescriptorSets{};
  std::vector<VkDescriptorSet> rdSkyboxDescriptorSets{};
  std::vector<VkDescriptorSet> rdCompositeDescriptorSets{};
  std::vector<VkDescriptorSet> rdSSAODescriptorSets{};
  std::vector<VkDescriptorSet> rdSSAOBlurDescriptorSets{};
  std::vector<VkDescriptorSet> rdDynLightDebugSphereDescriptorSets{};
  std::vector<VkDescriptorSet> rdLightSphereDescriptorSets{};
  std::vector<VkDescriptorSet> rdLightSphereShadowsDescriptorSets{};
  std::vector<VkDescriptorSet> rdSwapchainCopyDescriptorSets{};
  std::vector<VkDescriptorSet> rdVRControllerDescriptorSets{};

  VkDescriptorPool rdDescriptorPool = VK_NULL_HANDLE;
  VkDescriptorPool rdImguiDescriptorPool = VK_NULL_HANDLE;

  // not found on my system, so load it dynamically
  PFN_vkCmdSetColorBlendEquationEXT rdvkCmdSetColorBlendEquationEXT = nullptr;
  PFN_vkCmdSetColorBlendEnableEXT rdvkCmdSetColorBlendEnableEXT = nullptr;

};
