#ifndef __MORDOR_FIBERSYNCHRONIZATION_H__
#define __MORDOR_FIBERSYNCHRONIZATION_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <list>

#include <boost/thread/mutex.hpp>

namespace Mordor {

class Fiber;
class Scheduler;

/// Scheduler based Mutex for Fibers

/// Mutex for use by Fibers that yields to a Scheduler instead of blocking
/// if the mutex cannot be immediately acquired.  It also provides the
/// additional guarantee that it is strictly FIFO, instead of random which
/// Fiber will acquire the mutex next after it is released.
struct FiberMutex
{
    friend struct FiberCondition;
public:
    /// Type that will lock the mutex on construction, and unlock on
    /// destruction
    struct ScopedLock
    {
    public:
        ScopedLock(FiberMutex &mutex)
            : m_mutex(&mutex)
        {
            m_mutex->lock();
            m_locked = true;
        }

        ScopedLock(ScopedLock&& rhs)
            : m_mutex(rhs.m_mutex),
              m_locked(rhs.m_locked)
        { 
            rhs.m_mutex = nullptr;
            rhs.m_locked = false;
        }

        ScopedLock& operator= (ScopedLock&& rhs)
        {
            std::swap(rhs.m_mutex, m_mutex);
            std::swap(rhs.m_locked, m_locked);
            return *this;    
        }

        ~ScopedLock()
        { unlock(); }

        void lock()
        {
            if (!m_locked) {
                m_mutex->lock();
                m_locked = true;
            }
        }

        void unlock()
        {
            if (m_locked) {
                m_mutex->unlock();
                m_locked = false;
            }
        }

        bool unlockIfNotUnique()
        {
            if (m_locked) {
                if (m_mutex->unlockIfNotUnique()) {
                    m_locked = false;
                    return true;
                } else {
                    return false;
                }
            }
            return true;
        }

    private:
        FiberMutex *m_mutex;
        bool m_locked;
    };

public:
    FiberMutex() = default;
    ~FiberMutex();

    /// @brief Locks the mutex
    /// Note that it is possible for this Fiber to switch threads after this
    /// method, though it is guaranteed to still be on the same Scheduler
    /// @pre Scheduler::getThis() != NULL
    /// @pre Fiber::getThis() does not own this mutex
    /// @post Fiber::getThis() owns this mutex
    void lock();
    /// @brief Unlocks the mutex
    /// @pre Fiber::getThis() owns this mutex
    void unlock();

    /// Unlocks the mutex if there are other Fibers waiting for the mutex.
    /// This is useful if there is extra work should be done if there is no one
    /// else waiting (such as flushing a buffer).
    /// @return If the mutex was unlocked
    bool unlockIfNotUnique();

private:
    void unlockNoLock();

private:
    FiberMutex(const FiberMutex& rhs) = delete;

private:
    boost::mutex m_mutex;
    std::shared_ptr<Fiber> m_owner;
    std::list<std::pair<Scheduler *, std::shared_ptr<Fiber> > > m_waiters;
};

/// Scheduler based Semaphore for Fibers

/// Semaphore for use by Fibers that yields to a Scheduler instead of blocking
/// if the mutex cannot be immediately acquired.  It also provides the
/// additional guarantee that it is strictly FIFO, instead of random which
/// Fiber will acquire the semaphore next after it is released.
struct FiberSemaphore
{
public:
    FiberSemaphore(size_t initialConcurrency = 0);
    ~FiberSemaphore();

    /// @brief Waits for the semaphore
    /// Decreases the amount of concurrency.  If concurrency is already at
    /// zero, it will wait for someone else to notify the semaphore
    /// @note It is possible for this Fiber to switch threads after this
    /// method, though it is guaranteed to still be on the same Scheduler
    /// @pre Scheduler::getThis() != NULL
    void wait();
    /// @brief Increases the level of concurrency
    void notify();

private:
    FiberSemaphore(const FiberSemaphore& rhs) = delete;

private:
    boost::mutex m_mutex;
    std::list<std::pair<Scheduler *, std::shared_ptr<Fiber> > > m_waiters;
    size_t m_concurrency;
};

/// Scheduler based condition variable for Fibers

/// Condition for use by Fibers that yields to a Scheduler instead of blocking.
/// It also provides the additional guarantee that it is strictly FIFO,
/// instead of random which waiting Fiber will be released when the condition
/// is signalled.
struct FiberCondition
{
public:
    /// @param mutex The mutex to associate with the Condition
    FiberCondition(FiberMutex &mutex)
        : m_fiberMutex(mutex)
    {}
    ~FiberCondition();

    /// @brief Wait for the Condition to be signalled
    /// @details
    /// Atomically unlock mutex, and wait for the Condition to be signalled.
    /// Once released, the mutex is locked again.
    /// @pre Scheduler::getThis() != NULL
    /// @pre Fiber::getThis() owns mutex
    /// @post Fiber::getThis() owns mutex
    void wait();
    /// Release a single Fiber from wait()
    void signal();
    /// Release all waiting Fibers
    void broadcast();

private:
    FiberCondition(const FiberCondition& rhs) = delete;

private:
    boost::mutex m_mutex;
    FiberMutex &m_fiberMutex;
    std::list<std::pair<Scheduler *, std::shared_ptr<Fiber> > > m_waiters;
};

/// Scheduler based event variable for Fibers

/// Event for use by Fibers that yields to a Scheduler instead of blocking.
/// It also provides the additional guarantee that it is strictly FIFO,
/// instead of random which waiting Fiber will be released when the event
/// is signalled.
struct FiberEvent
{
public:
    /// @param autoReset If the Event should automatically reset itself
    /// whenever a Fiber is released
    FiberEvent(bool autoReset = true)
        : m_signalled(false),
          m_autoReset(autoReset)
    {}
    ~FiberEvent();

    /// @brief Wait for the Event to become set
    /// @pre Scheduler::getThis() != NULL
    void wait();
    /// Set the Event
    void set();
    /// Reset the Event
    void reset();

private:
    FiberEvent(const FiberEvent& rhs) = delete;

private:
    boost::mutex m_mutex;
    bool m_signalled, m_autoReset;
    std::list<std::pair<Scheduler *, std::shared_ptr<Fiber> > > m_waiters;
};

}

#endif
