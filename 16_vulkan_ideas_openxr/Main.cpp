#if !defined(__ANDROID__)

#include <memory>
#include <string>

#include <AppWindow.h>
#include <Logger.h>

int main(int argc, char *argv[]) {
  AppWindow w{};

  if (!w.init(1280, 720, "Vulkan Renderer - Collecting Ideas - OpenXR")) {
    Logger::log(1, "%s error: Window init error\n", __FUNCTION__);
    return -1;
  }

  Logger::log(1, "%s: Window initialized, starting main loop\n", __FUNCTION__);
  w.mainLoop();

  Logger::log(1, "%s: main loop finished, cleaning up\n", __FUNCTION__);
  w.cleanup();

  Logger::log(1, "%s: application finished\n", __FUNCTION__);
  return 0;
}

#endif
