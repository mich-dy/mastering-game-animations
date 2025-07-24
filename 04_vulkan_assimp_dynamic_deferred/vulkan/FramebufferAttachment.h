#pragma once

#include <vulkan/vulkan.h>

#include <VkRenderData.h>

class FramebufferAttachment {
  public:
    static bool init(VkRenderData& renderData, VkFrameBufferData& bufferData, VkFormat format, VkImageUsageFlags flags);
    static void cleanup(VkRenderData& renderData, VkFrameBufferData& bufferData);
};
