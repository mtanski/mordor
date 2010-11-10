#ifndef __MORDOR_IOMANAGER_IOCP_H__
#define __MORDOR_IOMANAGER_IOCP_H__

#include <map>

#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>

#include "iomanager.h"
#include "version.h"

#ifndef WINDOWS
#error IOManagerIOCP is Windows only
#endif

namespace Mordor {

class Fiber;

/// @brief IOManager using I/O Completion Ports
///
/// IOManagerIOCP offers additional functionality over a generic IOManager,
/// including I/O Completion Port style asynchronous I/O, and notification of
/// any HANDLE being signalled
class IOManagerIOCP : public IOManager
{
    friend class WaitBlock;
public:
    struct AsyncEvent
    {
        AsyncEvent();

        OVERLAPPED overlapped;

        Scheduler  *m_scheduler;
        tid_t m_thread;
        boost::shared_ptr<Fiber> m_fiber;
    };

private:
    class WaitBlock : public boost::enable_shared_from_this<WaitBlock>
    {
    public:
        typedef boost::shared_ptr<WaitBlock> ptr;
    public:
        WaitBlock(IOManagerIOCP &outer);
        ~WaitBlock();

        bool registerEvent(HANDLE handle, boost::function<void ()> dg,
            bool recurring);
        size_t unregisterEvent(HANDLE handle);

    private:
        void run();
        void removeEntry(int index);

    private:
        boost::mutex m_mutex;
        IOManagerIOCP &m_outer;
        HANDLE m_reconfigured;
        HANDLE m_handles[MAXIMUM_WAIT_OBJECTS];
        Scheduler *m_schedulers[MAXIMUM_WAIT_OBJECTS];
        boost::shared_ptr<Fiber> m_fibers[MAXIMUM_WAIT_OBJECTS];
        boost::function<void ()> m_dgs[MAXIMUM_WAIT_OBJECTS];
        bool m_recurring[MAXIMUM_WAIT_OBJECTS];
        int m_inUseCount;
    };

public:
    IOManagerIOCP(size_t threads = 1, bool useCaller = true);
    ~IOManagerIOCP();

    void registerEvent(int fd, Event event, boost::function<void ()> dg = NULL);
    bool unregisterEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);

    Implementation implementation() const { return IOCP; }

    void registerFile(HANDLE handle);
    void registerEvent(AsyncEvent *e);
    // Only use if the async call failed, not for cancelling it
    void unregisterEvent(AsyncEvent *e);
    void registerEvent(HANDLE handle, boost::function<void ()> dg,
        bool recurring = false);
    void registerEvent(HANDLE handle, bool recurring = false)
    { registerEvent(handle, NULL, recurring); }
    size_t unregisterEvent(HANDLE handle);
    void cancelEvent(HANDLE hFile, AsyncEvent *e);

protected:
    bool stoppingInternal();
    void idle();
    void tickle();

private:
    void ensurePosixStyleIOManager();

private:
    HANDLE m_hCompletionPort;
#ifdef DEBUG
    std::map<OVERLAPPED *, AsyncEvent*> m_pendingEvents;
#endif
    size_t m_pendingEventCount;
    boost::mutex m_mutex;
    std::list<WaitBlock::ptr> m_waitBlocks;
    IOManager *m_posixStyleIOManager;
};

}

#endif
