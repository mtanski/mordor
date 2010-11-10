#ifndef __MORDOR_TIMERSCHEDULER_H__
#define __MORDOR_TIMERSCHEDULER_H__
// Copyright (c) 2010 - Mozy, Inc.

#include "scheduler.h"
#include "timer.h"

namespace Mordor {

/// Base class for Schedulers that implement TimerManager
class TimerScheduler : public Scheduler, public TimerManager
{
public:
    TimerScheduler(size_t threads = 1, bool useCaller = true)
        : Scheduler(threads, useCaller)
    {}

    bool stopping();

protected:
    void onTimerInsertedAtFront() { tickle(); }
    bool stopping(unsigned long long &nextTimeout);
    /// To be overridden to allow the derived class to prevent stopping
    virtual bool stoppingInternal() { return true; }
};

}

#endif
