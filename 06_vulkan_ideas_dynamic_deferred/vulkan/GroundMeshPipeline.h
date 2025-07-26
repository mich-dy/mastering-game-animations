/* Vulkan graphics pipeline with shaders */
#pragma once

#include <string>
#include <vulkan/vulkan.h>

#include "VkRenderData.h"

class GroundMeshPipeline {
  public:
    static bool init(VkRenderData &renderData, std::vector<VkFormat> colorAttachmentFormats,
      VkPipelineLayout& pipelineLayout, VkPipeline& pipeline, std::string vertexShaderFilename, std::string fragmentShaderFilename);
    static void cleanup(VkRenderData &renderData, VkPipeline &pipeline);
};
