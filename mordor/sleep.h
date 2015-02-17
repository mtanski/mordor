#ifndef __MORDOR_SLEEP_H__
#define __MORDOR_SLEEP_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <chrono>

namespace Mordor {

class TimerManager;

/// Suspend execution of the current thread
/// @note This is a normal sleep, and will block the current thread
/// @param us How long to sleep, in microseconds
void sleep(unsigned long long us);

template <class Rep, class Period>
inline void sleep(std::chrono::duration<Rep, Period> duration)
{
    auto rescaled = std::chrono::duration_cast<std::chrono::microseconds>(
        duration);
    sleep(rescaled.count());
}

/// Suspend execution of the current Fiber
/// @note This will use the TimerManager to yield the current Fiber and allow
/// other Fibers to run until this Fiber is ready to run again.
/// @param timerManager The TimerManager (typically an IOManager) to use to
/// to control this sleep
/// @param us How long to sleep, in microseconds
/// @pre Scheduler::getThis() != NULL
void sleep(TimerManager &timerManager, unsigned long long us);

template <class Rep, class Period>
inline void sleep(TimerManager & timerManager,
                  std::chrono::duration<Rep, Period> duration)
{
    auto rescaled = std::chrono::duration_cast<std::chrono::microseconds>(
        duration);
    sleep(timerManager, rescaled.count());
}

}

#endif
