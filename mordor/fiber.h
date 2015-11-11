#ifndef __MORDOR_FIBER_H__
#define __MORDOR_FIBER_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <memory>

#include "exception.h"
#include "version.h"

#ifdef WINDOWS
#include "platform/windows_fiber.h"
#else
#include "platform/unix_fiber.h"
#endif

namespace Mordor {

/// Cooperative Thread
class Fiber final : public std::enable_shared_from_this<Fiber>, private internal::__Fiber
{
    template <class T> friend class FiberLocalStorageBase;
public:
    typedef std::shared_ptr<Fiber> ptr;
    typedef std::weak_ptr<Fiber> weak_ptr;

    /// The current execution state of a Fiber
    enum State
    {
        /// Initialized, but not run
        INIT,
        /// Currently suspended
        HOLD,
        /// Currently executing
        EXEC,
        /// Terminated because of an exception
        EXCEPT,
        /// Terminated
        TERM
    };

private:
    /// @brief Create a Fiber for the currently executing thread
    /// @pre No other Fiber object represents the currently executing thread
    /// @post state() == EXEC
    Fiber();

public:
    /// @brief Create a new Fiber
    /// @param dg The initial function
    /// @param stacksize An explicit size for the stack.  This is initial size in virtual
    /// memory; physical/paging memory is not allocated until the actual pages
    /// are touched by the Fiber executing
    /// @post state() == INIT
    Fiber(std::function<void ()> dg, size_t stacksize = 0);
    ~Fiber();

    /// @brief Reset a Fiber to be used again, with a different initial function
    /// @param dg The new initial function
    /// @pre state() == INIT || state() == TERM || state() == EXCEPT
    /// @post state() == INIT
    void reset(std::function<void ()> dg);

    /// @return The currently executing Fiber
    static ptr getThis();

    /// Call a Fiber

    /// The Fiber is executed as a "child" Fiber of the currently executing
    /// Fiber.  The currently executing Fiber is left in the EXEC state,
    /// and this Fiber also transitions to the EXEC state by either calling
    /// the initial function, or returning from yield() or yieldTo().
    ///
    /// call() does not return until the Fiber calls yield(), returns,
    /// or throws an exception.
    /// @pre state() == INIT || state() == HOLD
    void call();

    /// Inject an exception into a Fiber

    /// The Fiber is executed, but instead of returning from yield() or
    /// yieldTo(), exception is rethrown in the Fiber
    /// @param exception The exception to be rethrown in the Fiber
    /// @pre state() == INIT || state() == HOLD
    void inject(boost::exception_ptr exception);

    /// Yield execution to a specific Fiber

    /// The Fiber is executed by replacing the currently executing Fiber.
    /// The currently executing Fiber transitions to the HOLD state, and this
    /// Fiber transitions to the EXEC state, by either calling the initial
    /// function, or returning from yield() or yieldTo().
    ///
    /// yieldTo() does not return until another Fiber calls yieldTo() on the
    /// currently executing Fiber, or yieldToCallerOnTerminate is true and
    /// this fiber returns or throws an exception
    /// @param yieldToCallerOnTerminate Whether to keep a weak reference back
    /// to the currently executing Fiber in order to yield back to it when this
    /// Fiber terminates
    /// @return The Fiber that yielded back
    /// (not necessarily the Fiber that was yielded to)
    /// @pre state() == INIT || state() == HOLD
    Fiber::ptr yieldTo(bool yieldToCallerOnTerminate = true);

    /// Yield to the calling Fiber

    /// yield() returns when the Fiber has been called or yielded to again
    /// @pre This Fiber was executed by call()
    static void yield();

    /// The current execution state of the Fiber
    State state();

    /// Get the backtrace of a fiber

    /// The fiber must not be currently executing.  If it's in a state other
    /// than HOLD, the backtrace will be empty.
    /// @pre state() != EXEC
    std::vector<void *> backtrace();

private:
    Fiber::ptr yieldTo(bool yieldToCallerOnTerminate, State targetState);

    static void setThis(Fiber *f);

    virtual void entryPoint();
    static void exitPoint(Fiber::ptr &cur, State targetState);

private:
    Fiber(const Fiber& rhs) = delete;

private:
    std::function<void ()> m_dg;
    void *m_sp;
    State m_state, m_yielderNextState;
    ptr m_outer, m_yielder;
    weak_ptr m_terminateOuter;
    boost::exception_ptr m_exception;

    static thread_local Fiber* t_fiber;

    // FLS Support
    static size_t flsAlloc();
    static void flsFree(size_t key);
    static void flsSet(size_t key, intptr_t value);
    static intptr_t flsGet(size_t key);

    std::vector<intptr_t> m_fls;
};

std::ostream &operator<<(std::ostream &os, Fiber::State state);

template <class T>
class FiberLocalStorageBase
{
public:
    FiberLocalStorageBase()
    {
        m_key = Fiber::flsAlloc();
    }

    ~FiberLocalStorageBase()
    {
        Fiber::flsFree(m_key);
    }

    typename boost::enable_if_c<sizeof(T) <= sizeof(intptr_t)>::type set(const T &t)
    {
        Fiber::flsSet(m_key, (intptr_t)t);
    }

    T get() const
    {
#ifdef WINDOWS
#pragma warning(push)
#pragma warning(disable: 4800)
#endif
        return (T)Fiber::flsGet(m_key);
#ifdef WINDOWS
#pragma warning(pop)
#endif
    }

    operator T() const { return get(); }

private:
    FiberLocalStorageBase(const FiberLocalStorageBase& rhs) = delete;

private:
    size_t m_key;
};

template <class T>
class FiberLocalStorage : public FiberLocalStorageBase<T>
{
public:
    T operator =(T t) { FiberLocalStorageBase<T>::set(t); return t; }
};

template <class T>
class FiberLocalStorage<T *> : public FiberLocalStorageBase<T *>
{
public:
    T * operator =(T *const t) { FiberLocalStorageBase<T *>::set(t); return t; }
    T & operator*() { return *FiberLocalStorageBase<T *>::get(); }
    T * operator->() { return FiberLocalStorageBase<T *>::get(); }
};

}

#endif // __FIBER_H__
