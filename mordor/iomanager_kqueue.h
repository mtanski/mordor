#ifndef __MORDOR_IOMANAGER_EPOLL_H__
#define __MORDOR_IOMANAGER_EPOLL_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <sys/types.h>
#include <sys/event.h>

#include <map>

#include "iomanager.h"

#ifndef BSD
#error IOManagerKQueue is BSD only
#endif

namespace Mordor {

class IOManagerKQueue : public PipeTicklingIOManager
{
private:
    struct AsyncEvent
    {
        struct kevent event;

        Scheduler *m_scheduler, *m_schedulerClose;
        boost::shared_ptr<Fiber> m_fiber, m_fiberClose;
        boost::function<void ()> m_dg, m_dgClose;

        bool operator<(const AsyncEvent &rhs) const
        { if (event.ident < rhs.event.ident) return true; return event.filter < rhs.event.filter; }
    };

public:
    IOManagerKQueue(size_t threads = 1, bool useCaller = true);
    ~IOManagerKQueue();

    void registerEvent(int fd, Event events, boost::function<void ()> dg = NULL);
    bool unregisterEvent(int fd, Event events);
    bool cancelEvent(int fd, Event events);

    Implementation implementation() const { return KQUEUE; }

protected:
    bool stoppingInternal();
    void idle();

private:
    int m_kqfd;
    std::map<std::pair<int, Event>, AsyncEvent> m_pendingEvents;
    boost::mutex m_mutex;
};

}

#endif

