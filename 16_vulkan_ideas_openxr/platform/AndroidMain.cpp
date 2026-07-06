#if defined(__ANDROID__)

#include <AppWindow.h>
#include <Logger.h>
#include <Platform.h>

#include <android/log.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <thread>

struct AndroidAppState {
  bool resumed = false;
};

static void handleAppCmd(android_app *app, int32_t cmd) {
  auto *appState = static_cast<AndroidAppState *>(app->userData);

  switch (cmd) {
    case APP_CMD_RESUME:
      if (appState) {
        appState->resumed = true;
      }
      break;
    case APP_CMD_PAUSE:
      if (appState) {
        appState->resumed = false;
      }
      break;
    default:
      break;
  }
}

void android_main(struct android_app *app) {
  app->onAppCmd = handleAppCmd;

  AndroidAppState appState{};
  app->userData = &appState;

  Platform::setAndroidApp(app);
  Platform::setActivity(app->activity);

  JNIEnv *env = nullptr;
  app->activity->vm->AttachCurrentThread(&env, nullptr);

  AppWindow window{};

  while (app->destroyRequested == 0) {
    int events = 0;
    android_poll_source *source = nullptr;

    while (ALooper_pollOnce(appState.resumed ? 0 : -1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
      if (source) {
        source->process(app, source);
      }

      if (app->destroyRequested != 0) {
        break;
      }
    }

    if (!window.isInitialized()) {
      if (!Platform::prepareAssetRoot()) {
        Logger::log(1, "%s error: failed to prepare Android assets\n", __FUNCTION__);
        break;
      }

      if (!window.init(1280, 720, "Vulkan Renderer - AndroidXR")) {
        Logger::log(1, "%s error: AppWindow init failed\n", __FUNCTION__);
        break;
      }
    }

    if (appState.resumed) {
      window.mainLoopOnce();

      if (!window.isRunning()) {
        ANativeActivity_finish(app->activity);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  if (window.isInitialized()) {
    window.cleanup();
  }

  app->activity->vm->DetachCurrentThread();
}

#endif