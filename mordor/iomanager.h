#ifndef __MORDOR_IOMANAGER_H__
#define __MORDOR_IOMANAGER_H__
// Copyright (c) 2009 - Mozy, Inc.

#include "timerscheduler.h"
#include "version.h"

namespace Mordor {

class IOManager : public TimerScheduler
{
public:
    enum Event {
        READ,
        WRITE,
        /// CLOSE may not be supported, and will be silently discarded
        CLOSE
    };

    enum Implementation
    {
        UNKNOWN,
        SELECT,
        POLL,
        EPOLL,
        IOCP,
        KQUEUE
    };

public:
    IOManager(size_t threads = 1, bool useCaller = true)
        : TimerScheduler(threads, useCaller)
    {}

    // Create the most appropriate IOManager for the current platform
    static IOManager *create(size_t threads = 1, bool useCaller = true);

    virtual void registerEvent(int fd, Event event, boost::function<void ()> dg = NULL) = 0;
    /// Will not cause the event to fire
    /// @return If the event was successfully unregistered before firing normally
    virtual bool unregisterEvent(int fd, Event event) = 0;
    /// Will cause the event to fire
    virtual bool cancelEvent(int fd, Event event) = 0;

    /// This is to avoid costly dynamic_casts; check this value, and then do a
    /// static_cast.
    virtual Implementation implementation() const { return UNKNOWN; }
};

// Helper class for common functionality (tickling via a pipe)
class PipeTicklingIOManager : public IOManager
{
public:
    PipeTicklingIOManager(size_t threads = 1, bool useCaller = true);
    ~PipeTicklingIOManager();

protected:
    void tickle();
    int tickleFd() { return m_tickleFds[0]; }
    void consumeTickle();

private:
    int m_tickleFds[2];
};

};

#ifdef WINDOWS
#include "iomanager_iocp.h"
#elif defined(LINUX)
#include "iomanager_epoll.h"
#elif defined(BSD)
#include "iomanager_kqueue.h"
#endif

#endif
