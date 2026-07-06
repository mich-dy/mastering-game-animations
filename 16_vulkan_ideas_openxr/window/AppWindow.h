#pragma once
#include <string>
#include <memory>
#include <chrono>

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

#include <VRHeadset.h>
#include <Callbacks.h>

#if !defined(__ANDROID__)
#include <AudioManager.h>
#endif

class AppWindow {
  public:
    bool init(unsigned int width, unsigned int height, std::string title);
    void mainLoop();
#if defined(__ANDROID__)
    void mainLoopOnce();
    bool isInitialized() const { return mInitialized; }
    bool isRunning() const { return mRunning; }
#endif
    void cleanup();

#if !defined(__ANDROID__)
    GLFWwindow* getWindow();
#endif
    std::string getWindowTitle();
    void setWindowTitle(std::string newTitle);

  private:
#if !defined(__ANDROID__)
    GLFWwindow *mWindow = nullptr;
#endif

    std::string mWindowTitle;

    VRHeadset mVRHeadset{};

#if !defined(__ANDROID__)
    AudioManager mAudioManager{};
#endif

    bool mInitialized = false;
    bool mRunning = true;
};
