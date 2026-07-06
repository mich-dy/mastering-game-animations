#pragma once

#include <string>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/native_activity.h>
struct android_app;
struct ANativeActivity;
#endif

class Platform {
  public:
#if defined(__ANDROID__)
    static void setAndroidApp(android_app *app);
    static android_app *getAndroidApp();
    static void setActivity(ANativeActivity *activity);
    static ANativeActivity *getActivity();
    static bool prepareAssetRoot();
    static std::string getAssetRoot();
    static bool isAndroidXR() { return true; }
#else
    static bool isAndroidXR() { return false; }
#endif
};