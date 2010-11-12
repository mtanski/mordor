#ifndef __MORDOR_IOMANAGER_POLL_H__
#define __MORDOR_IOMANAGER_POLL_H__
// Copyright (c) 2010 - Mozy, Inc.

#include <boost/thread/mutex.hpp>

#include "iomanager.h"

#ifdef WINDOWS
#define WSA(error) WSA ## error
#else
#define WSA(error) error
#endif

namespace Mordor {

class IOManagerPoll : public PipeTicklingIOManager
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
        EventContext m_in, m_out;
        Event m_events;
    };
public:
    IOManagerPoll(size_t threads = 1, bool useCaller = true);
    ~IOManagerPoll();

    void registerEvent(int fd, Event event, boost::function<void ()> dg = NULL);
    bool unregisterEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);

protected:
    bool stoppingInternal();
    void idle();
    void tickle();

private:
    boost::mutex m_mutex;
    std::vector<WSA(POLLFD)> m_pendingEvents;
    std::vector<AsyncState *> m_events;
};

}

#endif
