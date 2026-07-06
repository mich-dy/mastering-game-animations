#include <AppWindow.h>
#include <Logger.h>
#include <ModelInstanceCamCallbacks.h>
#include <VkRenderer.h>

#if defined(__ANDROID__)
#include <Platform.h>
#endif

static ModelInstanceCamCallbacks createRendererCallbacks(AppWindow *window) {
  ModelInstanceCamCallbacks rendererMICCallbacks;
  rendererMICCallbacks.micGetWindowTitleFunction = [window]() { return window->getWindowTitle(); };
  rendererMICCallbacks.micSetWindowTitleFunction = [window](std::string windowTitle) { window->setWindowTitle(windowTitle); };
  return rendererMICCallbacks;
}

bool AppWindow::init(unsigned int width, unsigned int height, std::string title) {
#if !defined(__ANDROID__)
  if (!glfwInit()) {
    Logger::log(1, "%s: glfwInit() error\n", __FUNCTION__);
    return false;
  }

  if (!glfwVulkanSupported()) {
    glfwTerminate();
    Logger::log(1, "%s error: Vulkan is not supported\n", __FUNCTION__);
    return false;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif

  mWindowTitle = title;

#if !defined(__ANDROID__)
  mWindow = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  if (!mWindow) {
    glfwTerminate();
    Logger::log(1, "%s error: Could not create window\n", __FUNCTION__);
    return false;
  }
#endif

  ModelInstanceCamCallbacks rendererMICCallbacks = createRendererCallbacks(this);

#if !defined(__ANDROID__)
  rendererMICCallbacks.micIsAudioManagerInitializedCallbackFunction = [this]() { return mAudioManager.isInitialized(); };
  rendererMICCallbacks.micPlayRandomMusicCallbackFunction = [this]() { mAudioManager.playRandomMusic(); };
  rendererMICCallbacks.micStopMusicCallbackFunction = [this]() { mAudioManager.stopMusic(); };
  rendererMICCallbacks.micPauseResumeMusicCallbackFunction = [this](bool pauseOrResume) { mAudioManager.pauseMusic(pauseOrResume); };
  rendererMICCallbacks.micGetMusicPlayListCallbackFunction = [this]() { return mAudioManager.getPlayList(); };
  rendererMICCallbacks.micIsMusicPausedCallbackFunction = [this]() { return mAudioManager.isMusicPaused(); };
  rendererMICCallbacks.micIsMusicPlayingCallbackFunction = [this]() { return mAudioManager.isMusicPlaying(); };
  rendererMICCallbacks.micGetMusicCurrentTrackCallbackFunction = [this]() { return mAudioManager.getCurrentTitle(); };
  rendererMICCallbacks.micPlayNextMusicTrackCallbackFunction = [this]() { mAudioManager.playNextTitle(); };
  rendererMICCallbacks.micPlayPrevMusicTrackCallbackFunction = [this]() { mAudioManager.playPrevTitle(); };
  rendererMICCallbacks.micSetMusicVolumeCallbackFunction = [this](int volume) { mAudioManager.setMusicVolume(volume); };
  rendererMICCallbacks.micGetMusicVolumeCallbackFunction = [this]() { return mAudioManager.getMusicVolume(); };
  rendererMICCallbacks.micPlayMusicTitleCallbackFunction = [this](std::string trackTitle) { mAudioManager.playTitle(trackTitle); };
  rendererMICCallbacks.micSetSoundEffectsVolumeCallbackFunction = [this](int volume) { mAudioManager.setSoundVolume(volume); };
  rendererMICCallbacks.micGetSoundEffectsVolumeCallbackFunction = [this]() { return mAudioManager.getSoundVolume(); };
  rendererMICCallbacks.micPlayWalkFootstepCallbackFunction = [this]() { mAudioManager.playWalkFootsteps(); };
  rendererMICCallbacks.micPlayRunFootstepCallbackFunction = [this]() { mAudioManager.playRunFootsteps(); };
  rendererMICCallbacks.micStopFootstepCallbackFunction = [this]() { mAudioManager.stopFootsteps(); };
#endif

#if defined(__ANDROID__)
  if (!mVRHeadset.init(nullptr, rendererMICCallbacks)) {
    Logger::log(1, "%s error: Could not init VR Headset\n", __FUNCTION__);
    return false;
  }
#else
  if (!mVRHeadset.init(mWindow, rendererMICCallbacks)) {
    glfwTerminate();
    Logger::log(1, "%s error: Could not init VR Headset\n", __FUNCTION__);
    return false;
  }
#endif

  std::shared_ptr<VkRenderer> renderer = mVRHeadset.getVulkanRenderer();

#if !defined(__ANDROID__)
  glfwSetWindowUserPointer(mWindow, renderer.get());
  glfwSetFramebufferSizeCallback(mWindow, [](GLFWwindow *win, int winWidth, int winHeight) {
      auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      winRenderer->setSize(winWidth, winHeight);
    }
  );

  glfwSetKeyCallback(mWindow, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
      auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      winRenderer->handleKeyEvents(key, scancode, action, mods);
    }
  );

  glfwSetMouseButtonCallback(mWindow, [](GLFWwindow *win, int button, int action, int mods) {
      auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      winRenderer->handleMouseButtonEvents(button, action, mods);
    }
  );

  glfwSetCursorPosCallback(mWindow, [](GLFWwindow *win, double xpos, double ypos) {
      auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      winRenderer->handleMousePositionEvents(xpos, ypos);
    }
  );

  glfwSetScrollCallback(mWindow, [](GLFWwindow *win, double xOffset, double yOffset) {
    auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
    winRenderer->handleMouseWheelEvents(xOffset, yOffset);
    }
  );

  glfwSetWindowCloseCallback(mWindow, [](GLFWwindow *win) {
      auto winRenderer = static_cast<VkRenderer*>(glfwGetWindowUserPointer(win));
      winRenderer->requestExitApplication();
    }
  );

  if (!mAudioManager.init()) {
    Logger::log(1, "%s error: unable to init audio, skipping\n", __FUNCTION__);
  }

  if (mAudioManager.isInitialized()) {
    if (!mAudioManager.loadMusicFromFolder("assets/music", "mp3")) {
      Logger::log(1, "%s warning: not MP3 tracks found, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadMusicFromFolder("assets/music", "ogg")) {
      Logger::log(1, "%s warning: not OGG tracks found, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadWalkFootsteps("assets/sounds/Fantozzi-SandL1.wav")) {
      Logger::log(1, "%s warning: could not load walk footsteps, skipping\n", __FUNCTION__);
    }
    if (!mAudioManager.loadRunFootsteps("assets/sounds/Fantozzi-SandR3.wav")) {
      Logger::log(1, "%s warning: could not load run footsteps, skipping\n", __FUNCTION__);
    }
  }
#endif

  mInitialized = true;
  Logger::log(1, "%s: Window with Vulkan successfully initialized\n", __FUNCTION__);
  return true;
}

static float gDeltaTime = 0.0f;

void AppWindow::mainLoop() {
#if !defined(__ANDROID__)
  glfwSwapInterval(1);
#endif

  std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
  std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();

  while (mRunning) {
#if defined(__ANDROID__)
    mainLoopOnce();
#else
    if (mVRHeadset.isXRApplicationRunning()) {
      mVRHeadset.pollEvents();

      if (mVRHeadset.isXRSessionRunning()) {
        if (!mVRHeadset.draw(gDeltaTime)) {
          break;
        }
      }
    }

    glfwPollEvents();

    loopEndTime = std::chrono::steady_clock::now();
    gDeltaTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime).count() / 1'000'000.0f;
    loopStartTime = loopEndTime;
#endif
  }
}

#if defined(__ANDROID__)
void AppWindow::mainLoopOnce() {
  static std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();

  if (mVRHeadset.isXRApplicationRunning()) {
    mVRHeadset.pollEvents();

    if (mVRHeadset.isXRSessionRunning()) {
      if (!mVRHeadset.draw(gDeltaTime)) {
        mRunning = false;
        return;
      }
    }
  }

  auto loopEndTime = std::chrono::steady_clock::now();
  gDeltaTime = std::chrono::duration_cast<std::chrono::microseconds>(loopEndTime - loopStartTime).count() / 1'000'000.0f;
  loopStartTime = loopEndTime;
}
#endif

void AppWindow::cleanup() {
#if !defined(__ANDROID__)
  mAudioManager.cleanup();
#endif
  mVRHeadset.cleanup();

#if !defined(__ANDROID__)
  glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
  glfwDestroyWindow(mWindow);
  glfwTerminate();
#endif

  mInitialized = false;
  Logger::log(1, "%s: Terminating Window\n", __FUNCTION__);
}

std::string AppWindow::getWindowTitle() {
  return mWindowTitle;
}

void AppWindow::setWindowTitle(std::string newTitle) {
  mWindowTitle = newTitle;
#if !defined(__ANDROID__)
  glfwSetWindowTitle(mWindow, mWindowTitle.c_str());
#endif
}

#if !defined(__ANDROID__)
GLFWwindow* AppWindow::getWindow() {
  return mWindow;
}
#endif