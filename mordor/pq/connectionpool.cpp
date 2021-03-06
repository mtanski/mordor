// Copyright (c) 2010 Mozy, Inc.

#include "../predef.h"

#include "connectionpool.h"


#include "../config.h"
#include "../iomanager.h"
#include "../log.h"
#include "../util.h"

#include "connection.h"

namespace Mordor {
namespace PQ {

static Logger::ptr g_logger = Log::lookup("mordor:pq:connectionpool");

ConnectionPool::ConnectionPool(const std::string &conninfo, IOManager *iomanager, size_t num)
    :m_conninfo(conninfo)
    ,m_iomanager(iomanager)
    ,m_mutex()
    ,m_condition(m_mutex)
    ,m_total(num) {
    FiberMutex::ScopedLock lock(m_mutex);
    try {
        for (size_t i = 0; i < m_total; i++) {
            m_freeConnections.push_back(
                std::shared_ptr<Connection>(
                    new Connection(m_conninfo, m_iomanager, 0, false)));
        }
    } catch(...) {
        MORDOR_LOG_ERROR(g_logger)
            << boost::current_exception_diagnostic_information();
        throw;
    }
}

ConnectionPool::~ConnectionPool() {
    FiberMutex::ScopedLock lock(m_mutex);
    while(!m_busyConnections.empty()) {
        m_condition.wait();
    }
}

std::shared_ptr<Connection> ConnectionPool::getConnection() {
    FiberMutex::ScopedLock lock(m_mutex);
    MORDOR_LOG_DEBUG(g_logger) << "Trying to get connection, pool size is "
                               << m_freeConnections.size();
    if (m_freeConnections.empty() && m_busyConnections.size() < m_total) {
        try {
            m_freeConnections.push_back(
                std::shared_ptr<Connection>(
                    new Connection(m_conninfo, m_iomanager, 0, false)));
        } catch(...) {
            MORDOR_LOG_ERROR(g_logger)
                << boost::current_exception_diagnostic_information();
        }
    }
    while (m_freeConnections.empty()) {
        m_condition.wait();
    }
    MORDOR_ASSERT(!m_freeConnections.empty());
    std::shared_ptr<Connection> conn = *m_freeConnections.begin();
    if (conn->status() == CONNECTION_BAD) {
        conn->connect();
    }
    if (conn->status() != CONNECTION_OK) {
        MORDOR_LOG_WARNING(g_logger) << "Connection is bad, try to reset";
        try {
            conn->reset();
        }catch(...) {
            MORDOR_LOG_ERROR(g_logger)
                << boost::current_exception_diagnostic_information();
            throw;
        }
    }
    //The ret is for return value, which has separate counter than
    //the share_ptr stored in m_free/m_busyConnections
    std::shared_ptr<Connection> ret(
        conn.get(),
        std::bind(&ConnectionPool::releaseConnection, this, std::placeholders::_1));
    m_busyConnections.push_back(conn);
    m_freeConnections.erase(m_freeConnections.begin());
    return ret;
}

void ConnectionPool::resize(size_t num) {
    FiberMutex::ScopedLock lock(m_mutex);
    m_total = num;
    while (m_busyConnections.size() + m_freeConnections.size() > m_total &&
           !m_freeConnections.empty()) {
        m_freeConnections.erase(m_freeConnections.begin());
    }
}

void ConnectionPool::releaseConnection(Connection* conn) {
        FiberMutex::ScopedLock lock(m_mutex);
        MORDOR_LOG_DEBUG(g_logger) << "Release connection " << conn;
        std::shared_ptr<Connection> c(conn, &nop<Connection*>);
        std::list<std::shared_ptr<Connection> >::iterator it
            = std::find(m_busyConnections.begin(), m_busyConnections.end(), c);
        MORDOR_ASSERT(it != m_busyConnections.end());
        c = *it; //This line is necessary,
        //or the pointer hold by it will be deleted after the second line

        m_busyConnections.erase(it);
        if (m_busyConnections.size() + m_freeConnections.size() < m_total) {
            m_freeConnections.push_front(c);
            m_condition.signal();
        }
        MORDOR_LOG_DEBUG(g_logger) << "Free connections "
                                   << m_freeConnections.size();
}

void
associateConnectionPoolWithConfigVar(ConnectionPool &pool,
    ConfigVar<size_t>::ptr configVar)
{
  configVar->onChange.connect(std::bind(&ConnectionPool::resize, &pool, std::placeholders::_1));
  pool.resize(configVar->val());
}

}}
