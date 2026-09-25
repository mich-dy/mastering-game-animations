#pragma once

#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <shared_mutex>

class TimerFunc {
  public:
    TimerFunc(unsigned int ticksPerSecond);
    TimerFunc();

    void registerFunction(std::function<void()> func);
    void cleanupFunctions();

    void callExtFunctionLocked(std::function<void()> func);

    void startTimer();
    void stopTimer();

  private:
    static const unsigned int DEFAULT_TICKS_PER_SECOND = 60;
    unsigned int calculateTickInverse(unsigned int ticksPerSecond);

    void timerFunction();
    std::thread mTimerThread;

    std::atomic<bool> mTimerRunning = false;
    unsigned int mTickInverse = calculateTickInverse(DEFAULT_TICKS_PER_SECOND);

    std::vector<std::function<void(void)>> mFunctions {};
    std::shared_mutex mFuncMutex;
};
