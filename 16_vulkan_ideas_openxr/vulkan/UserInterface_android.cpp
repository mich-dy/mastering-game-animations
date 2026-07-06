#if defined(__ANDROID__)

#include <UserInterface.h>

bool UserInterface::init(VkRenderData& renderData) {
  (void)renderData;
  return true;
}

void UserInterface::hideMouse(bool hide) {
  (void)hide;
}

void UserInterface::createFrame(VkRenderData& renderData) {
  (void)renderData;
}

void UserInterface::createSettingsWindow(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, ModelInstanceCamCallbacks &modInstCamCallbacks) {
  (void)renderData;
  (void)modInstCamData;
  (void)modInstCamCallbacks;
}

void UserInterface::createDebugWindow(VkRenderData &renderData) {
  (void)renderData;
}

void UserInterface::createStatusBar(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, ModelInstanceCamCallbacks &modInstCamCallbacks) {
  (void)renderData;
  (void)modInstCamData;
  (void)modInstCamCallbacks;
}

void UserInterface::createPositionsWindow(VkRenderData &renderData, ModelInstanceCamData &modInstCamData, ModelInstanceCamCallbacks &modInstCamCallbacks) {
  (void)renderData;
  (void)modInstCamData;
  (void)modInstCamCallbacks;
}

void UserInterface::resetPositionWindowOctreeView() {}

void UserInterface::updateDescriptorSets(VkRenderData& renderData) {
  (void)renderData;
}

void UserInterface::render(VkRenderData& renderData) {
  (void)renderData;
}

void UserInterface::cleanup(VkRenderData& renderData) {
  (void)renderData;
}

#endif