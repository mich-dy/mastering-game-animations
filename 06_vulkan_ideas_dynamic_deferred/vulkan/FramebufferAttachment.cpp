#include <FramebufferAttachment.h>

#include <VkBootstrap.h>
#include <CommandBuffer.h>
#include <Logger.h>

bool FramebufferAttachment::init(VkRenderData& renderData, VkFrameBufferData& bufferData,
                                 VkFormat format, VkImageUsageFlags flags) {
  bufferData.format = format;

  VkImageAspectFlags aspectMask = 0;
  if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
    aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  } else  if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  } else {
    Logger::log(1, "%s error: could not detect usage flags\n", __FUNCTION__);
    return false;
  }

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent.width = renderData.rdVkbSwapchain.extent.width;
  imageInfo.extent.height = renderData.rdVkbSwapchain.extent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = flags; /* we need input attachment bit to read/write in shader, and samples bit to show in UI and debug windows */
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo imageAllocInfo{};
  imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  if (vmaCreateImage(renderData.rdAllocator, &imageInfo, &imageAllocInfo,
      &bufferData.image, &bufferData.alloc, nullptr) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image\n", __FUNCTION__);
    return false;
  }

  VkImageViewCreateInfo imageViewInfo{};
  imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewInfo.format = format;
  imageViewInfo.subresourceRange.aspectMask = aspectMask;
  imageViewInfo.subresourceRange.baseMipLevel = 0;
  imageViewInfo.subresourceRange.levelCount = 1;
  imageViewInfo.subresourceRange.baseArrayLayer = 0;
  imageViewInfo.subresourceRange.layerCount = 1;
  imageViewInfo.image = bufferData.image;

  if (vkCreateImageView(renderData.rdVkbDevice.device, &imageViewInfo, nullptr, &bufferData.imageView) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create image view\n", __FUNCTION__);
    return false;
  }

  VkCommandBuffer imageTransitionBuffer = CommandBuffer::createSingleShotBuffer(renderData, renderData.rdCommandPool);

  VkImageMemoryBarrier imageMemoryBarrier {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .newLayout = VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ,
    .image = bufferData.image,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    }
  };

  vkCmdPipelineBarrier(
    imageTransitionBuffer,
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
    0,
    0, nullptr, 0, nullptr,
    1, &imageMemoryBarrier // pImageMemoryBarriers
  );

  if (!CommandBuffer::submitSingleShotBuffer(renderData, renderData.rdCommandPool, imageTransitionBuffer, renderData.rdGraphicsQueue)) {
    Logger::log(1, "%s error: could not transition image\n", __FUNCTION__);
    return false;
  }

  // Sampler for ImGui and Debug
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_NEAREST;
  samplerInfo.minFilter = VK_FILTER_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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

  if (vkCreateSampler(renderData.rdVkbDevice.device, &samplerInfo, nullptr, &bufferData.sampler) != VK_SUCCESS) {
    Logger::log(1, "%s error: could not create sampler\n", __FUNCTION__);
    return false;
  }

  Logger::log(2, "%s: created frame buffer attachment\n", __FUNCTION__);
  return true;
}

void FramebufferAttachment::cleanup(VkRenderData& renderData, VkFrameBufferData& bufferData) {
  vkDestroySampler(renderData.rdVkbDevice.device, bufferData.sampler, nullptr);
  vkDestroyImageView(renderData.rdVkbDevice.device, bufferData.imageView, nullptr);
  vmaDestroyImage(renderData.rdAllocator, bufferData.image, bufferData.alloc);
}
