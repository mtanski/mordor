#ifndef __MORDOR_PLATFORM_WINDOWS_FIBER_H__
#define __MORDOR_PLATFORM_WINDOWS_FIBER_H__

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
    { return m_sp; }

    void* stack_ptr()
    { return m_stack; }

private:
    __Fiber(const __Fiber& rhs) = delete;

    static VOID CALLBACK trampoline(PVOID lpParameter);

private:
    void *m_stack;
    void *m_sp;
    size_t m_stacksize;

};

}
}

#endif // __MORDOR_PLATFORM_WINDOWS_FIBER_H__
