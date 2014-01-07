// Copyright (c) 2010 - Mozy, Inc.

#include "assert.h"
#include "fiber.h"
#include "scheduler.h"
#include "thread.h"

#ifdef LINUX
#include <sys/prctl.h>
#include <syscall.h>
#elif defined(WINDOWS)
#include <process.h>
#elif defined (OSX)
#include <mach/mach_init.h>
#endif

#include "exception.h"

namespace Mordor {

#ifdef WINDOWS
//
// Usage: SetThreadName (-1, "MainThread");
//
#define MS_VC_EXCEPTION 0x406D1388

namespace {
typedef struct tagTHREADNAME_INFO
{
   DWORD dwType; // Must be 0x1000.
   LPCSTR szName; // Pointer to name (in user addr space).
   DWORD dwThreadID; // Thread ID (-1=caller thread).
   DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
}

static void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName)
{
   THREADNAME_INFO info;
   info.dwType = 0x1000;
   info.szName = szThreadName;
   info.dwThreadID = dwThreadID;
   info.dwFlags = 0;

   __try
   {
      RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
   }
   __except(EXCEPTION_CONTINUE_EXECUTION)
   {
   }
}
#endif

Thread::Bookmark::Bookmark()
    : m_scheduler(Scheduler::getThis())
    , m_tid(std::this_thread::get_id())
{}

void
Thread::Bookmark::switchTo()
{
    if (std::this_thread::get_id() == m_tid)
        return;

    MORDOR_ASSERT(m_scheduler);
    m_scheduler->schedule(Fiber::getThis(), m_tid);
    Scheduler::yieldTo();
    MORDOR_ASSERT(m_tid == std::this_thread::get_id());
}

// The object of this function is to start a new thread running dg, and store
// that thread's id in m_tid.  Each platform is a little bit different, but
// there are similarities in different areas.  The areas are:
//
// * How to start a thread
//   Windows - _beginthreadex (essentially CreateThread, but keeps the CRT
//             happy)
//   Everything else - pthread_create
//
// * How to get the native thread id (we like the native thread id because it
//   makes it much easier to correlate with a debugger or performance tool,
//   instead of some random artificial thread id)
//   Linux - there is no documented way to query a pthread for its tid, so we
//           have to have the thread itself call std::this_thread::get_id(), and report it back
//           to the constructor.  This means that a) the constructor will not
//           return until the thread is actually running; and b) the thread
//           neads a pointer back to the Thread object to store the tid
//   Windows - happily returns the thread id as part of _beginthreadex
//   OS X - Has a documented, non-portable function to query the
//          mach_thread_port from the pthread
//   Everything else - dunno, it's not special cased, so just use the pthread_t
//                     as the thread id
//
//   In all cases except for Linux, the thread itself doesn't need to know
//   about the Thread object, just which dg to execute.  Because we just fire
//   off the thread and return immediately, it's perfectly possible for the
//   constructor and destructor to run before the thread even starts, so move
//   dg onto the heap in newly allocated memory, and pass the thread start
//   function the pointer to the dg.  It will move the dg onto it's local
//   stack, deallocate the memory on the heap, and then call dg.
//   For Linux, because we need to have the thread tell us its own id, we
//   instead know that the constructor cannot return until the thread has
//   started, so simply pass a pointer to this to the thread, which will
//   set the tid in the object, copy the dg that is a member field onto the
//   stack, signal the constructor that it's ready to go, and then call dg
Thread::Thread(std::function<void ()> dg, const std::string& name)
    : m_thread(&Thread::run, this, name, dg)
{

}

Thread::~Thread()
{
    if (m_thread.joinable())
        m_thread.detach();
}

void Thread::run(const std::string& name, std::function<void ()> dg)
{
    if (name.size()) {
#if defined WINDOWS
        auto handle = m_thread.native_handle();
        DWORD tid = GetThreadId(handle);
        SetThreadName(tid, pContext->name.c_str());
#elif defined LINUX
        pthread_setname_np(m_thread.native_handle(), name.c_str());
#elif defined __MAC_10_6
        pthread_setname_np(name.c_str());
#endif
    }

    return dg();
}

}
