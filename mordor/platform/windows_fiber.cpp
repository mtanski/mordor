#include "windows_fiber.h"

namespace Mordor {
namespace internal {

__Fiber::__Fiber()
    : m_sp(nullptr)
{
    m_stack = ConvertThreadToFiber(NULL);
}

__Fiber::__Fiber(size_t stacksize)
    : m_stacksize(stacksize)
{
    reset();
}

__Fiber::~__Fiber()
{
    DeleteFiber(m_stack);
}

void __Fiber::reset()
{
    m_stack = m_sp = pCreateFiberEx(0, m_stacksize, 0, &__Fiber::trampoline, this);
}

void __Fiber::switchContext(__Fiber *to)
{
    SwitchToFiber(to->m_stack);
}

VOID CALLBACK __Fiber::trampoline(PVOID lpParameter)
{
    __Fiber* f = (__Fiber*) lpParameter;
    f->entryPoint();
    MORDOR_NOTREACHED();
}

}
}
