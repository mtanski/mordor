// Copyright (c) 2010 - Mozy, Inc.


#include "mordor/test/test.h"
#include "mordor/thread.h"
#include "mordor/workerpool.h"

using namespace Mordor;

static void threadFunc(std::thread::id &myTid)
{
    myTid = std::this_thread::get_id();
}

// Tests to make sure the tid as reported by Thread object and inside the
// thread itself are the same (and have a valid value)
MORDOR_UNITTEST(Thread, correctTID)
{
    std::thread::id tid;
    Thread t(std::bind(&threadFunc, std::ref(tid)), "my thread");
    t.join();
    MORDOR_TEST_ASSERT_NOT_EQUAL(tid, std::thread::id());
    MORDOR_TEST_ASSERT_EQUAL(tid, t.tid());
}

MORDOR_UNITTEST(Thread, bookMark)
{
    WorkerPool poolA(1, true), poolB(1, false);
    std::thread::id mainTid = std::this_thread::get_id();
    Thread::Bookmark mark;
    MORDOR_TEST_ASSERT_EQUAL(mark.tid(), mainTid);
    poolB.switchTo();
    std::thread::id tidB = std::this_thread::get_id();
    MORDOR_TEST_ASSERT_NOT_EQUAL(tidB, mainTid);
    mark.switchTo();
    std::thread::id current = std::this_thread::get_id();
    MORDOR_TEST_ASSERT_EQUAL(current, mainTid);
}

namespace {

struct DummyException : public virtual Mordor::Exception {};

static void doSomething(WorkerPool &pool)
{
    pool.switchTo();
}

static void rethrowException(WorkerPool &poolA, WorkerPool &poolB)
{
    std::thread::id mainTid;
    try {
        // execute in poolA
        poolA.switchTo();
        mainTid = std::this_thread::get_id();
        MORDOR_THROW_EXCEPTION(DummyException());
    } catch (...) {
        // still in poolA
        MORDOR_TEST_ASSERT_EQUAL(std::this_thread::get_id(), mainTid);
        // do something that might switch or not switch thread
        // in this test case, it does switch to a different thread
        doSomething(poolB);
        MORDOR_TEST_ASSERT_NOT_EQUAL(mainTid, std::this_thread::get_id());
        // switch back
        // rethrow the exception without any issue
        // if we do not handle exception stack, this `throw' can't be caught any
        // more, and C++ runtime terminates the program.
        throw;
    }
}
}

MORDOR_UNITTEST(Thread, exceptionRethrow)
{
    WorkerPool poolA(1, true), poolB(1, false);
    MORDOR_TEST_ASSERT_EXCEPTION(rethrowException(poolA, poolB), DummyException);
    poolA.switchTo();
}
