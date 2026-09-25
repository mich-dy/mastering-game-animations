// Vulkan uniform buffer object
#pragma once

#include <vulkan/vulkan.h>

#include <VkRenderData.h>
#include <Logger.h>

class UniformBuffer {
  public:
    static bool init(VkRenderData &renderData, VkUniformBufferData &uboData, size_t size);

    template <typename T>
    static void uploadData(VkRenderData &renderData, VkUniformBufferData &uboData, T uploadData) {
      void* data;
      VkResult result = vmaMapMemory(renderData.rdAllocator, uboData.bufferAlloc, &data);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not map uniform buffer memory (error: %i)\n", __FUNCTION__, result);
        return;
      }
      std::memcpy(data, &uploadData, sizeof(T));
      vmaUnmapMemory(renderData.rdAllocator, uboData.bufferAlloc);
      vmaFlushAllocation(renderData.rdAllocator, uboData.bufferAlloc, 0, uboData.bufferSize);
    }

    static void uploadData(VkRenderData &renderData, VkUniformBufferData &uboData, std::vector<glm::vec4> data);
    static void cleanup(VkRenderData &renderData, VkUniformBufferData &uboData);
};
