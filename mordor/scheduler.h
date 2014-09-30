#ifndef __MORDOR_SCHEDULER_H__
#define __MORDOR_SCHEDULER_H__
// Copyright (c) 2009 - Mozy, Inc.

// C++
#include <atomic>
#include <deque>
#include <functional>
#include <list>
#include <thread>

// Boost
#include <boost/thread/shared_mutex.hpp>

#include "fiber.h"
#include "thread.h"

namespace Mordor {

class Fiber;

/// Cooperative user-mode thread (Fiber) Scheduler

/// A Scheduler is used to cooperatively schedule fibers on threads,
/// implementing an M-on-N threading model. A Scheduler can either "hijack"
/// the thread it was created on (by passing useCaller = true in the
/// constructor), spawn multiple threads of its own, or a hybrid of the two.
///
/// Hijacking and Schedulers begin processing fibers when either
/// yieldTo() or dispatch() is called. The Scheduler will stop itself when
/// there are no more Fibers scheduled, and return from yieldTo() or
/// dispatch(). Hybrid and spawned Schedulers must be explicitly stopped via
/// stop(). stop() will return only after there are no more Fibers scheduled.
class Scheduler
{
public:
    enum State {
        RUNNING,
        STOPPING,
        STOPPED,
    };

public:
    /// Default constructor

    /// By default, a single-threaded hijacking Scheduler is constructed.
    /// If threads > 1 && useCaller == true, a hybrid Scheduler is constructed.
    /// If useCaller == false, this Scheduler will not be associated with
    /// the currently executing thread.
    /// @param threads How many threads this Scheduler should be comprised of
    /// @param useCaller If this Scheduler should "hijack" the currently
    /// executing thread
    /// @param batchSize Number of operations to pull off the scheduler queue
    /// on every iteration
    /// @pre if (useCaller == true) Scheduler::getThis() == NULL
    Scheduler(size_t threads = 1, bool useCaller = true, size_t batchSize = 1);
    /// Destroys the scheduler, implicitly calling stop()
    virtual ~Scheduler();

    /// @return The Scheduler controlling the currently executing thread
    static Scheduler* getThis();

    /// Explicitly start the Scheduler

    /// Derived classes should call start() in their constructor.
    /// It is safe to call start() even if the Scheduler is already started -
    /// it will be a no-op
    void start();

    /// Explicitly stop the scheduler

    /// This must be called for hybrid and spawned Schedulers.  It is safe to
    /// call stop() even if the Scheduler is already stopped (or stopping) -
    /// it will be a no-op
    /// For hybrid or hijacking schedulers, it must be called from within
    /// the scheduler.  For spawned Schedulers, it must be called from outside
    /// the Scheduler.
    /// If called on a hybrid/hijacking scheduler from a Fiber
    /// that did not create the Scheduler, it will return immediately (the
    /// Scheduler will yield to the creating Fiber when all work is complete).
    /// In all other cases stop() will not return until all work is complete.
    void stop();

    /// Schedule a Fiber to be executed on the Scheduler

    /// @param fd The Fiber or the functor to schedule, if a pointer is passed
    ///           in, the ownership will be transfered to this scheduler
    /// @param thread Optionally provide a specific thread for the Fiber to run
    /// on
    template <class FiberOrDg>
    void schedule(FiberOrDg&& fd, std::thread::id thread = {});

    /// Schedule multiple items to be executed at once

    /// @param begin The first item to schedule
    /// @param end One past the last item to schedule
    template <class InputIterator>
    void schedule(InputIterator begin, InputIterator end);

    /// Change the currently executing Fiber to be running on this Scheduler

    /// This function can be used to change which Scheduler/thread the
    /// currently executing Fiber is executing on.  This switch is done by
    /// rescheduling this Fiber on this Scheduler, and yielding to the current
    /// Scheduler.
    /// @param thread Optionally provide a specific thread for this Fiber to
    /// run on
    /// @post Scheduler::getThis() == this
    void switchTo(std::thread::id thread = {});

    /// Yield to the Scheduler to allow other Fibers to execute on this thread

    /// The Scheduler will not re-schedule this Fiber automatically.
    ///
    /// In a hijacking Scheduler, any scheduled work will begin running if
    /// someone yields to the Scheduler.
    /// @pre Scheduler::getThis() != NULL
    static void yieldTo();

    /// Yield to the Scheduler to allow other Fibers to execute on this thread

    /// The Scheduler will automatically re-schedule this Fiber.
    /// @pre Scheduler::getThis() != NULL
    static void yield();

    /// Force a hijacking Scheduler to process scheduled work

    /// Calls yieldTo(), and yields back to the currently executing Fiber
    /// when there is no more work to be done
    /// @pre this is a hijacking Scheduler
    void dispatch();

    unsigned threadCount() const;

    /// Change the number of threads in this scheduler
    void threadCount(unsigned threads);

    // Get the list of thread ids in the current scheduler
    const std::list<std::thread::id> threads() const;

protected:

    /// Derived classes can query stopping() to see if the Scheduler is trying
    /// to stop, and should return from the idle Fiber as soon as possible.
    ///
    /// Also, this function should be implemented if the derived class has
    /// any additional work to do in the idle Fiber that the Scheduler is not
    /// aware of.
    virtual bool stopping();

    /// The function called (in its own Fiber) when there is no work scheduled
    /// on the Scheduler.  The Scheduler is not considered stopped until the
    /// idle Fiber has terminated.
    ///
    /// Implementors should Fiber::yield() when it believes there is work
    /// scheduled on the Scheduler.
    virtual void idle(const std::atomic<unsigned>& ec) = 0;
    /// The Scheduler wants to force the idle fiber to Fiber::yield(), because
    /// new work has been scheduled.

    virtual void tickle(const std::atomic<unsigned>& ec) = 0;

private:
    Scheduler(const Scheduler& rhs) = delete;

// Lockless queue (and queue entry)
private:

    struct Task
    {
        Fiber::ptr fiber;
        std::function<void ()> dg;
        std::thread::id thread;
        Task(const Fiber::ptr& f, std::thread::id th)
            : fiber(f), thread(th) 
        { }

        Task(Fiber::ptr* f, std::thread::id th)
            : thread(th)
        { fiber.swap(*f); }

        Task(std::function<void ()> d, std::thread::id th)
            : dg(d), thread(th) 
        { }

        Task(std::function<void ()> *d, std::thread::id th)
            : thread(th)
        { dg.swap(*d); }
    };

    // Worker classes (hidden from user)
    struct Worker;
    struct ThreadWorker;
    struct HijackWorker;
    friend struct Worker;
    //
    struct WorkerList;

private:
    void yieldTo(bool yieldToCallerOnTerminate);
    void schedule(std::unique_ptr<Task> task);

private:
    static thread_local Fiber* t_fiber;
    static thread_local Worker* t_worker;

    bool m_hijack;
    size_t m_batchSize;
    unsigned m_threadCount;

    boost::shared_mutex m_lock;

    std::atomic<State> m_state;
    std::unique_ptr<WorkerList> worker_list;
    Fiber::ptr context;

    std::atomic<unsigned> work_count;
    std::atomic<unsigned> done_count;
};

/// Automatic Scheduler switcher

/// Automatically returns to Scheduler::getThis() when goes out of scope
/// (by calling Scheduler::switchTo())
struct SchedulerSwitcher
{
public:
    /// Captures Scheduler::getThis(), and optionally calls target->switchTo()
    /// if target != NULL
    SchedulerSwitcher(Scheduler *target = NULL);
    /// Calls switchTo() on the Scheduler captured in the constructor
    /// @post Scheduler::getThis() == the Scheduler captured in the constructor
    ~SchedulerSwitcher();
    SchedulerSwitcher(const SchedulerSwitcher& rhs) = delete;

private:
    Scheduler *m_caller;
};

// Implementation

template <class FiberOrDg>
inline void Scheduler::schedule(FiberOrDg&& fd, std::thread::id tid)
{
    std::unique_ptr<Task> task(new Task(std::forward<FiberOrDg>(fd), tid));
    schedule(std::move(task));
}

template <class InputIterator>
inline void Scheduler::schedule(InputIterator begin, InputIterator end)
{
    for (; begin != end; ++begin)
        schedule(&*begin);
}

} // namespace: Mordor

#endif
