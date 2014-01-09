#include <unistd.h>

#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#include <setjmp.h>

#include "../exception.h"
#include "unix_fiber.h"

// On Linux Forify doesn't let us switch stacks (it checks for lower adress)
#if __GNUC_PREREQ (2,11)
#ifdef __LP64__
__asm__(".symver __longjmp_chk,longjmp@GLIBC_2.2.5");
#else
__asm__(".symver __longjmp_chk,longjmp@GLIBC_2.0");
#endif
#endif

namespace Mordor {
namespace internal {

__Fiber::__Fiber()
    : m_init(true),
      m_stack(nullptr)
{ }

__Fiber::__Fiber(size_t stacksize)
    : m_stacksize(stacksize)
{
    if ((m_stack = malloc(m_stacksize)) == nullptr)
        throw std::bad_alloc();

#ifdef HAVE_VALGRIND_VALGRIND_H
    m_valgrind_stackid = VALGRIND_STACK_REGISTER(m_stack, (char *)m_stack + m_stacksize);
#endif

    reset();
}

__Fiber::~__Fiber()
{
#ifdef HAVE_VALGRIND_VALGRIND_H
    VALGRIND_STACK_DEREGISTER(m_valgrind_stackid);
#endif
    free(m_stack);
}

void __Fiber::reset()
{
    ucontext_t tmp;
    m_init = false;

    if (getcontext(&m_ctx) == -1)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("getcontext");

    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    // Save tmp, so we can jump back from __Fiber::trampoline()
    m_ctx.uc_link = &tmp; 

    // Jump to __Fiber::trampoline() and return right before calling entrypoint
    makecontext(&m_ctx, (void(*)()) __Fiber::trampoline, 1, this);
}

void __Fiber::switchContext(__Fiber *to)
{
    if (!_setjmp(m_env)) {

#ifdef CXXABIV1_EXCEPTION
        m_eh.swap(to->m_eh);
#endif

        // First switch into the thread uses stack prepared by makecontext.
        // After that we can use setjmp / longjmp for subsequent calls.
        if (to->m_init == false) {
            setcontext(&to->m_ctx);
        } else {
            _longjmp(to->m_env, 1);
        }
    }
}

void __Fiber::trampoline(void *ptr)
{
    __Fiber* f = (__Fiber*) ptr;

    f->m_init = true;
    f->entryPoint();
}

}
}
