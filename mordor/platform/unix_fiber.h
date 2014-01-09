#ifndef __MORDOR_PLATFORM_UNIX_FIBER_H__
#define __MORDOR_PLATFORM_UNIX_FIBER_H__

#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#include <setjmp.h>

// *context() for initial context creation
#ifdef __APPLE__
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

// Register stack with valgrind (for tracking)
#ifdef HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#include "cxa_exception.h"

namespace Mordor {
namespace internal {

class __Fiber
{
protected:
    /// Capture current context
    __Fiber();
    /// Normal constructor (with entry point transfer)
    __Fiber(size_t stacksize);
    ~__Fiber();

    /// Transfer control to child class
    virtual void entryPoint() = 0;

    void reset();
    void switchContext(class __Fiber *to);

    void* stackid()
    { return &m_ctx; }

    void* stack_ptr()
    { return m_stack; }

private:
    __Fiber(const __Fiber& rhs) = delete;

    static void trampoline(void* ptr);

private:
    bool m_init;
#ifdef HAVE_VALGRIND_VALGRIND_H
    int m_valgrind_stackid;
#endif
    void *m_stack;
    size_t m_stacksize;

    union {
        ucontext_t m_ctx;
        jmp_buf m_env;
    };

#ifdef CXXABIV1_EXCEPTION
    /// Exceptions Stack is stores in TLS, we need to handle moving between threads
    ExceptionStack m_eh;
#endif
};

}
}

#endif // __MORDOR_PLATFORM_UNIX_FIBER_H__
