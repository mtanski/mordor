// Copyright (c) 2010 - Mozy, Inc.

#include "timerscheduler.h"

namespace Mordor {

bool
TimerScheduler::stopping()
{
    unsigned long long nextTimeout;
    return stopping(nextTimeout);
}

bool
TimerScheduler::stopping(unsigned long long &nextTimeout)
{
    nextTimeout = nextTimer();
    return nextTimeout == ~0ull && stoppingInternal() && Scheduler::stopping();
}

}
