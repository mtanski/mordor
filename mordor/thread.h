#ifndef __MORDOR_THREAD_H__
#define __MORDOR_THREAD_H__
// Copyright (c) 2010 - Mozy, Inc.

// C++
#include <iosfwd>
#include <mutex>
#include <thread>

#include "version.h"

namespace Mordor {

class Scheduler;

class Thread
{

public:
    /// thread bookmark
    ///
    /// bookmark current thread id and scheduler, allow to switch back to
    /// the same thread id later.
    /// 
    /// @pre The process must be running with available scheduler, otherwise
    /// it is not possible to switch execution between threads with bookmark.
    /// 
    /// @note Bookmark was designed to address the issue where we failed to
    /// rethrow an exception in catch block, because GCC C++ runtime saves the
    /// exception stack in a pthread TLS variable. and swapcontext(3) does not
    /// take care of TLS. but developer needs to be more aware of underlying
    /// thread using thread bookmark, so we developed another way to fix this
    /// problem. thus bookmark only serve as a way which allow user to stick
    /// to a native thread.
    class Bookmark
    {
    public:
        Bookmark();
        /// switch to bookmark's tid
        void switchTo();
        /// bookmark's tid
        std::thread::id tid() const { return m_tid; }

    private:
        Scheduler *m_scheduler;
        std::thread::id m_tid;
    };

public:
    Thread(std::function<void ()> dg, const std::string& name = "");
    ~Thread();

    std::thread::id tid() const { return m_thread.get_id(); }

    void join() { m_thread.join(); }

private:
    Thread(const Thread& rhs) = delete;
    void run(const std::string& name, std::function<void ()> dg);

private:
    std::thread m_thread;
};

}

#endif
