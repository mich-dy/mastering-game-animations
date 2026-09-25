#include <TimerFunc.h>

#include <condition_variable>
#include <chrono>
#include <Logger.h>

TimerFunc::TimerFunc(unsigned int ticksPerSecond) {
  mTickInverse = calculateTickInverse(ticksPerSecond);
}

TimerFunc::TimerFunc() : TimerFunc(DEFAULT_TICKS_PER_SECOND) {}

void TimerFunc::timerFunction() {
  while (mTimerRunning) {
    auto destTime = std::chrono::steady_clock::now() + std::chrono::microseconds(mTickInverse);

    if (!mFunctions.empty()) {
      std::unique_lock<std::shared_mutex> timerLock(mFuncMutex);
      for (const auto& func : mFunctions) {
        if (func) {
          func();
        }
      }
    }
    std::this_thread::sleep_until(destTime);
  }
}

void TimerFunc::callExtFunctionLocked(std::function<void()> func) {
  std::unique_lock<std::shared_mutex> timerLock(mFuncMutex);
  if (func) {
    func();
  }
}

void TimerFunc::registerFunction(std::function<void()> func) {
  std::unique_lock<std::shared_mutex> timerLock(mFuncMutex);
  mFunctions.emplace_back(func);
}

void TimerFunc::cleanupFunctions() {
  std::unique_lock<std::shared_mutex> timerLock(mFuncMutex);
  mFunctions.clear();
}

unsigned int TimerFunc::calculateTickInverse(unsigned int ticksPerSecond) {
  unsigned int tickInverse = (1'000'000.0 / ticksPerSecond);
  return tickInverse;
}

void TimerFunc::startTimer() {
  if (mTimerRunning) {
    Logger::log(1, "%s error: timer already running\n", __FUNCTION__);
    return;
  }

  if (mFunctions.empty()) {
    Logger::log(1, "%s error: no functons were registered\n", __FUNCTION__);
    return;
  }

  mTimerRunning = true;
  mTimerThread = std::thread(&TimerFunc::timerFunction, this);

  Logger::log(1, "%s: timer thread started\n", __FUNCTION__);
}

void TimerFunc::stopTimer() {
  if (!mTimerRunning) {
    Logger::log(1, "%s error: timer was not runnning\n", __FUNCTION__);
    return;
  }

  mTimerRunning = false;
  mTimerThread.join();

  Logger::log(1, "%s: timer stopped\n", __FUNCTION__);
}
