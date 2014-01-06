#ifndef __MORDOR_OPENSSL_LOCK_H__
#define __MORDOR_OPENSSL_LOCK_H__

#include <vector>

#ifndef USE_FIBER_MUTEX
namespace boost {
    class mutex;
}
#else
namespace Mordor {
    class FiberMutex;
}
#endif

namespace Mordor {

class OpensslLockManager
{
public:
#ifndef USE_FIBER_MUTEX
    typedef boost::mutex LockType;
#else
    typedef Mordor::FiberMutex LockType;
#endif

    static OpensslLockManager & instance();
    static unsigned long id();
    static void locking_function(int mode, int n, const char *file, int line);

    void installLockCallbacks();
    void uninstallLockCallBacks();

protected:
    typedef std::vector<std::shared_ptr<LockType>> Locks;

    OpensslLockManager();
    ~OpensslLockManager();

    void locking(int mode, int n, const char * file, int line);

private:
    OpensslLockManager(const OpensslLockManager& rhs) = delete;

protected:
    std::vector<std::shared_ptr<LockType>> m_locks;
    bool m_initialized;
};

}

#endif
