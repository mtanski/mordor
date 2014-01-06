#ifndef __MORDOR_NOTIFY_STREAM_H__
#define __MORDOR_NOTIFY_STREAM_H__
// Copyright (c) 2009 - Mozy, Inc.

#include "filter.h"

namespace Mordor {

class NotifyStream : public FilterStream
{
public:
    typedef std::shared_ptr<NotifyStream> ptr;
public:
    NotifyStream(Stream::ptr parent, bool own = true)
        : FilterStream(parent, own)
    {}

    std::function<void ()> notifyOnFlush;
    std::function<void ()> notifyOnEof;
    std::function<void ()> notifyOnException;

    void close(CloseType type = BOTH)
    {
        try {
            if (ownsParent())
                parent()->close(type);
        } catch (...) {
            if (notifyOnException)
                notifyOnException();
            throw;
        }
        if (m_notifyOnClose)
            m_notifyOnClose(type);
    }

    using FilterStream::read;
    size_t read(Buffer &b, size_t len)
    {
        size_t result;
        try {
            result = parent()->read(b, len);
        } catch(...) {
            if (notifyOnException)
                notifyOnException();
            throw;
        }
        if (result == 0 && notifyOnEof)
            notifyOnEof();
        return result;
    }

    using FilterStream::write;
    size_t write(const Buffer &b, size_t len)
    {
        try {
            return parent()->write(b, len);
        } catch(...) {
            if (notifyOnException)
                notifyOnException();
            throw;
        }
    }

    void flush(bool flushParent = true)
    {
        try {
            parent()->flush(flushParent);
        } catch(...) {
            if (notifyOnException)
                notifyOnException();
            throw;
        }
        if (notifyOnFlush)
            notifyOnFlush();
    }

    void notifyOnClose(std::function<void ()> dg = NULL)
    {
        if (dg)
            notifyOnClose2(std::bind(&NotifyStream::onCloseAdapter, dg, std::placeholders::_1));
        else
            notifyOnClose2(NULL);
    }

    void notifyOnClose2(std::function<void (CloseType)> dg)
    { m_notifyOnClose = dg; }

private:
    static void onCloseAdapter(std::function<void ()> dg, CloseType type)
    { dg(); }

private:
    std::function<void (CloseType)> m_notifyOnClose;

};

}

#endif
