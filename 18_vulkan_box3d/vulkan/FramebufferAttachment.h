#pragma once

#include <vulkan/vulkan.h>

#include <VkRenderData.h>

class FramebufferAttachment {
  public:
    static bool init(VkRenderData& renderData, VkImageData& bufferData, VkFormat format, VkImageUsageFlags flags, VkExtent2D size, uint32_t numLayers = 1);
    static void cleanup(VkRenderData& renderData, VkImageData& bufferData);

  private:
    static bool createImage(VkRenderData& renderData, VkImage& image, VmaAllocation& allocation, VkFormat format, VkImageUsageFlags flags, VkExtent2D size, uint32_t numLayers);
    static bool createImageView(VkRenderData& renderData, VkImage& image, VkImageView& imageView, VkFormat format, VkImageUsageFlags flags, uint32_t numLayers);
    static bool createSampler(VkRenderData& renderData, VkSampler& sampler);
};
