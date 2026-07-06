#pragma once
#include <cstdint>

#include <glm/glm.hpp>

// Vulkan also before GLFW
#include <vulkan/vulkan.h>
#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

#include <VkRenderData.h>

class VkHelper {
  public:
    static bool initVulkan(VkRenderData& renderData);

    static bool deviceInit(VkRenderData& renderData);
    static bool initVma(VkRenderData& renderData);
    static bool getQueues(VkRenderData& renderData);
    static bool createSwapchain(VkRenderData& renderData);
    static bool createCommandPools(VkRenderData& remderData);
    static bool createCommandBuffers(VkRenderData& renderData);
    static bool createDescriptorPool(VkRenderData& renderData);
    static bool createDescriptorLayouts(VkRenderData& renderData);
    static bool createDescriptorSets(VkRenderData& renderData);
    static bool createPipelineLayouts(VkRenderData& renderData);
    static bool createPipelines(VkRenderData& renderData);
    static bool createSyncObjects(VkRenderData& renderData);
    static bool createVertexBuffers(VkRenderData& renderData);
    static bool createUBOs(VkRenderData& renderData);
    static bool createSSBOs(VkRenderData& renderData);

    static bool createXRPipeline(VkRenderData& renderData, VkFormat format);
    static void destroyXRPipeline(VkRenderData& renderData);

    static void enableBlending(VkRenderData &renderData, uint32_t attachmentCount);
    static void disableBlending(VkRenderData &renderData, uint32_t attachmentCount);

    static bool createImages(VkRenderData& renderData);
    static void cleanupImages(VkRenderData& renderData);

    static bool recreateSwapchain(VkRenderData& renderData);

    static float getPixelValueFromPos(VkRenderData &renderData, VkImage srcImage, int xPos, int yPos);

    static bool createShadowMapBuffer(VkRenderData& renderData);
    static void cleanupShadowMapBuffer(VkRenderData& renderData);

    static bool createDepthBuffer(VkRenderData& renderData);
    static void cleanupDepthBuffer(VkRenderData& renderDat);

    static bool createDepthBufferCubeMap(VkRenderData& renderData, VkImageData& imageData, uint32_t numDynLigthsWithShadow);
    static void cleanupDepthBufferCubeMap(VkRenderData& renderData, VkImageData& imageData);

    static void transitionImageLayout(VkRenderData& renderData, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount);

    static void initSSAO(VkRenderData& renderData);
    static bool createSSAONoiseTexture(VkRenderData& renderData, std::vector<glm::vec4> noiseData);
    static void cleanupSSAONoiseTexture(VkRenderData& renderData);

    static bool createGBuffer(VkRenderData& renderData);
    static void cleanupGBuffer(VkRenderData& renderData);
    static void updateImageDescriptorSets(VkRenderData& renderData);

    static void updateDescriptorSets(VkRenderData& renderData);
    static void updateComputeDescriptorSets(VkRenderData& renderData);
    static void updateLevelDescriptorSets(VkRenderData& renderData);
    static void updateSphereComputeDescriptorSets(VkRenderData& renderData);
    static void updateIKComputeDescriptorSets(VkRenderData& renderData);

    static void runComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model,
       int numInstances, uint32_t modelOffset, uint32_t instanceOffset, bool useEmtpyBoneOffsets = false);

    static void runBoundingSphereComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances,
      uint32_t modelOffset);

    static bool runIKComputeShaders(VkRenderData& renderData, std::shared_ptr<AssimpModel> model, int numInstances, uint32_t modelOffset);

    static void cleanup(VkRenderData& renderData);
};
