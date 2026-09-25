#include <UniformBuffer.h>

#include <VkBootstrap.h>

bool UniformBuffer::init(VkRenderData& renderData, VkUniformBufferData &uboData, size_t size) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

  VmaAllocationCreateInfo vmaAllocInfo{};
  vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

  VkResult result = vmaCreateBuffer(renderData.rdAllocator, &bufferInfo, &vmaAllocInfo, &uboData.buffer, &uboData.bufferAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate uniform buffer via VMA (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  return true;
}

void UniformBuffer::uploadData(VkRenderData &renderData, VkUniformBufferData &uboData, std::vector<glm::vec4> data) {
  void* pData;
  VkResult result = vmaMapMemory(renderData.rdAllocator, uboData.bufferAlloc, &pData);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not map uniform buffer memory (error: %i)\n", __FUNCTION__, result);
    return;
  }
  std::memcpy(pData, data.data(), data.size() * sizeof(glm::vec4));
  vmaUnmapMemory(renderData.rdAllocator, uboData.bufferAlloc);
  vmaFlushAllocation(renderData.rdAllocator, uboData.bufferAlloc, 0, uboData.bufferSize);
}

void UniformBuffer::cleanup(VkRenderData& renderData, VkUniformBufferData &uboData) {
  vmaDestroyBuffer(renderData.rdAllocator, uboData.buffer, uboData.bufferAlloc);
}
