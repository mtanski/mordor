#ifndef __MORDOR_PIPE_STREAM_H__
#define __MORDOR_PIPE_STREAM_H__
// Copyright (c) 2009 - Mozy, Inc.

#include <utility>

#include <boost/shared_ptr.hpp>

#ifdef WINDOWS
#include "handle.h"
#else
#include "fd.h"
#endif

namespace Mordor {

class IOManager;

/// Create a user-space only, full-duplex anonymous pipe
std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t bufferSize = ~0);

/// Create a kernel-level, half-duplex anonymous pipe
///
/// The Streams created by this function will have a file handle, and are
/// suitable for usage with native OS APIs, especially popen
std::pair<NativeStream::ptr, NativeStream::ptr>
    anonymousPipe(IOManager *ioManager = NULL);


#ifdef WINDOWS
typedef HANDLE pid_handle_t;
#else
typedef pid_t pid_handle_t;
#endif

struct Process
{
    typedef boost::shared_ptr<Process> ptr;

    Process(pid_handle_t _pid) : pid(_pid)
    {}
    ~Process();

    void wait();

    pid_handle_t pid;
};

/// Launch a process
Process::ptr popen(const char *filename,
    char * const argv[],
    char * const envp[],
    NativeStream::ptr in = NativeStream::ptr(),
    NativeStream::ptr out = NativeStream::ptr(),
    NativeStream::ptr err = NativeStream::ptr());

enum OpenMode
{
    READ,
    WRITE
};

enum StderrMode
{
    DROP, /// stderr is sent to /dev/null
    REDIRECT, /// stderr is sent to stdout
    INHERIT /// stderr is inherited from the current process
};

Stream::ptr popen(const char *filename, char * const argv[] = NULL,
    OpenMode openMode = READ, StderrMode stderrMode = DROP,
    char * const envp[] = NULL);

#ifdef WINDOWS
Process::ptr popen(const wchar_t *filename,
    wchar_t * const argv[],
    wchar_t * const envp[],
    NativeStream::ptr in = NativeStream::ptr(),
    NativeStream::ptr out = NativeStream::ptr(),
    NativeStream::ptr err = NativeStream::ptr());

Stream::ptr popen(const wchar_t *filename, wchar_t * const argv[] = NULL,
    OpenMode openMode = READ, StderrMode stderrMode = DROP,
    wchar_t * const envp[] = NULL);
#endif

}

#endif
