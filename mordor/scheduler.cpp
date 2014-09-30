// Copyright (c) 2009 - Mozy, Inc.

// C++
#include <functional>
#include <list>
#include <memory>
#include <vector>
using std::function;
using std::list;
using std::shared_ptr;
using std::vector;
using std::unique_ptr;

// Boost
#include <boost/thread/locks.hpp>
#include <boost/iterator/iterator_concepts.hpp>

// Mordor
#include "scheduler.h"
#include "assert.h"
#include "fiber.h"
#include "platform/eventcount_linux.hpp"

namespace Mordor {

typedef boost::shared_lock<boost::shared_mutex> ReadGuard;
typedef boost::unique_lock<boost::shared_mutex> WriteGuard;
typedef std::lock_guard<std::mutex> MutexGuard;

static Logger::ptr g_log = Log::lookup("mordor:scheduler");

thread_local Fiber* Scheduler::t_fiber = nullptr;
thread_local struct Scheduler::Worker* Scheduler::t_worker = nullptr;

enum WorkerStatus : uint32_t
{
    NOT_STARTED,
    STARTING,
    RUNNING,
    DISPATCH,
    KILL,
    STOPPING,
    STOPPED
};

struct FiberStack
{
    size_t  len;
    void*   ptr;
};

struct Scheduler::Worker
{
    Worker(Scheduler* parent, int batch = 1);
    virtual ~Worker();

    Scheduler* get_parent() const;

    /** Start worker */
    virtual void start() = 0;

    /** Stop running worker */
    virtual void stop();

    /** Unconditional wakeup. */
    void wakeup() const;

    /** Post a new task and wakeup if needed. */
    void enqueue(unique_ptr<Task> t);

    /** Get worker's thread id */
    virtual std::thread::id get_tid() const = 0;

protected:
    void change_status(WorkerStatus status);
    void change_status(WorkerStatus status, WorkerStatus expected);

    void run(const std::function<void()>& idle_fun);

private:
    /** Check if we should commit suecide */
    bool check_done() const;
    
    /** Return all unused work to the parent scheduler */
    void return_work();

    /** Get/setup fiber for this task */
    Fiber* work_fiber(Scheduler::Task& task);

    int get_next_task(vector<unique_ptr<Task>>& out, int count = 1);
    void steal_task(vector<unique_ptr<Task>>& out);

    void run_task(unique_ptr<Scheduler::Task> task);

    void do_idle(const Fiber::ptr& idle_fiber, unsigned ec_prev) const;

    void cleanup(const Fiber::ptr& idle_fiber);

    /** Get a stack for fiber that doesn't have one yet. */
    void get_stack();

protected:
    Scheduler* parent;
    int batch;

private:
    std::atomic<WorkerStatus> status;
    std::atomic<unsigned> event_count;

    std::deque<Task*> affinity_queue;
    std::deque<Task*> work_queue;
    std::mutex lock;
};

struct Scheduler::ThreadWorker final: public Scheduler::Worker
{
    virtual ~ThreadWorker();

    ThreadWorker(Scheduler* parent, std::function<void()> idle_func, int batch = 1)
        : Worker(parent, batch),
          idle_func(std::move(idle_func))
    { };

    virtual void start();

    virtual std::thread::id get_tid() const
    { return runner.get_id(); }

private:
    std::function<void()> idle_func;
    std::thread runner;
};

struct Scheduler::HijackWorker final : public Scheduler::Worker
{
    virtual ~HijackWorker();

    HijackWorker(Scheduler* parent, std::function<void()> idle_func, int batch = 1)
        : Worker(parent, batch),
          tid(std::this_thread::get_id()),
          runner(std::make_shared<Fiber>(std::bind(&HijackWorker::run, this,
                                                   std::move(idle_func))))
    {
        t_worker = this;
        t_fiber = runner.get();
    };

    virtual void start();

    virtual std::thread::id get_tid() const
    { return tid; }

    void stop_when_done()
    {
        change_status(WorkerStatus::DISPATCH);
        wakeup();
    }

private:
    std::thread::id tid;
    Fiber::ptr runner;
};

static void cancelFiber(Fiber& fib)
{
    try {
        try { throw boost::enable_current_exception(OperationAbortedException()); }
        catch(...) { fib.inject(boost::current_exception()); }
    } catch (OperationAbortedException) {
        // Nothing
    }
}

struct Scheduler::WorkerList : public vector<shared_ptr<Worker>>
{
    WorkerList(WorkerList&& rhs) = default;
    WorkerList(unsigned reserve = 0)
    { if (reserve > 1) this->reserve(reserve); }
};

Scheduler::Worker::Worker(Scheduler* parent, int batch)
    : parent(parent),
      batch(batch),
      status(WorkerStatus::NOT_STARTED),
      event_count(0)
{ }

Scheduler::Worker::~Worker()
{
    // TODO: Free all the stacks
}

inline
Scheduler* Scheduler::Worker::get_parent() const
{
    return parent;
}

inline
void Scheduler::Worker::stop()
{
    change_status(WorkerStatus::KILL);
    // TODO: Wakeup workers
    // Can't call it currently since it gets called from the destructor
}

void Scheduler::Worker::wakeup() const
{
    parent->tickle(event_count);
}

inline
void Scheduler::Worker::enqueue(unique_ptr<Task> task)
{
    {
        MutexGuard _guard(lock);
        work_queue.push_back(task.release());
    }

    if (internal::eventcount_post(event_count))
        wakeup();
}

inline
bool Scheduler::Worker::check_done() const
{
    return status.load(std::memory_order_relaxed) == KILL ||
           parent->stopping();
}

inline
void Scheduler::Worker::return_work() 
{
    MutexGuard _guard(lock); 

    for (; work_queue.size(); work_queue.pop_front()) {
        parent->schedule(unique_ptr<Task>(work_queue.front()));
    }
}

inline
Fiber* Scheduler::Worker::work_fiber(Scheduler::Task& task)
{
    Fiber* f = task.fiber.get();

    if (f && f->state() != Fiber::TERM) {
        return f;
    } else if (task.dg) {
        // TODO: Get new stack from pool
        task.fiber = std::make_shared<Fiber>(std::move(task.dg));
        return task.fiber.get();
    }

    MORDOR_LOG_ERROR(g_log) << this << " Unexpected dead fiber";
    return nullptr;
}

inline
int Scheduler::Worker::get_next_task(vector<unique_ptr<Task>>& out, int count)
{
    int i = 0;
    MutexGuard _guard(lock);

    for (; i < count && !affinity_queue.empty(); i++) {
        affinity_queue.emplace_back(work_queue.front());
        affinity_queue.pop_front();
    }

    for (; i < count && !work_queue.empty(); i++) {
        out.emplace_back(work_queue.front());
        work_queue.pop_front();
    }

    return i;
}

inline
void Scheduler::Worker::steal_task(vector<unique_ptr<Task>>& out)
{
    ReadGuard _guard(parent->m_lock); 

    for (const shared_ptr<Worker>& w: *parent->worker_list) {
        MutexGuard  _guard(w->lock);

        if (!w->work_queue.empty()) {
            unique_ptr<Task> task(work_queue.back());

            if (task->thread != std::thread::id()) 
                w->affinity_queue.emplace_back(std::move(task));
            else {
                out->push_back(std::move(task));
                return;
            }
        }
    }
}

inline
void Scheduler::Worker::change_status(WorkerStatus new_status)
{
    status.store(new_status, std::memory_order_relaxed);
}

inline
void Scheduler::Worker::change_status(WorkerStatus new_status, WorkerStatus expected)
{
    status.compare_exchange_strong(expected, new_status, std::memory_order_relaxed,
                                   std::memory_order_relaxed);
}

inline
void Scheduler::Worker::run_task(unique_ptr<Scheduler::Task> task)
{
    Fiber* f = work_fiber(*task);

    if (f) {
        if (f->state() == Fiber::State::EXEC) {
            // Fiber rescheduled itself hasn't fallen asleep yet
            enqueue(std::move(task));
            return;
        }

        MORDOR_LOG_DEBUG(g_log) << this << " running " << task.get();
        f->yieldTo();
        parent->done_count.fetch_add(1, std::memory_order_relaxed);
    }

    if (f && f->state() == Fiber::TERM) {
        // TODO: Return stack to stack pool
    }
}

inline
void Scheduler::Worker::do_idle(const Fiber::ptr& idle_fiber, unsigned ec_prev) const
{
    if (!idle_fiber) {
        // No idle fiber (spin)
        return;
    } else if (idle_fiber->state() == Fiber::State::TERM) {
        // Idle fiber exited (or died)
        return false;
    }

    if (internal::eventcount_shouldsleep(event_count, ec_prev))
        idle_fiber->call();
}

static inline
void log_run_error()
{
    // Swallow any exceptions that might occur while trying to log the current fiber state #98680
    try {
        MORDOR_LOG_FATAL(Log::root())
            << boost::current_exception_diagnostic_information();
    } catch (...) {
    }
}

void Scheduler::Worker::run(const std::function<void()>& idle_func)
{
    vector<unique_ptr<Task>> tasks;
    unsigned    empty_runs = 0;
    Fiber::ptr  idle_fiber;

    if (dynamic_cast<ThreadWorker*>(this)) {
        t_fiber = Fiber::getThis().get();
    } else {
        MORDOR_ASSERT(t_fiber == Fiber::getThis().get());
    }

    if (idle_func)
        idle_fiber = std::make_shared<Fiber>(std::bind(idle_func, std::ref(event_count)));

    // Current scheduler worker
    t_worker = this;

    // Keep enough space
    tasks.reserve(batch);

    // Make this thread as running (unless it's already switched to KILL or DISPATCH mode)
    change_status(WorkerStatus::RUNNING, WorkerStatus::STARTING);

    while (!check_done()) {
        unsigned ec_prev = internal::eventcount_current(event_count);

        // Get task, if we fail try to steal a task
        get_next_task(tasks, batch);
        if (tasks.empty())
            steal_task(tasks);

        try {
            for (unsigned i = 0; i < tasks.size(); i++) {
                run_task(std::move(tasks[i]));
            }
        } catch (...) {
            log_run_error();
            cleanup(idle_fiber);
            throw;
        }

        // There was work for us to do
        if (tasks.size()) {
            tasks.clear();
            empty_runs = 0;
            continue;
        }

        // If this thread is running in dispatch mode, return when there's no more work
        // left to do.
        if (status.load(std::memory_order_relaxed) == WorkerStatus::DISPATCH) {
            parent->context->yieldTo();
            continue;
        }

        // Try adaptive spinning to see if we can get some data before we go to sleep
        empty_runs++;
        if (empty_runs < 100) {
            continue;
        } else if (empty_runs < 1000) {
            std::this_thread::yield();
        }

        // Run idle loop
        do_idle(idle_fiber, ec_prev);
    }

    cleanup(idle_fiber);
}

void Mordor::Scheduler::Worker::cleanup(const Fiber::ptr& idle_fiber)
{
    // Shutdown sequence:
    // - Mark the worker as shutting down
    // - Cleanup idle fiber
    // - Return the work (to other fibers)
    if ((bool) idle_fiber && idle_fiber->state() == Fiber::State::HOLD) {
        cancelFiber(*idle_fiber);
    }

    return_work();

    // We're dead
    change_status(WorkerStatus::STOPPED);
}

Scheduler::ThreadWorker::~ThreadWorker()
{
    stop();

    if (runner.joinable()) {
        runner.join();
    }
}

void Scheduler::ThreadWorker::start()
{
    change_status(WorkerStatus::STARTING);

    // Create the fiber containing the run loop
    runner = std::thread(std::bind(&ThreadWorker::run, this, std::move(idle_func)));
}

Scheduler::HijackWorker::~HijackWorker()
{
    if (runner->state() == Fiber::State::HOLD) {
        cancelFiber(*runner);
    }

    t_worker = nullptr;
    t_fiber = nullptr;
}

void Scheduler::HijackWorker::start()
{
    parent->context = Fiber::getThis();
    change_status(WorkerStatus::STARTING, WorkerStatus::NOT_STARTED);

    // Save our calling fiber, so we can return
    parent->yieldTo(true);
}

Scheduler::Scheduler(size_t threads, bool useCaller, size_t batchSize)
    : m_hijack(useCaller),
      m_batchSize(batchSize),
      m_threadCount(threads),
      m_state(State::STOPPED),
      work_count(0),
      done_count(0)
{
    MORDOR_ASSERT(threads >= 1);

    if (useCaller) {
        // Not allowed to create a new scheduler if already running in a scheduler
        MORDOR_ASSERT(!getThis());
    }
}

Scheduler::~Scheduler()
{
    // Derived shedulers should make sure we're stopped...
    // virtual calls might be needed to shutdown
    MORDOR_ASSERT(m_state == State::STOPPED);
}

Scheduler*
Scheduler::getThis()
{
    return (t_worker)
        ? t_worker->get_parent()
        : nullptr;
}

void
Scheduler::start()
{
    std::function<void()>   idle_func = std::bind(&Mordor::Scheduler::idle, this);
    unique_ptr<WorkerList>  new_list(new WorkerList);

    MORDOR_LOG_VERBOSE(g_log) << this << " starting " << m_threadCount << " threads";

    WriteGuard lock(m_lock);

    // Only start if we're in a already fully stopped mode
    if (m_state != STOPPED)
        return;

    MORDOR_ASSERT(!worker_list);

    // Create the "thread-less" worker
    if (m_hijack) {
        auto worker = std::make_shared<HijackWorker>(this, idle_func, m_batchSize);
        new_list->emplace_back(std::move(worker));
    }

    // Create the threaded worker
    for (unsigned i = (m_hijack) ? 1 : 0; i < m_threadCount; ++i) {
        auto worker = std::make_shared<ThreadWorker>(this, idle_func, m_batchSize);
        new_list->emplace_back(std::move(worker));
        new_list->back()->start();
    }

    worker_list = std::move(new_list);
    m_state = State::RUNNING;
}

void
Scheduler::stop()
{
    unsigned    count = (m_hijack) ? m_threadCount -1 : m_threadCount;
    WorkerList  stop_list(count);

    {
        WriteGuard _guard(m_lock);

        if (!m_state == RUNNING) {
            _guard.unlock();
            MORDOR_LOG_VERBOSE(g_log) << this << " already stopping / stopped";
            return;
        } else if (!worker_list) {
            _guard.unlock();
            MORDOR_LOG_VERBOSE(g_log) << this << " already stopped";
            return;
        }

        for (unsigned i = (m_hijack) ? 1 : 0; i < worker_list->size(); i++)
            stop_list.push_back(worker_list->at(i));

        // Leave the "hijacked" worker running
        if (m_hijack)
            worker_list->resize(1);

        m_state.store(State::STOPPING, std::memory_order_relaxed);
    }

    MORDOR_LOG_INFO(g_log) << this << " stopping scheduler";
    tickle();

    if (m_hijack) {
        // A thread-hijacking scheduler must be stopped from within itself to return control to the
        // original thread.
        MORDOR_ASSERT(Scheduler::getThis() == this);

        // Switch to the hijacked thread
        if (!dynamic_cast<HijackWorker*>(t_worker)) {
            std::thread::id tid;

            {
                ReadGuard _guard(m_lock);
                tid = worker_list->at(0)->get_tid();
            }

            MORDOR_LOG_DEBUG(g_log) << this << " switching to root thread to stop";
            switchTo(tid);
        }
    } else {
        // A spawned-threads only scheduler cannot be stopped from within itself...
        // who would get control?
        MORDOR_ASSERT(Scheduler::getThis() != this);
    }

    // Notify all of worker to stop
    stop_list.clear();

    while (!stopping()) {
        MORDOR_LOG_DEBUG(g_log) << this << " yielding to this thread to stop";
        yieldTo(true);
    }

    {
        WriteGuard lock(m_lock);
        m_state = State::STOPPED;

        if (m_hijack) {
            worker_list.reset();
            MORDOR_ASSERT(!t_fiber);
        }
    }

    MORDOR_LOG_VERBOSE(g_log) << this << " stopped";
    // Return back to our callin context
}

bool
Scheduler::stopping()
{
    unsigned    done, work;
    ReadGuard   lock(m_lock);

    if (m_state.load(std::memory_order_relaxed) != STOPPING)
        return false;

    // Only fully stop once there's no more work
    done = done_count.load(std::memory_order_relaxed);
    work = work_count.load(std::memory_order_relaxed);
    return done == work;
}

void
Scheduler::switchTo(std::thread::id thread)
{
    MORDOR_ASSERT(Scheduler::getThis() != NULL);

    if (Scheduler::getThis() == this && thread == std::this_thread::get_id())
        return;

    MORDOR_LOG_DEBUG(g_log) << this << " switching to thread " << thread;
    schedule(Fiber::getThis(), thread);
    Scheduler::yieldTo();
}

void
Scheduler::yieldTo()
{
    Scheduler* self = Scheduler::getThis();
    HijackWorker* hworker = dynamic_cast<HijackWorker*>(t_worker);

    MORDOR_ASSERT(self);
    MORDOR_LOG_DEBUG(g_log) << self << " yielding to scheduler";
    MORDOR_ASSERT(t_fiber);

    if (hworker &&
        (t_fiber->state() == Fiber::State::INIT || t_fiber->state() == Fiber::State::TERM))
    {
        hworker->start();
    } else {
        self->yieldTo(false);
    }
}

void
Scheduler::yield()
{
    MORDOR_ASSERT(Scheduler::getThis());

    // Make sure we schedule ourselves first 
    Scheduler::getThis()->schedule(Fiber::getThis());
    yieldTo();
}

void
Scheduler::dispatch()
{
    HijackWorker* worker;

    MORDOR_LOG_DEBUG(g_log) << this << " dispatching";
    MORDOR_ASSERT(worker = dynamic_cast<HijackWorker*>(t_worker));
    MORDOR_ASSERT(worker->get_tid() == std::this_thread::get_id());

    worker->stop_when_done();
    yieldTo();
}

void
Scheduler::threadCount(unsigned threads)
{
    unique_ptr<WorkerList>  old_list;
    unique_ptr<WorkerList>  new_list(new WorkerList(threads));
    unsigned                prev;

    MORDOR_ASSERT(threads >= 1);

    // This is done infreqently
    WriteGuard lock(m_lock);

    m_threadCount = threads;

    // We're not running so no ned to manually kick things off
    if (m_state.load(std::memory_order_relaxed) != Scheduler::State::RUNNING)
        return;

    old_list = std::move(worker_list);
    prev = old_list->size();

    // Notify old threads to shutdown
    for (unsigned i = threads; i < prev; i++) {
        Worker* w = worker_list->at(i).get();
        w->stop();
    }

    // Copy the list of alive ones
    size_t copy = (prev < threads) ? prev : threads;
    for (size_t i = 0; i < copy; i++) {
        new_list->emplace_back(worker_list->at(i));
    }

    // Create new ones if needed
    for (unsigned i = prev; i < threads; i++) {
        auto worker = std::make_shared<ThreadWorker>(this, nullptr, m_batchSize);
        new_list->emplace_back(std::move(worker));
    }

    worker_list = std::move(new_list);
}

unsigned
Scheduler::threadCount() const
{
    Scheduler*  self = const_cast<Scheduler*>(this);
    ReadGuard   guard(self->m_lock);

    return self->m_threadCount;
}

const list<std::thread::id> Scheduler::threads() const
{
    list<std::thread::id>   result;
    Scheduler*              self = const_cast<Scheduler*>(this);
    ReadGuard               guard(self->m_lock);

    for (const shared_ptr<Worker>& ptr: *(self->worker_list))
        result.push_back(ptr->get_tid());

    return result;
}

void
Scheduler::yieldTo(bool yieldToCallerOnTerminate)
{
    MORDOR_ASSERT(t_fiber);
    MORDOR_ASSERT(Scheduler::getThis() == this);

    // Ensure we're running inside the hihack worker (if we're transfering out)
    if (yieldToCallerOnTerminate)
        MORDOR_ASSERT(dynamic_cast<HijackWorker*>(t_worker));

    t_fiber->yieldTo(yieldToCallerOnTerminate);
}

void
Scheduler::schedule(unique_ptr<Scheduler::Task> task)
{
    unsigned prev = work_count.fetch_add(1, std::memory_order_relaxed);
    MORDOR_LOG_DEBUG(g_log) << this << " Adding work " << task.get();

    ReadGuard lock(m_lock);

    unsigned            count = worker_list->size();
    shared_ptr<Worker>* workers = worker_list->data();

    if (task->thread != std::thread::id()) {
        // Try to find the worker in the list
        for (unsigned i = 0; i < count ; i++) {
            if (workers[i]->get_tid() == task->thread) {
                workers[i]->enqueue(std::move(task));
                return;
            }
        }
    }

    // Round robin which worker we're going to use
    unsigned which = prev % count;
    workers[which]->enqueue(std::move(task));

    m_lock.unlock();
    work_count.load();
}

void Scheduler::tickle()
{
    ReadGuard lock(m_lock);

    for (const shared_ptr<Worker*>& w: *worker_list) {
        w->wakeup();
    }
}

SchedulerSwitcher::SchedulerSwitcher(Scheduler *target)
{
    m_caller = Scheduler::getThis();
    if (target)
        target->switchTo();
}

SchedulerSwitcher::~SchedulerSwitcher()
{
    if (m_caller)
        m_caller->switchTo();
}

}
