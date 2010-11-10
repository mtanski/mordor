// Copyright (c) 2010 - Mozy, Inc.

#include "mordor/iomanager.h"

#include "mordor/assert.h"
#include "mordor/log.h"

namespace Mordor {

static Logger::ptr g_log = Log::lookup("mordor:iomanager");

IOManager *IOManager::create(size_t threads, bool useCaller)
{
#ifdef WINDOWS
    return new IOManagerIOCP(threads, useCaller);
#elif defined (LINUX)
    return new IOManagerEPoll(threads, useCaller);
#elif defined (BSD)
    return new IOManagerKQueue(threads, useCaller);
#else
    return new IOManagerPoll(threads, useCaller)
#endif
}

PipeTicklingIOManager::PipeTicklingIOManager(size_t threads, bool useCaller)
    : IOManager(threads, useCaller)
{
#ifndef WINDOWS
    int rc = pipe(m_tickleFds);
    MORDOR_LOG_LEVEL(g_log, rc ? Log::ERROR : Log::VERBOSE) << this << " pipe(): "
        << rc << " (" << errno << ")";
    if (rc)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("pipe");
    MORDOR_ASSERT(m_tickleFds[0] > 0);
    MORDOR_ASSERT(m_tickleFds[1] > 0);
#endif
}

PipeTicklingIOManager::~PipeTicklingIOManager()
{
#ifndef WINDOWS
    close(m_tickleFds[0]);
    MORDOR_LOG_VERBOSE(g_log) << this << " close(" << m_tickleFds[0] << ")";
    close(m_tickleFds[1]);
    MORDOR_LOG_VERBOSE(g_log) << this << " close(" << m_tickleFds[1] << ")";
#endif
}

void
PipeTicklingIOManager::tickle()
{
#ifndef WINDOWS
    int rc = write(m_tickleFds[1], "T", 1);
    MORDOR_LOG_VERBOSE(g_log) << this << " write(" << m_tickleFds[1] << ", 1): "
        << rc << " (" << errno << ")";
    MORDOR_VERIFY(rc == 1);
#endif
}

void
PipeTicklingIOManager::consumeTickle()
{
#ifndef WINDOWS
    unsigned char dummy;
    int rc2 = read(m_tickleFds[0], &dummy, 1);
    MORDOR_VERIFY(rc2 == 1);
    MORDOR_VERIFY(dummy == 'T');
    MORDOR_LOG_VERBOSE(g_log) << this << " received tickle";
#endif
}

}
