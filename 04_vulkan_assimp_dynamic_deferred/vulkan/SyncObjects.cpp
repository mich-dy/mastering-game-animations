#include "SyncObjects.h"
#include "Logger.h"

#include <VkBootstrap.h>

bool SyncObjects::init(VkRenderData &renderData) {
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  renderData.rdPresentSemaphores.resize(renderData.MAX_FRAMES_IN_FLIGHT);
  renderData.rdRenderSemaphores.resize(renderData.MAX_FRAMES_IN_FLIGHT);
  renderData.rdRenderFences.resize(renderData.MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < renderData.MAX_FRAMES_IN_FLIGHT; ++i) {
    if (vkCreateSemaphore(renderData.rdVkbDevice.device, &semaphoreInfo, nullptr, &renderData.rdPresentSemaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(renderData.rdVkbDevice.device, &semaphoreInfo, nullptr, &renderData.rdRenderSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(renderData.rdVkbDevice.device, &fenceInfo, nullptr, &renderData.rdRenderFences[i]) != VK_SUCCESS) {
      Logger::log(1, "%s error: failed to init sync objects\n", __FUNCTION__);
      cleanup(renderData);
      return false;
    }
  }
  return true;
}

void SyncObjects::cleanup(VkRenderData &renderData) {
  for (int i = 0; i < renderData.MAX_FRAMES_IN_FLIGHT; ++i) {
    vkDestroySemaphore(renderData.rdVkbDevice.device, renderData.rdPresentSemaphores[i], nullptr);
    vkDestroySemaphore(renderData.rdVkbDevice.device, renderData.rdRenderSemaphores[i], nullptr);
    vkDestroyFence(renderData.rdVkbDevice.device, renderData.rdRenderFences[i], nullptr);
  }
}
