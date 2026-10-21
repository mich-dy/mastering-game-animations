#include <cstring>

#include <VertexBuffer.h>
#include <CommandBuffer.h>

bool VertexBuffer::init(VkRenderData &renderData, std::vector<VkVertexBufferData> &vertexBufferData,
    unsigned int bufferSize) {
  vertexBufferData.resize(renderData.rdNumFramesInFlight);
  bool success = true;

  for (int i = 0; i < vertexBufferData.size(); ++i) {
    if (!init(renderData, vertexBufferData.at(i), bufferSize)) {
      success = false;
    }
  }
  return success;
}

bool VertexBuffer::init(VkRenderData &renderData, VkVertexBufferData &vertexBufferData,
    unsigned int bufferSize) {
  // avoid errors causes by zero buffer size
  if (bufferSize == 0) {
    bufferSize = 1024;
  }

  // vertex buffer
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = bufferSize;
  bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo bufferAllocInfo{};
  bufferAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  VkResult result = vmaCreateBuffer(renderData.rdAllocator, &bufferInfo, &bufferAllocInfo,
    &vertexBufferData.buffer, &vertexBufferData.bufferAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate vertex buffer via VMA (error: %i)\n", __FUNCTION__, result);
    return false;
  }

  // staging buffer for copy
  VkBufferCreateInfo stagingBufferInfo{};
  stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingBufferInfo.size = bufferSize;;
  stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

  VmaAllocationCreateInfo stagingAllocInfo{};
  stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

  result = vmaCreateBuffer(renderData.rdAllocator, &stagingBufferInfo, &stagingAllocInfo,
    &vertexBufferData.stagingBuffer, &vertexBufferData.stagingBufferAlloc, nullptr);
  if (result != VK_SUCCESS) {
    Logger::log(1, "%s error: could not allocate vertex staging buffer via VMA (error: %i)\n",
      __FUNCTION__, result);
    return false;
  }
  vertexBufferData.bufferSize = bufferSize;
  return true;
}

bool VertexBuffer::uploadToGPU(VkRenderData &renderData, VkVertexBufferData &vertexBufferData) {
  VkBufferMemoryBarrier vertexBufferBarrier{};
  vertexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  vertexBufferBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  vertexBufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  vertexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  vertexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  vertexBufferBarrier.buffer = vertexBufferData.stagingBuffer;
  vertexBufferBarrier.offset = 0;
  vertexBufferBarrier.size = vertexBufferData.bufferSize;

  VkBufferCopy stagingBufferCopy{};
  stagingBufferCopy.srcOffset = 0;
  stagingBufferCopy.dstOffset = 0;
  stagingBufferCopy.size = vertexBufferData.bufferSize;

  // trigger data transfer via command buffer
  VkCommandBuffer commandBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  vkCmdCopyBuffer(commandBuffer, vertexBufferData.stagingBuffer,
   vertexBufferData.buffer, 1, &stagingBufferCopy);
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &vertexBufferBarrier, 0, nullptr);

  if (!CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool, commandBuffer, renderData.rdGraphicsQueue)) {
    return false;
  }

  return true;
}

void VertexBuffer::cleanup(VkRenderData &renderData, std::vector<VkVertexBufferData> &vertexBufferData) {
  for (int i = 0; i < vertexBufferData.size(); ++i) {
    cleanup(renderData, vertexBufferData.at(i));
  }
}

void VertexBuffer::cleanup(VkRenderData &renderData, VkVertexBufferData &vertexBufferData) {
  vmaDestroyBuffer(renderData.rdAllocator, vertexBufferData.stagingBuffer, vertexBufferData.stagingBufferAlloc);
  vmaDestroyBuffer(renderData.rdAllocator, vertexBufferData.buffer, vertexBufferData.bufferAlloc);
}
