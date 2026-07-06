// simple Logger class
#pragma once
#include <cstdio>

#if defined(__ANDROID__)
#include <android/log.h>
#include <cstdarg>
#endif

class Logger {
  public:
    // log if input log level is equal or smaller to log level set
    template <typename... Args>
    static void log(unsigned int logLevel, Args ... args) {
      if (logLevel <= mLogLevel) {
#if defined(__ANDROID__)
        char buffer[2048];
        std::snprintf(buffer, sizeof(buffer), args ...);
        __android_log_write(ANDROID_LOG_INFO, "MasteringAnim", buffer);
#else
        std::printf(args ...);
        std::fflush(stdout);
#endif
      }
    }

    static void setLogLevel(unsigned int inLogLevel) {
      inLogLevel <= 9 ? mLogLevel = inLogLevel : mLogLevel = 9;
    }

  private:
    static unsigned int mLogLevel;
};
