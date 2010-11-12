// Copyright (c) 2010 - Mozy, Inc.

#include "iomanager_poll.h"

namespace Mordor {

IOManagerPoll::IOManagerPoll(size_t threads, bool useCaller)
    : PipeTicklingIOManager(threads, useCaller)
{
}

void
IOManagerPoll::registerEvent(int fd, Event event, boost::function<void ()> dg)
{
    WSAPOLLFD pollfd;
    pollfd.
}

#ifdef WINDOWS
#define poll    WSAPoll
#endif
#define pollStr #poll

void
IOManagerPoll::idle()
{
    std::vector<WSA(POLLFD)> localEvents;
    while (true) {
        unsigned long long nextTimeout;
        if (stopping(nextTimeout))
            return;
        int rc = -1;
        lastError(WSA(EINTR));
        while (rc < 0 && lastError() == WSA(EINTR)) {
            timeout = NULL;
            if (nextTimeout != ~0ull)
                timeout = 1;
            rc =
#ifdef WINDOWS
                WSAPoll(
#else
                poll(
#endif
                    localEvents.data(), localEvents.size(), timeout);
            if (rc < 0 && lastError() == WSA(EINTR))
                nextTimeout = nextTimer();
        }
        if (rc < 0)
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API(
#ifdef WINDOWS
            "WSAPoll"
#else
            "poll"
#endif
            );

    }


}


}
