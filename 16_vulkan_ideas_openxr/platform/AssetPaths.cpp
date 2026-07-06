#include <Platform.h>

#include <Logger.h>

#if defined(__ANDROID__)

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android_native_app_glue.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

static android_app *gAndroidApp = nullptr;
static ANativeActivity *gActivity = nullptr;
static std::string gAssetRoot;
static bool gAssetsPrepared = false;
static constexpr const char *kAssetBundleVersion = "5";

void Platform::setAndroidApp(android_app *app) {
  gAndroidApp = app;
}

android_app *Platform::getAndroidApp() {
  return gAndroidApp;
}

void Platform::setActivity(ANativeActivity *activity) {
  gActivity = activity;
}

ANativeActivity *Platform::getActivity() {
  return gActivity;
}

std::string Platform::getAssetRoot() {
  return gAssetRoot;
}

static bool copyAssetFile(AAssetManager *assetManager, const std::string &assetPath, const std::string &destPath) {
  AAsset *asset = AAssetManager_open(assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
  if (!asset) {
    Logger::log(1, "%s error: could not open asset %s\n", __FUNCTION__, assetPath.c_str());
    return false;
  }

  const void *buffer = AAsset_getBuffer(asset);
  const off_t length = AAsset_getLength(asset);
  if (!buffer || length <= 0) {
    AAsset_close(asset);
    Logger::log(1, "%s error: asset %s is empty\n", __FUNCTION__, assetPath.c_str());
    return false;
  }

  std::ofstream outFile(destPath, std::ios::binary);
  if (!outFile.is_open()) {
    AAsset_close(asset);
    Logger::log(1, "%s error: could not write %s\n", __FUNCTION__, destPath.c_str());
    return false;
  }

  outFile.write(static_cast<const char *>(buffer), length);
  outFile.close();
  AAsset_close(asset);
  return true;
}

static bool copyAssetDir(AAssetManager *assetManager, const std::string &assetDir, const std::string &destDir) {
  AAssetDir *dir = AAssetManager_openDir(assetManager, assetDir.c_str());
  if (!dir) {
    Logger::log(1, "%s error: could not open asset dir %s\n", __FUNCTION__, assetDir.c_str());
    return false;
  }

  std::filesystem::create_directories(destDir);

  size_t extractedCount = 0;
  const char *fileName = nullptr;
  while ((fileName = AAssetDir_getNextFileName(dir)) != nullptr) {
    ++extractedCount;
    const std::string childAssetPath = assetDir.empty() ? fileName : assetDir + "/" + fileName;
    const std::string childDestPath = destDir + "/" + fileName;

    AAsset *childAsset = AAssetManager_open(assetManager, childAssetPath.c_str(), AASSET_MODE_UNKNOWN);
    if (childAsset != nullptr) {
      AAsset_close(childAsset);
      if (!copyAssetFile(assetManager, childAssetPath, childDestPath)) {
        AAssetDir_close(dir);
        return false;
      }
      continue;
    }

    AAssetDir *childDir = AAssetManager_openDir(assetManager, childAssetPath.c_str());
    if (childDir != nullptr) {
      if (!copyAssetDir(assetManager, childAssetPath, childDestPath)) {
        AAssetDir_close(childDir);
        AAssetDir_close(dir);
        return false;
      }
      AAssetDir_close(childDir);
      continue;
    }

    Logger::log(1, "%s error: could not open asset %s as file or directory\n", __FUNCTION__, childAssetPath.c_str());
    AAssetDir_close(dir);
    return false;
  }

  AAssetDir_close(dir);
  Logger::log(1, "%s: extracted %zu entries from asset dir '%s' into '%s'\n",
    __FUNCTION__, extractedCount, assetDir.c_str(), destDir.c_str());
  return extractedCount > 0 || assetDir.empty();
}

bool Platform::prepareAssetRoot() {
  if (!gAndroidApp || !gAndroidApp->activity || !gAndroidApp->activity->assetManager) {
    Logger::log(1, "%s error: Android asset manager unavailable\n", __FUNCTION__);
    return false;
  }

  gAssetRoot = std::string(gAndroidApp->activity->internalDataPath) + "/game_data";
  const std::string versionMarker = gAssetRoot + "/.asset_version";
  std::string installedVersion;
  {
    std::ifstream versionIn(versionMarker);
    if (versionIn.is_open()) {
      std::getline(versionIn, installedVersion);
    }
  }

  if (gAssetsPrepared && installedVersion == kAssetBundleVersion) {
    if (chdir(gAssetRoot.c_str()) != 0) {
      Logger::log(1, "%s error: could not chdir to %s\n", __FUNCTION__, gAssetRoot.c_str());
      return false;
    }
    return true;
  }

  if (std::filesystem::exists(gAssetRoot)) {
    std::filesystem::remove_all(gAssetRoot);
  }
  std::filesystem::create_directories(gAssetRoot);

  AAssetManager *assetManager = gAndroidApp->activity->assetManager;
  const std::vector<std::pair<std::string, std::string>> assetDirs = {
    {"shader", "shader"},
    {"textures", "textures"},
    {"game_assets/level", "assets/level"},
    {"game_assets/lightbulb", "assets/lightbulb"},
    {"game_assets/man", "assets/man"},
    {"game_assets/music", "assets/music"},
    {"game_assets/sounds", "assets/sounds"},
    {"game_assets/vr-controller", "assets/vr-controller"},
    {"game_assets/waypointmarker", "assets/waypointmarker"},
    {"game_assets/woman", "assets/woman"},
    {"config", "config"},
  };

  for (const auto &[apkDir, diskDir] : assetDirs) {
    if (!copyAssetDir(assetManager, apkDir, gAssetRoot + "/" + diskDir)) {
      Logger::log(1, "%s error: failed to extract %s\n", __FUNCTION__, apkDir.c_str());
      return false;
    }
  }

  if (!copyAssetFile(assetManager, "controls.txt", gAssetRoot + "/controls.txt")) {
    Logger::log(1, "%s warning: controls.txt not found in APK assets\n", __FUNCTION__);
  }

  if (chdir(gAssetRoot.c_str()) != 0) {
    Logger::log(1, "%s error: could not chdir to %s\n", __FUNCTION__, gAssetRoot.c_str());
    return false;
  }

  {
    std::ofstream versionOut(versionMarker);
    if (versionOut.is_open()) {
      versionOut << kAssetBundleVersion;
    }
  }

  gAssetsPrepared = true;
  Logger::log(1, "%s: asset root ready at %s\n", __FUNCTION__, gAssetRoot.c_str());
  return true;
}

#endif