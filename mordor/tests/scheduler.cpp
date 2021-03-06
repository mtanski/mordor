// Copyright (c) 2009 - Mozy, Inc.

#include <atomic>
#include <boost/thread/mutex.hpp>

#include "../fiber.h"
#include "../iomanager.h"
#include "../parallel.h"
#include "../sleep.h"
#include "../test/test.h"
#include "../workerpool.h"
#include "../util.h"

using namespace Mordor;
using namespace Mordor::Test;

MORDOR_SUITE_INVARIANT(Scheduler)
{
    MORDOR_TEST_ASSERT(!Scheduler::getThis());
}


namespace {
    static void doNothing() { }

    void throwException() { throw Exception(); }

    void runOrException(int &i, int expectedValue, bool throwException)
    {
        MORDOR_LOG_DEBUG(::Mordor::Log::root()) << "set value: " << expectedValue;
        if (throwException)
            throw Exception();
        else
            i = expectedValue;
    }

}

// Stop can be called multiple times without consequence
MORDOR_UNITTEST(Scheduler, idempotentStopHijack)
{
    WorkerPool pool;
    pool.stop();
    pool.stop();
}

MORDOR_UNITTEST(Scheduler, idempotentStopHybrid)
{
    WorkerPool pool(2);
    pool.stop();
    pool.stop();
}

MORDOR_UNITTEST(Scheduler, idempotentStopSpawn)
{
    WorkerPool pool(1, false);
    pool.stop();
    pool.stop();
}

// Start can be called multiple times without consequence
MORDOR_UNITTEST(Scheduler, idempotentStartHijack)
{
    WorkerPool pool;
    pool.start();
    pool.start();
}

MORDOR_UNITTEST(Scheduler, idempotentStartHybrid)
{
    WorkerPool pool(2);
    pool.start();
    pool.start();
}

MORDOR_UNITTEST(Scheduler, idempotentStartSpawn)
{
    WorkerPool pool(1, false);
    pool.start();
    pool.start();
}

// When hijacking the calling thread, you can stop() from anywhere within
// it
MORDOR_UNITTEST(Scheduler, stopScheduledHijack)
{
    WorkerPool pool;
    pool.schedule(std::bind(&Scheduler::stop, &pool));
    pool.dispatch();
}

MORDOR_UNITTEST(Scheduler, stopScheduledHybrid)
{
    WorkerPool pool(2);
    pool.schedule(std::bind(&Scheduler::stop, &pool));
    pool.yieldTo();
}

// When hijacking the calling thread, you don't need to explicitly start
// or stop the scheduler; it starts on the first yieldTo, and stops on
// destruction
MORDOR_UNITTEST(Scheduler, hijackBasic)
{
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &pool);
    pool.schedule(doNothingFiber);
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::INIT);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::TERM);
}

// Similar to above, but after the scheduler has stopped, yielding
// to it again should implicitly restart it
MORDOR_UNITTEST(Scheduler, hijackMultipleDispatch)
{
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &pool);
    pool.schedule(doNothingFiber);
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::INIT);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::TERM);
    doNothingFiber->reset(&doNothing);
    pool.schedule(doNothingFiber);
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::INIT);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::TERM);
}

// Just calling stop should still clear all pending work
MORDOR_UNITTEST(Scheduler, hijackStopOnScheduled)
{
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool;
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &pool);
    pool.schedule(doNothingFiber);
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::INIT);
    pool.stop();
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::TERM);
}

// TODO: could improve this test by having two fibers that
// synchronize and MORDOR_ASSERT( that they are on different threads
MORDOR_UNITTEST(Scheduler, hybridBasic)
{
    Fiber::ptr doNothingFiber(new Fiber(&doNothing));
    WorkerPool pool(2);
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &pool);
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::INIT);
    pool.schedule(doNothingFiber);
    Scheduler::yield();
    pool.stop();
    MORDOR_TEST_ASSERT_EQUAL(doNothingFiber->state(), Fiber::TERM);
}

void
otherThreadProc(Scheduler *scheduler, bool &done)
{
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), scheduler);
    done = true;
}

MORDOR_UNITTEST(Scheduler, spawnBasic)
{
    bool done = false;
    WorkerPool pool(1, false);
    Fiber::ptr f(new Fiber(
        std::bind(&otherThreadProc, &pool, std::ref(done))));
    MORDOR_TEST_ASSERT(!Scheduler::getThis());
    MORDOR_TEST_ASSERT_EQUAL(f->state(), Fiber::INIT);
    MORDOR_TEST_ASSERT(!done);
    pool.schedule(f);
    volatile bool &doneVolatile = done;
    while (!doneVolatile);
    pool.stop();
    MORDOR_TEST_ASSERT_EQUAL(f->state(), Fiber::TERM);
}

MORDOR_UNITTEST(Scheduler, switchToStress)
{
    WorkerPool poolA(1, true), poolB(1, false);

    // Ensure we return to poolA
    SchedulerSwitcher switcher;
    for (int i = 0; i < 1000; ++i) {
        if (i % 2) {
            poolA.switchTo();
            MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &poolA);
        } else {
            poolB.switchTo();
            MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &poolB);
        }
    }
}

void
runInContext(Scheduler &poolA, Scheduler &poolB)
{
    SchedulerSwitcher switcher(&poolB);
    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &poolB);
    MORDOR_TEST_ASSERT_NOT_EQUAL(Scheduler::getThis(), &poolA);
    MORDOR_THROW_EXCEPTION(OperationAbortedException());
}

MORDOR_UNITTEST(Scheduler, switcherExceptions)
{
    WorkerPool poolA(1, true), poolB(1, false);

    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &poolA);
    MORDOR_TEST_ASSERT_NOT_EQUAL(Scheduler::getThis(), &poolB);

    MORDOR_TEST_ASSERT_EXCEPTION(runInContext(poolA, poolB), OperationAbortedException);

    MORDOR_TEST_ASSERT_EQUAL(Scheduler::getThis(), &poolA);
    MORDOR_TEST_ASSERT_NOT_EQUAL(Scheduler::getThis(), &poolB);
}

static void increment(int &total)
{
    ++total;
}

MORDOR_UNITTEST(Scheduler, parallelDo)
{
    WorkerPool pool;

    int total = 0;
    std::vector<std::function<void ()> > dgs;
    dgs.push_back(std::bind(&increment, std::ref(total)));
    dgs.push_back(std::bind(&increment, std::ref(total)));

    parallel_do(dgs);
    MORDOR_TEST_ASSERT_EQUAL(total, 2);
}

MORDOR_UNITTEST(Scheduler, parallelDoFibersDone)
{
    WorkerPool pool(8u);

    int total = 0;
    std::vector<std::function<void ()> > dgs;
    std::vector<Fiber::ptr> fibers;
    std::function<void ()> dg = std::bind(&increment, std::ref(total));
    for (int i = 0; i < 8; ++i) {
        dgs.push_back(dg);
        fibers.push_back(Fiber::ptr(new Fiber(NULL)));
    }

    for (int i = 0; i < 5000; ++i) {
        parallel_do(dgs, fibers);
        for (size_t j = 0; j < dgs.size(); ++j)
            // This should not assert about the fiber not being terminated
            fibers[j]->reset(dg);
    }
}

static void exception()
{
    MORDOR_THROW_EXCEPTION(OperationAbortedException());
}

MORDOR_UNITTEST(Scheduler, parallelDoException)
{
    WorkerPool pool;

    std::vector<std::function<void ()> > dgs;
    dgs.push_back(&exception);
    dgs.push_back(&exception);

    MORDOR_TEST_ASSERT_EXCEPTION(parallel_do(dgs), OperationAbortedException);
}

static void checkEqual(int x, int &sequence)
{
    MORDOR_TEST_ASSERT_EQUAL(x, sequence);
    ++sequence;
}

MORDOR_UNITTEST(Scheduler, parallelForEach)
{
    const int values[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    WorkerPool pool;

    int sequence = 1;
    parallel_foreach(&values[0], &values[10], std::bind(
        &checkEqual, std::placeholders::_1, std::ref(sequence)), 4);
    MORDOR_TEST_ASSERT_EQUAL(sequence, 11);
}

MORDOR_UNITTEST(Scheduler, parallelForEachLessThanParallelism)
{
    const int values[] = { 1, 2 };
    WorkerPool pool;

    int sequence = 1;
    parallel_foreach(&values[0], &values[2], std::bind(
        &checkEqual, std::placeholders::_1, std::ref(sequence)), 4);
    MORDOR_TEST_ASSERT_EQUAL(sequence, 3);
}

static void checkEqualStop5(int x, int &sequence, bool expectOrdered)
{
    if (expectOrdered)
        MORDOR_TEST_ASSERT_EQUAL(x, sequence);
    if (++sequence >= 5)
        MORDOR_THROW_EXCEPTION(OperationAbortedException());
}

MORDOR_UNITTEST(Scheduler, parallelForEachStopShort)
{
    const int values[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    WorkerPool pool;

    int sequence = 1;
    MORDOR_TEST_ASSERT_EXCEPTION(
    parallel_foreach(&values[0], &values[10], std::bind(
        &checkEqualStop5, std::placeholders::_1, std::ref(sequence), true), 4),
        OperationAbortedException);
    // 5 <= sequence < 10 (we told it to stop at five, it's undefined how many
    // more got executed, because of other threads (on a single thread it's
    // deterministically 5))
    MORDOR_TEST_ASSERT_EQUAL(sequence, 5);
}

MORDOR_UNITTEST(Scheduler, parallelForEachStopShortParallel)
{
    const int values[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    WorkerPool pool(2);

    int sequence = 1;
    MORDOR_TEST_ASSERT_EXCEPTION(
    parallel_foreach(&values[0], &values[10], std::bind(
        &checkEqualStop5, std::placeholders::_1, std::ref(sequence), false), 4),
        OperationAbortedException);
    // 5 <= sequence < 10 (we told it to stop at five, it's undefined how many
    // more got executed, because of other threads (on a single thread it's
    // deterministically 5))
    MORDOR_TEST_ASSERT_GREATER_THAN_OR_EQUAL(sequence, 5);
    MORDOR_TEST_ASSERT_LESS_THAN(sequence, 10);
}

// #ifndef NDEBUG
// MORDOR_UNITTEST(Scheduler, scheduleForThreadNotOnScheduler)
// {
//     Fiber::ptr doNothingFiber(new Fiber(&doNothing));
//     WorkerPool pool(1, false);
//     MORDOR_TEST_ASSERT_ASSERTED(pool.schedule(doNothingFiber, std::this_thread::get_id()));
//     pool.stop();
// }
// #endif

static void sleepForABit(std::set<std::thread::id> &threads,
    boost::mutex &mutex, Fiber::ptr scheduleMe, std::atomic<int> *count = nullptr)
{
    {
        boost::mutex::scoped_lock lock(mutex);
        threads.insert(std::this_thread::get_id());
    }
    Mordor::sleep(10000);
    if (count && --(*count) == 0)
        Scheduler::getThis()->schedule(scheduleMe);
}

MORDOR_UNITTEST(Scheduler, spreadTheLoad)
{
    std::set<std::thread::id> threads;
    {
        boost::mutex mutex;
        WorkerPool pool(8);
        // Wait for the other threads to get to idle first
        Mordor::sleep(100000);
	std::atomic<int> count(24);
        for (size_t i = 0; i < 24; ++i)
            pool.schedule(std::bind(&sleepForABit, std::ref(threads),
                std::ref(mutex), Fiber::getThis(), &count));
        // We have to have one of these fibers reschedule us, because if we
        // let the pool destruct, it will call stop which will wake up all
        // the threads
        Scheduler::yieldTo();
    }
    // Make sure we hit every thread
    MORDOR_TEST_ASSERT_ABOUT_EQUAL(threads.size(), 8u, 2u);
}

static void fail()
{
    MORDOR_NOTREACHED();
}

static void cancelTheTimer(Timer::ptr timer)
{
    // Wait for the other threads to get to idle first
    Mordor::sleep(100000);
    timer->cancel();
}

MORDOR_UNITTEST(Scheduler, stopIdleMultithreaded)
{
    IOManager ioManager(4);
    unsigned long long start = TimerManager::now();
    Timer::ptr timer = ioManager.registerTimer(10000000ull, &fail);
    // Wait for the other threads to get to idle first
    Mordor::sleep(100000);
    ioManager.schedule(std::bind(&cancelTheTimer, timer));
    ioManager.stop();
    // This should have taken less than a second, since we cancelled the timer
    MORDOR_TEST_ASSERT_LESS_THAN(TimerManager::now() - start, 1000000ull);
}

static void startTheFibers(std::set<std::thread::id> &threads,
    boost::mutex &mutex)
{
    Mordor::sleep(100000);
    for (size_t i = 0; i < 24; ++i)
        Scheduler::getThis()->schedule(std::bind(&sleepForABit,
            std::ref(threads), std::ref(mutex), Fiber::ptr(),
            nullptr));
}

MORDOR_UNITTEST(Scheduler, spreadTheLoadWhileStopping)
{
    std::set<std::thread::id> threads;
    {
        boost::mutex mutex;
        WorkerPool pool(8);
        // Wait for the other threads to get to idle first
        Mordor::sleep(100000);

        pool.schedule(std::bind(&startTheFibers, std::ref(threads),
            std::ref(mutex)));
        pool.stop();
    }
    // Make sure we hit every thread
    MORDOR_TEST_ASSERT_ABOUT_EQUAL(threads.size(), 8u, 2u);
}

MORDOR_UNITTEST(Scheduler, tolerantException)
{
    WorkerPool pool;
    pool.schedule(throwException);
    MORDOR_TEST_ASSERT_ANY_EXCEPTION(pool.stop());
}

MORDOR_UNITTEST(Scheduler, tolerantExceptionInBatch)
{
    WorkerPool pool(1, true, 10); // batchSize set to 10
    std::vector<int> values(3);
    std::vector<std::function<void ()> > dgs;
    dgs.push_back(std::bind(runOrException, std::ref(values[0]), 1, false));
    dgs.push_back(std::bind(runOrException, std::ref(values[1]), 2, true));
    dgs.push_back(std::bind(runOrException, std::ref(values[2]), 3, false));
    pool.schedule(dgs.begin(), dgs.end());

    MORDOR_TEST_ASSERT_EQUAL(values[0], 0);
    MORDOR_TEST_ASSERT_EQUAL(values[1], 0);
    MORDOR_TEST_ASSERT_EQUAL(values[2], 0);

    // executing the jobs
    MORDOR_TEST_ASSERT_ANY_EXCEPTION(pool.stop());
    pool.stop();

    MORDOR_TEST_ASSERT_EQUAL(values[0], 1);
    MORDOR_TEST_ASSERT_EQUAL(values[1], 0);
    // even though the 2nd is exceptioned,
    // the 3rd one should still have chance to get executed
    MORDOR_TEST_ASSERT_EQUAL(values[2], 3);
}

static void doSleeping(boost::mutex &mutex, int &count, int &reference, int &max, IOManager &ioManager)
{
    boost::mutex::scoped_lock lock(mutex);
    ++reference;
    ++count;
    if (reference > max)
        max = reference;
    lock.unlock();
    sleep(ioManager, 5000);
    lock.lock();
    --reference;
}

MORDOR_UNITTEST(Scheduler, parallelDoParallelism)
{
    IOManager ioManager(6, true);
    int reference = 0, count = 0, max = 0;
    boost::mutex mutex;
    std::vector<std::function<void ()> > dgs;
    for (int i=0; i<1000; ++i) {
        dgs.push_back(std::bind(&doSleeping,
                            std::ref(mutex),
                            std::ref(count),
                            std::ref(reference),
                            std::ref(max),
                            std::ref(ioManager)));
    }
    // 6 threads in IOManager, but only parallel do with 4.
    parallel_do(dgs, 4);
    ioManager.stop();
    MORDOR_TEST_ASSERT_EQUAL(reference, 0);
    MORDOR_TEST_ASSERT_EQUAL(count, 1000);
    MORDOR_TEST_ASSERT_LESS_THAN_OR_EQUAL(max, 4);
}

#ifndef NDEBUG
MORDOR_UNITTEST(Scheduler, parallelDoEvilParallelism)
{
    WorkerPool pool(2, true);
    std::vector<std::function<void ()> > dgs;
    for (int i=0; i<2; ++i) {
        dgs.push_back(std::bind(nop<int>, 1));
    }
    // doing something evil, no one can save you
    MORDOR_TEST_ASSERT_ASSERTED(parallel_do(dgs, 0));
    pool.stop();
}
#endif

namespace {
    struct DummyClass {
        ~DummyClass() { Scheduler::yield(); }
    };

    static void fun(std::shared_ptr<DummyClass> a) {}
}

MORDOR_UNITTEST(Scheduler, allowYieldInDestructor)
{
    WorkerPool pool(2, true);
    pool.schedule(std::bind(fun, std::shared_ptr<DummyClass>(new DummyClass)));
    pool.schedule(Fiber::ptr(
            new Fiber(std::bind(fun, std::shared_ptr<DummyClass>(new DummyClass)))));
    pool.stop();
}
