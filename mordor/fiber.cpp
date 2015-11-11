// Copyright (c) 2009 - Mozy, Inc.

#include <atomic>

#include "fiber.h"

#ifdef HAVE_CONFIG_H
#include "../autoconfig.h"
#endif

#ifdef HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#include "assert.h"
#include "config.h"
#include "exception.h"
#include "statistics.h"
#include "version.h"

#ifdef WINDOWS
#include <windows.h>

#include "runtime_linking.h"
#else
#include <sys/mman.h>
#include <pthread.h>
#endif

namespace Mordor {

static std::atomic<unsigned int> g_cntFibers(0); // Active fibers
static MaxStatistic<unsigned int> &g_statMaxFibers=Statistics::registerStatistic("fiber.max",
    MaxStatistic<unsigned int>());

#ifdef SETJMP_FIBERS
#ifdef OSX
#define setjmp _setjmp
#define longjmp _longjmp
#endif
#endif

static size_t g_pagesize;

namespace {

static struct FiberInitializer {
    FiberInitializer()
    {
#ifdef WINDOWS
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        g_pagesize = info.dwPageSize;
#elif defined(POSIX)
        g_pagesize = sysconf(_SC_PAGESIZE);
#endif
    }
} g_init;

}

static ConfigVar<size_t>::ptr g_defaultStackSize = Config::lookup<size_t>(
    "fiber.defaultstacksize",
#ifdef NATIVE_WINDOWS_FIBERS
    0u,
#else
    1024 * 1024u,
#endif
    "Default stack size for new fibers.  This is the virtual size; physical "
    "memory isn't consumed until it is actually referenced.");

// t_fiber is the Fiber currently executing on this thread
// t_threadFiber is the Fiber that represents the thread's original stack
thread_local Fiber* Fiber::t_fiber = nullptr;
static thread_local Fiber::ptr t_threadFiber;

static boost::mutex & g_flsMutex()
{
    static boost::mutex mutex;
    return mutex;
}
static std::vector<bool> & g_flsIndices()
{
    static std::vector<bool> indices;
    return indices;
}

Fiber::Fiber()
    : m_sp(stackid()),
      m_state(EXEC)
{
    g_statMaxFibers.update(++g_cntFibers);
    MORDOR_ASSERT(!t_fiber);
    setThis(this);
}

Fiber::Fiber(std::function<void ()> dg, size_t stacksize)
    : internal::__Fiber(stacksize ? stacksize : g_defaultStackSize->val()),
      m_dg(dg),
      m_sp(stackid()),
      m_state(INIT)
{
    g_statMaxFibers.update(++g_cntFibers);
}

Fiber::~Fiber()
{
    --g_cntFibers;
    if (!stack_ptr() ) {
        // Thread entry fiber
        MORDOR_ASSERT(!m_dg);
        MORDOR_ASSERT(m_state == EXEC);
        Fiber *cur = t_fiber;

        // We're actually running on the fiber we're about to delete
        // i.e. the thread is dying, so clean up after ourselves
        if (cur == this)  {
            setThis(NULL);
#ifdef NATIVE_WINDOWS_FIBERS
            if (m_stack) {
                MORDOR_ASSERT(m_stack == m_sp);
                MORDOR_ASSERT(m_stack == GetCurrentFiber());
                pConvertFiberToThread();
            }
#endif
        }
        // Otherwise, there's not a thread left to clean up
    } else {
        // Regular fiber
        MORDOR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
    }
}

void
Fiber::reset(std::function<void ()> dg)
{
    m_exception = boost::exception_ptr();
    MORDOR_ASSERT(stack_ptr() != nullptr);
    MORDOR_ASSERT(m_state == TERM || m_state == INIT || m_state == EXCEPT);
    m_dg = dg;
    internal::__Fiber::reset();
    m_state = INIT;
}

Fiber::ptr
Fiber::getThis()
{
    if (t_fiber)
        return t_fiber->shared_from_this();

    Fiber::ptr threadFiber(new Fiber());
    MORDOR_ASSERT(t_fiber == threadFiber.get());
    t_threadFiber = threadFiber;
    return t_fiber->shared_from_this();
}

void
Fiber::setThis(Fiber* f)
{
    t_fiber = f;
}

void
Fiber::call()
{
    MORDOR_ASSERT(!m_outer);
    ptr cur = getThis();
    MORDOR_ASSERT(m_state == HOLD || m_state == INIT);
    MORDOR_ASSERT(cur);
    MORDOR_ASSERT(cur.get() != this);
    setThis(this);
    m_outer = cur;
    m_state = m_exception ? EXCEPT : EXEC;
    cur->switchContext(this);
    setThis(cur.get());
    MORDOR_ASSERT(cur->m_yielder);
    m_outer.reset();
    if (cur->m_yielder) {
        MORDOR_ASSERT(cur->m_yielder.get() == this);
        Fiber::ptr yielder = cur->m_yielder;
        yielder->m_state = cur->m_yielderNextState;
        cur->m_yielder.reset();
        if (yielder->m_state == EXCEPT && yielder->m_exception)
            Mordor::rethrow_exception(yielder->m_exception);
    }
    MORDOR_ASSERT(cur->m_state == EXEC);
}

void
Fiber::inject(boost::exception_ptr exception)
{
    MORDOR_ASSERT(exception);
    m_exception = exception;
    call();
}

Fiber::ptr
Fiber::yieldTo(bool yieldToCallerOnTerminate)
{
    return yieldTo(yieldToCallerOnTerminate, HOLD);
}

void
Fiber::yield()
{
    ptr cur = getThis();
    MORDOR_ASSERT(cur);
    MORDOR_ASSERT(cur->m_state == EXEC);
    MORDOR_ASSERT(cur->m_outer);
    cur->m_outer->m_yielder = cur;
    cur->m_outer->m_yielderNextState = Fiber::HOLD;
    cur->switchContext(cur->m_outer.get());
    if (cur->m_yielder) {
        cur->m_yielder->m_state = cur->m_yielderNextState;
        cur->m_yielder.reset();
    }
    if (cur->m_state == EXCEPT) {
        MORDOR_ASSERT(cur->m_exception);
        Mordor::rethrow_exception(cur->m_exception);
    }
    MORDOR_ASSERT(cur->m_state == EXEC);
}

Fiber::State
Fiber::state()
{
    return m_state;
}

Fiber::ptr
Fiber::yieldTo(bool yieldToCallerOnTerminate, State targetState)
{
    MORDOR_ASSERT(m_state == HOLD || m_state == INIT);
    MORDOR_ASSERT(targetState == HOLD || targetState == TERM || targetState == EXCEPT);
    ptr cur = getThis();
    MORDOR_ASSERT(cur);
    setThis(this);
    if (yieldToCallerOnTerminate) {
        Fiber::ptr outer = shared_from_this();
        Fiber::ptr previous;
        while (outer) {
            previous = outer;
            outer = outer->m_outer;
        }
        previous->m_terminateOuter = cur;
    }
    m_state = EXEC;
    m_yielder = cur;
    m_yielderNextState = targetState;
    Fiber *curp = cur.get();
    // Relinguish our reference
    cur.reset();
    curp->switchContext(this);
#ifdef NATIVE_WINDOWS_FIBERS
    if (targetState == TERM)
        return Fiber::ptr();
#endif
    MORDOR_ASSERT(targetState != TERM);
    setThis(curp);
    if (curp->m_yielder) {
        Fiber::ptr yielder = curp->m_yielder;
        yielder->m_state = curp->m_yielderNextState;
        curp->m_yielder.reset();
        if (yielder->m_exception)
            Mordor::rethrow_exception(yielder->m_exception);
        return yielder;
    }
    if (curp->m_state == EXCEPT) {
        MORDOR_ASSERT(curp->m_exception);
        Mordor::rethrow_exception(curp->m_exception);
    }
    MORDOR_ASSERT(curp->m_state == EXEC);
    return Fiber::ptr();
}

void
Fiber::entryPoint()
{
    // This function never returns, so take care that smart pointers (or other resources)
    // are properly released.
    ptr cur = getThis();
    MORDOR_ASSERT(cur);
    if (cur->m_yielder) {
        cur->m_yielder->m_state = cur->m_yielderNextState;
        cur->m_yielder.reset();
    }
    MORDOR_ASSERT(cur->m_dg);
    State nextState = TERM;
    try {
        if (cur->m_state == EXCEPT) {
            MORDOR_ASSERT(cur->m_exception);
            Mordor::rethrow_exception(cur->m_exception);
        }
        MORDOR_ASSERT(cur->m_state == EXEC);
        cur->m_dg();
        cur->m_dg = NULL;
    } catch (boost::exception &ex) {
        removeTopFrames(ex);
        cur->m_exception = boost::current_exception();
        nextState = EXCEPT;
    } catch (...) {
        cur->m_exception = boost::current_exception();
        nextState = EXCEPT;
    }

    exitPoint(cur, nextState);
}

void
Fiber::exitPoint(Fiber::ptr &cur, State targetState)
{
    // This function never returns, so take care that smart pointers (or other resources)
    // are properly released.
    Fiber::ptr outer;
    Fiber *rawPtr = NULL;
    if (!cur->m_terminateOuter.expired() && !cur->m_outer) {
        outer = cur->m_terminateOuter.lock();
        rawPtr = outer.get();
    } else {
        outer = cur->m_outer;
        rawPtr = cur.get();
    }
    MORDOR_ASSERT(outer);
    MORDOR_ASSERT(rawPtr);
    MORDOR_ASSERT(outer != cur);

    // Have to set this reference before calling yieldTo()
    // so we can reset cur before we call yieldTo()
    // (since it's not ever going to destruct)
    outer->m_yielder = cur;
    outer->m_yielderNextState = targetState;
    MORDOR_ASSERT(!cur.unique());
    cur.reset();
    if (rawPtr == outer.get()) {
        rawPtr = outer.get();
        MORDOR_ASSERT(!outer.unique());
        outer.reset();
        rawPtr->yieldTo(false, targetState);
    } else {
        outer.reset();
        rawPtr->switchContext(rawPtr->m_outer.get());
    }
}

#ifdef WINDOWS
static bool g_doesntHaveOSFLS;
#endif

size_t
Fiber::flsAlloc()
{
#ifdef WINDOWS
    while (!g_doesntHaveOSFLS) {
        size_t result = pFlsAlloc(NULL);
        if (result == FLS_OUT_OF_INDEXES && lastError() == ERROR_CALL_NOT_IMPLEMENTED) {
            g_doesntHaveOSFLS = true;
            break;
        }
        if (result == FLS_OUT_OF_INDEXES)
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("FlsAlloc");
        return result;
    }
#endif
    boost::mutex::scoped_lock lock(g_flsMutex());
    std::vector<bool>::iterator it = std::find(g_flsIndices().begin(),
        g_flsIndices().end(), false);
    // TODO: we don't clear out values when freeing, so we can't reuse
    // force new
    it = g_flsIndices().end();
    if (it == g_flsIndices().end()) {
        g_flsIndices().resize(g_flsIndices().size() + 1);
        g_flsIndices()[g_flsIndices().size() - 1] = true;
        return g_flsIndices().size() - 1;
    } else {
        size_t result = it - g_flsIndices().begin();
        g_flsIndices()[result] = true;
        return result;
    }
}

void
Fiber::flsFree(size_t key)
{
#ifdef WINDOWS
    if (!g_doesntHaveOSFLS) {
        if (!pFlsFree((DWORD)key))
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("FlsFree");
        return;
    }
#endif
    boost::mutex::scoped_lock lock(g_flsMutex());
    MORDOR_ASSERT(key < g_flsIndices().size());
    MORDOR_ASSERT(g_flsIndices()[key]);
    if (key + 1 == g_flsIndices().size()) {
        g_flsIndices().resize(key);
    } else {
        // TODO: clear out current values
        g_flsIndices()[key] = false;
    }
}

void
Fiber::flsSet(size_t key, intptr_t value)
{
#ifdef WINDOWS
    if (!g_doesntHaveOSFLS) {
        if (!pFlsSetValue((DWORD)key, (PVOID)value))
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("FlsSetValue");
        return;
    }
#endif
    Fiber::ptr self = Fiber::getThis();
    if (self->m_fls.size() <= key)
        self->m_fls.resize(key + 1);
    self->m_fls[key] = value;
}

intptr_t
Fiber::flsGet(size_t key)
{
#ifdef WINDOWS
    if (!g_doesntHaveOSFLS) {
        error_t error = lastError();
        intptr_t result = (intptr_t)pFlsGetValue((DWORD)key);
        lastError(error);
        return result;
    }
#endif
    Fiber::ptr self = Fiber::getThis();
    if (self->m_fls.size() <= key)
        return 0;
    return self->m_fls[key];
}

std::vector<void *>
Fiber::backtrace()
{
    MORDOR_ASSERT(m_state != EXEC);
    std::vector<void *> result;
    if (m_state != HOLD)
        return result;

#ifdef WINDOWS
    STACKFRAME64 frame;
    DWORD type;
    CONTEXT *context;
#ifdef _M_IX86
    context = (CONTEXT *)((char *)m_sp + 0x14);
    type                   = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = context->Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode   = AddrModeFlat;
    context = NULL;
#elif _M_X64
    context = (CONTEXT *)((char *)m_sp + 0x30);
    CONTEXT dupContext;
    memcpy(&dupContext, context, sizeof(CONTEXT));
    context = &dupContext;
    type                   = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = dupContext.Rip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = dupContext.Rsp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = dupContext.Rsp;
    frame.AddrStack.Mode   = AddrModeFlat;
#else
#error "Unsupported platform"
#endif

    while (result.size() < 64) {
        if (!StackWalk64(type, GetCurrentProcess(), GetCurrentThread(),
            &frame, context, NULL, &SymFunctionTableAccess64,
            &SymGetModuleBase64, NULL)) {
            error_t error = lastError();
            break;
        }
        if (frame.AddrPC.Offset != 0) {
            result.push_back((void *)frame.AddrPC.Offset);
        }
    }
#endif
    return result;
}

std::ostream &operator<<(std::ostream &os, Fiber::State state)
{
    switch (state) {
        case Fiber::INIT:
            return os << "INIT";
        case Fiber::HOLD:
            return os << "HOLD";
        case Fiber::EXEC:
            return os << "EXEC";
        case Fiber::EXCEPT:
            return os << "EXCEPT";
        case Fiber::TERM:
            return os << "TERM";
        default:
            return os << (int)state;
    }
}

}
