#ifndef __MORDOR_IOMANAGER_EPOLL_H__
#define __MORDOR_IOMANAGER_EPOLL_H__
// Copyright (c) 2009 - Mozy, Inc.

#include "iomanager.h"

#ifndef LINUX
#error IOManagerEPoll is Linux only
#endif

namespace Mordor {

class Fiber;

class IOManagerEPoll : public PipeTicklingIOManager
{
private:
    struct AsyncState : boost::noncopyable
    {
        AsyncState();
        ~AsyncState();

        struct EventContext
        {
            EventContext() : scheduler(NULL) {}
            Scheduler *scheduler;
            boost::shared_ptr<Fiber> fiber;
            boost::function<void ()> dg;
        };

        EventContext &contextForEvent(Event event);
        bool triggerEvent(Event event, size_t &pendingEventCount);

        int m_fd;
        EventContext m_in, m_out, m_close;
        Event m_events;
        boost::mutex m_mutex;
    };

public:
    IOManagerEPoll(size_t threads = 1, bool useCaller = true);
    ~IOManagerEPoll();

    void registerEvent(int fd, Event events,
        boost::function<void ()> dg = NULL);
    bool unregisterEvent(int fd, Event events);
    bool cancelEvent(int fd, Event events);

    Implementation implementation() const { return EPOLL; }

protected:
    bool stoppingInternal();
    void idle();

private:
    int m_epfd;
    size_t m_pendingEventCount;
    boost::mutex m_mutex;
    std::vector<AsyncState *> m_pendingEvents;
};

}

#endif
