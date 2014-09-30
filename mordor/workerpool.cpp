// Copyright (c) 2009 - Mozy, Inc.

#include <linux/futex.h>

#include "workerpool.h"
#include "fiber.h"
#include "log.h"

namespace Mordor {

static Logger::ptr g_log = Log::lookup("mordor:workerpool");

WorkerPool::WorkerPool(size_t threads, bool useCaller, size_t batchSize)
    : Scheduler(threads, useCaller, batchSize)
{
    start();
}

void
WorkerPool::idle(const std::atomic<unsigned>& ec)
{
    while (stopping()) {
        int* uaddr = reinterpret_cast<int*>(&ec);
        sys_futex(uaddr, FUTEX_WAIT, 1, nullptr, nullptr, 0);

        try {
            Fiber::yield();
        } catch (OperationAbortedException &) {
            return;
        }
    }
}

void
WorkerPool::tickle(const std::atomic<unsigned>& ec)
{
    int* uaddr = reinterpret_cast<int*>(&ec);

    MORDOR_LOG_DEBUG(g_log) << this << " tickling";
    sys_futex(uaddr, FUTEX_WAKE, 1, nullptr, nullptr, 0);
}

}
