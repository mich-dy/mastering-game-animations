// Vulkan uniform buffer object
#pragma once
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <VkRenderData.h>
#include <Logger.h>

class VertexBuffer {
  public:
    static bool init(VkRenderData &renderData, std::vector<VkVertexBufferData> &vertexBufferData,
      unsigned int bufferSize = 1024);
    static bool init(VkRenderData &renderData, VkVertexBufferData &vertexBufferData,
      unsigned int bufferSize = 1024);

    template <typename T>
    static bool uploadData(VkRenderData &renderData, VkVertexBufferData &vertexBufferData, std::vector<T> vertexData) {
      unsigned int vertexDataSize = vertexData.size() * sizeof(T);

      // buffer too small, resize
      if (vertexBufferData.bufferSize < vertexDataSize) {
        cleanup(renderData, vertexBufferData);

        if (!init(renderData, vertexBufferData, vertexDataSize)) {
          Logger::log(1, "%s error: could not create vertex buffer of size %i bytes\n",
            __FUNCTION__, vertexDataSize);
          return false;
        }
        Logger::log(1, "%s: vertex buffer resize to %i bytes\n", __FUNCTION__, vertexDataSize);
        vertexBufferData.bufferSize = vertexDataSize;
      }

      // copy data to staging buffer
      void* data;
      VkResult result = vmaMapMemory(renderData.rdAllocator, vertexBufferData.stagingBufferAlloc, &data);
      if (result != VK_SUCCESS) {
        Logger::log(1, "%s error: could not map memory (error: %i)\n", __FUNCTION__, result);
        return false;
      }
      std::memcpy(data, vertexData.data(), vertexDataSize);
      vmaUnmapMemory(renderData.rdAllocator, vertexBufferData.stagingBufferAlloc);
      vmaFlushAllocation(renderData.rdAllocator, vertexBufferData.stagingBufferAlloc, 0, vertexDataSize);

      // trigger upload
      return uploadToGPU(renderData, vertexBufferData);
    }

    static void cleanup(VkRenderData &renderData, std::vector<VkVertexBufferData> &vertexBufferData);
    static void cleanup(VkRenderData &renderData, VkVertexBufferData &vertexBufferData);

  private:
    static bool uploadToGPU(VkRenderData &renderData, VkVertexBufferData &vertexBufferData);
};
