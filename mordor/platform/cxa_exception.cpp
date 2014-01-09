#include "../assert.h"
#include "cxa_exception.h"

#ifdef CXXABIV1_EXCEPTION

// exception handling implemented by GCC and Clang conform to this ABI.
// both implementation store the exception stack to a thread local storage
// variable and swapcontext(3) does not care about pthread's TLS.
namespace __cxxabiv1 {
extern "C" __cxa_eh_globals * __cxa_get_globals() throw();
}

namespace Mordor {
namespace internal {

ExceptionStack::ExceptionStack()
{
    memset(&m_eh, 0, sizeof(m_eh));
}


void
ExceptionStack::swap(ExceptionStack &rhs)
{
    __cxxabiv1::__cxa_eh_globals *eh = __cxxabiv1::__cxa_get_globals();
    MORDOR_ASSERT(eh);
    m_eh = *eh;
    *eh = rhs.m_eh;
}

}
}

#endif //CXXABIV1_EXCEPTION
