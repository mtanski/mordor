// Copyright (c) 2009 - Mozy, Inc.

#include "pipe.h"

#include <boost/thread/mutex.hpp>

#include "buffer.h"
#include "file.h"
#include "mordor/assert.h"
#include "mordor/fiber.h"
#include "mordor/scheduler.h"
#include "stream.h"

#ifdef OSX
#include <crt_externs.h>
#endif

namespace Mordor {

static Logger::ptr g_log = Log::lookup("mordor:streams:pipe");

class PipeStream : public Stream
{
    friend std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t);
public:
    typedef boost::shared_ptr<PipeStream> ptr;
    typedef boost::weak_ptr<PipeStream> weak_ptr;

public:
    PipeStream(size_t bufferSize);
    ~PipeStream();

    bool supportsHalfClose() { return true; }
    bool supportsRead() { return true; }
    bool supportsWrite() { return true; }

    void close(CloseType type = BOTH);
    size_t read(Buffer &b, size_t len);
    void cancelRead();
    size_t write(const Buffer &b, size_t len);
    void cancelWrite();
    void flush(bool flushParent = true);

    boost::signals2::connection onRemoteClose(
        const boost::signals2::slot<void ()> &slot);

private:
    PipeStream::weak_ptr m_otherStream;
    boost::shared_ptr<boost::mutex> m_mutex;
    Buffer m_readBuffer;
    size_t m_bufferSize;
    bool m_cancelledRead, m_cancelledWrite;
    CloseType m_closed, m_otherClosed;
    Scheduler *m_pendingWriterScheduler, *m_pendingReaderScheduler;
    boost::shared_ptr<Fiber> m_pendingWriter, m_pendingReader;
    boost::signals2::signal<void ()> m_onRemoteClose;
};

std::pair<Stream::ptr, Stream::ptr> pipeStream(size_t bufferSize)
{
    if (bufferSize == ~0u)
        bufferSize = 65536;
    std::pair<PipeStream::ptr, PipeStream::ptr> result;
    result.first.reset(new PipeStream(bufferSize));
    result.second.reset(new PipeStream(bufferSize));
    MORDOR_LOG_VERBOSE(g_log) << "pipeStream(" << bufferSize << "): {"
        << result.first << ", " << result.second << "}";
    result.first->m_otherStream = result.second;
    result.second->m_otherStream = result.first;
    result.first->m_mutex.reset(new boost::mutex());
    result.second->m_mutex = result.first->m_mutex;
    return result;
}

PipeStream::PipeStream(size_t bufferSize)
: m_bufferSize(bufferSize),
  m_cancelledRead(false),
  m_cancelledWrite(false),
  m_closed(NONE),
  m_otherClosed(NONE),
  m_pendingWriterScheduler(NULL),
  m_pendingReaderScheduler(NULL)
{}

PipeStream::~PipeStream()
{
    MORDOR_LOG_VERBOSE(g_log) << this << " destructing";
    boost::mutex::scoped_lock lock(*m_mutex);
    PipeStream::ptr otherStream = m_otherStream.lock();
    if (otherStream) {
        MORDOR_ASSERT(!otherStream->m_pendingReader);
        MORDOR_ASSERT(!otherStream->m_pendingReaderScheduler);
        MORDOR_ASSERT(!otherStream->m_pendingWriter);
        MORDOR_ASSERT(!otherStream->m_pendingWriterScheduler);
        if (!m_readBuffer.readAvailable())
            otherStream->m_otherClosed = (CloseType)(otherStream->m_otherClosed | READ);
        else
            otherStream->m_otherClosed = (CloseType)(otherStream->m_otherClosed & ~READ);
        otherStream->m_onRemoteClose();
    }
    if (m_pendingReader) {
        MORDOR_ASSERT(m_pendingReaderScheduler);
        MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling read";
        m_pendingReaderScheduler->schedule(m_pendingReader);
        m_pendingReader.reset();
        m_pendingReaderScheduler = NULL;
    }
    if (m_pendingWriter) {
        MORDOR_ASSERT(m_pendingWriterScheduler);
        MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling write";
        m_pendingWriterScheduler->schedule(m_pendingWriter);
        m_pendingWriter.reset();
        m_pendingWriterScheduler = NULL;
    }
}

void
PipeStream::close(CloseType type)
{
    boost::mutex::scoped_lock lock(*m_mutex);
    bool closeWriteFirstTime = !(m_closed & WRITE) && (type & WRITE);
    m_closed = (CloseType)(m_closed | type);
    PipeStream::ptr otherStream = m_otherStream.lock();
    if (otherStream) {
        otherStream->m_otherClosed = m_closed;
        if (closeWriteFirstTime)
            otherStream->m_onRemoteClose();
    }
    if (m_pendingReader && (m_closed & WRITE)) {
        MORDOR_ASSERT(m_pendingReaderScheduler);
        MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling read";
        m_pendingReaderScheduler->schedule(m_pendingReader);
        m_pendingReader.reset();
        m_pendingReaderScheduler = NULL;
    }
    if (m_pendingWriter && (m_closed & READ)) {
        MORDOR_ASSERT(m_pendingWriterScheduler);
        MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling write";
        m_pendingWriterScheduler->schedule(m_pendingWriter);
        m_pendingWriter.reset();
        m_pendingWriterScheduler = NULL;
    }
}

size_t
PipeStream::read(Buffer &b, size_t len)
{
    MORDOR_ASSERT(len != 0);
    while (true) {
        {
            boost::mutex::scoped_lock lock(*m_mutex);
            if (m_closed & READ)
                MORDOR_THROW_EXCEPTION(BrokenPipeException());
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (!otherStream && !(m_otherClosed & WRITE))
                MORDOR_THROW_EXCEPTION(BrokenPipeException());
            size_t avail = m_readBuffer.readAvailable();
            if (avail > 0) {
                size_t todo = (std::min)(len, avail);
                b.copyIn(m_readBuffer, todo);
                m_readBuffer.consume(todo);
                if (m_pendingWriter) {
                    MORDOR_ASSERT(m_pendingWriterScheduler);
                    MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling write";
                    m_pendingWriterScheduler->schedule(m_pendingWriter);
                    m_pendingWriter.reset();
                    m_pendingWriterScheduler = NULL;
                }
                MORDOR_LOG_TRACE(g_log) << this << " read(" << len << "): "
                    << todo;
                return todo;
            }

            if (m_otherClosed & WRITE) {
                MORDOR_LOG_TRACE(g_log) << this << " read(" << len << "): "
                    << 0;
                return 0;
            }

            if (m_cancelledRead)
                MORDOR_THROW_EXCEPTION(OperationAbortedException());

            // Wait for the other stream to schedule us
            MORDOR_ASSERT(!otherStream->m_pendingReader);
            MORDOR_ASSERT(!otherStream->m_pendingReaderScheduler);
            MORDOR_LOG_DEBUG(g_log) << this << " waiting to read";
            otherStream->m_pendingReader = Fiber::getThis();
            otherStream->m_pendingReaderScheduler = Scheduler::getThis();
        }
        try {
            Scheduler::yieldTo();
        } catch (...) {
            boost::mutex::scoped_lock lock(*m_mutex);
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (otherStream && otherStream->m_pendingReader == Fiber::getThis()) {
                MORDOR_ASSERT(otherStream->m_pendingReaderScheduler == Scheduler::getThis());
                otherStream->m_pendingReader.reset();
                otherStream->m_pendingReaderScheduler = NULL;
            }
            throw;
        }
    }
}

void
PipeStream::cancelRead()
{
    boost::mutex::scoped_lock lock(*m_mutex);
    m_cancelledRead = true;
    PipeStream::ptr otherStream = m_otherStream.lock();
    if (otherStream && otherStream->m_pendingReader) {
        MORDOR_ASSERT(otherStream->m_pendingReaderScheduler);
        MORDOR_LOG_DEBUG(g_log) << this << " cancelling read";
        otherStream->m_pendingReaderScheduler->schedule(otherStream->m_pendingReader);
        otherStream->m_pendingReader.reset();
        otherStream->m_pendingReaderScheduler = NULL;
    }
}

size_t
PipeStream::write(const Buffer &b, size_t len)
{
    MORDOR_ASSERT(len != 0);
    while (true) {
        {
            boost::mutex::scoped_lock lock(*m_mutex);
            if (m_closed & WRITE)
                MORDOR_THROW_EXCEPTION(BrokenPipeException());
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (!otherStream || (otherStream->m_closed & READ))
                MORDOR_THROW_EXCEPTION(BrokenPipeException());

            size_t available = otherStream->m_readBuffer.readAvailable();
            size_t todo = (std::min)(m_bufferSize - available, len);
            if (todo != 0) {
                otherStream->m_readBuffer.copyIn(b, todo);
                if (m_pendingReader) {
                    MORDOR_ASSERT(m_pendingReaderScheduler);
                    MORDOR_LOG_DEBUG(g_log) << otherStream << " scheduling read";
                    m_pendingReaderScheduler->schedule(m_pendingReader);
                    m_pendingReader.reset();
                    m_pendingReaderScheduler = NULL;
                }
                MORDOR_LOG_TRACE(g_log) << this << " write(" << len << "): "
                    << todo;
                return todo;
            }

            if (m_cancelledWrite)
                MORDOR_THROW_EXCEPTION(OperationAbortedException());

            // Wait for the other stream to schedule us
            MORDOR_ASSERT(!otherStream->m_pendingWriter);
            MORDOR_ASSERT(!otherStream->m_pendingWriterScheduler);
            MORDOR_LOG_DEBUG(g_log) << this << " waiting to write";
            otherStream->m_pendingWriter = Fiber::getThis();
            otherStream->m_pendingWriterScheduler = Scheduler::getThis();
        }
        try {
            Scheduler::yieldTo();
        } catch (...) {
            boost::mutex::scoped_lock lock(*m_mutex);
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (otherStream && otherStream->m_pendingWriter == Fiber::getThis()) {
                MORDOR_ASSERT(otherStream->m_pendingWriterScheduler == Scheduler::getThis());
                otherStream->m_pendingWriter.reset();
                otherStream->m_pendingWriterScheduler = NULL;
            }
            throw;
        }
    }
}

void
PipeStream::cancelWrite()
{
    boost::mutex::scoped_lock lock(*m_mutex);
    m_cancelledWrite = true;
    PipeStream::ptr otherStream = m_otherStream.lock();
    if (otherStream && otherStream->m_pendingWriter) {
        MORDOR_ASSERT(otherStream->m_pendingWriterScheduler);
        MORDOR_LOG_DEBUG(g_log) << this << " cancelling write";
        otherStream->m_pendingWriterScheduler->schedule(otherStream->m_pendingWriter);
        otherStream->m_pendingWriter.reset();
        otherStream->m_pendingWriterScheduler = NULL;
    }
}

void
PipeStream::flush(bool flushParent)
{
    while (true) {
        {
            boost::mutex::scoped_lock lock(*m_mutex);
            if (m_cancelledWrite)
                MORDOR_THROW_EXCEPTION(OperationAbortedException());
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (!otherStream) {
                // See if they read everything before destructing
                if (m_otherClosed & READ)
                    return;
                MORDOR_THROW_EXCEPTION(BrokenPipeException());
            }

            if (otherStream->m_readBuffer.readAvailable() == 0)
                return;
            if (otherStream->m_closed & READ)
                MORDOR_THROW_EXCEPTION(BrokenPipeException());
            // Wait for the other stream to schedule us
            MORDOR_ASSERT(!otherStream->m_pendingWriter);
            MORDOR_ASSERT(!otherStream->m_pendingWriterScheduler);
            MORDOR_LOG_DEBUG(g_log) << this << " waiting to flush";
            otherStream->m_pendingWriter = Fiber::getThis();
            otherStream->m_pendingWriterScheduler = Scheduler::getThis();
        }
        try {
            Scheduler::yieldTo();
        } catch (...) {
            boost::mutex::scoped_lock lock(*m_mutex);
            PipeStream::ptr otherStream = m_otherStream.lock();
            if (otherStream && otherStream->m_pendingWriter == Fiber::getThis()) {
                MORDOR_ASSERT(otherStream->m_pendingWriterScheduler == Scheduler::getThis());
                otherStream->m_pendingWriter.reset();
                otherStream->m_pendingWriterScheduler = NULL;
            }
            throw;
        }
    }
}

boost::signals2::connection
PipeStream::onRemoteClose(const boost::signals2::slot<void ()> &slot)
{
    return m_onRemoteClose.connect(slot);
}


std::pair<NativeStream::ptr, NativeStream::ptr>
anonymousPipe(IOManager *ioManager)
{
    std::pair<NativeStream::ptr, NativeStream::ptr> result;
#ifdef WINDOWS
    if (ioManager) {
        // TODO: Implement overlapped I/O for this pipe with either a
        // not-quite-anonymous pipe, or a socket pair
        MORDOR_NOTREACHED();
    } else {
        HANDLE read = NULL, write = NULL;
        if (!CreatePipe(&read, &write, NULL, 0))
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("CreatePipe");
        try {
            result.first.reset(new HandleStream(read));
            result.second.reset(new HandleStream(write));
        } catch (...) {
            if (!result.first)
                CloseHandle(read);
            if (!result.second)
                CloseHandle(write);
            throw;
        }
    }
#else
    int fds[2];
    if (pipe(fds))
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("pipe");
    try {
            result.first.reset(new FDStream(fds[0], ioManager));
            result.second.reset(new FDStream(fds[1], ioManager));
        } catch (...) {
            if (!result.first)
                close(fds[0]);
            if (!result.second)
                close(fds[1]);
            throw;
        }
#endif
    return result;
}

#ifdef WINDOWS

Process::~Process()
{
    CloseHandle(pid);
}

void
Process::wait()
{
    WaitForSingleObject(pid, INFINITE);
}

Process::ptr
popen(const wchar_t *filename, wchar_t * const argv[],
    wchar_t * const envp[],
    NativeStream::ptr in,
    NativeStream::ptr out,
    NativeStream::ptr err)
{
    MORDOR_ASSERT(!in || in->supportsRead());
    MORDOR_ASSERT(!out || out->supportsWrite());
    MORDOR_ASSERT(!err || err->supportsWrite());

    std::wostringstream os;
    // FIXME: escape "'s and \'s and quote spaces and tabs;
    // see http://msdn.microsoft.com/en-us/library/17w5ykft.aspx
    os << filename;
    while (argv && *argv) {
        os << L' ' << *argv;
        ++argv;
    }
    std::wstring commandLine = os.str();

    STARTUPINFOW startupInfo;
    PROCESS_INFORMATION processInfo;
    memset(&startupInfo, 0, sizeof(STARTUPINFO));
    startupInfo.cb = sizeof(STARTUPINFO);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    if (in) {
        if (!DuplicateHandle(GetCurrentProcess(), in->handle(),
            GetCurrentProcess(), &startupInfo.hStdInput,
            0, TRUE, DUPLICATE_SAME_ACCESS))
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("DuplicateHandle");
    } else {
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    if (out) {
        if (!DuplicateHandle(GetCurrentProcess(), out->handle(),
            GetCurrentProcess(), &startupInfo.hStdOutput,
            0, TRUE, DUPLICATE_SAME_ACCESS)) {
            if (in)
                CloseHandle(startupInfo.hStdInput);
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("DuplicateHandle");
        }
    } else {
        startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (err) {
        if (!DuplicateHandle(GetCurrentProcess(), err->handle(),
            GetCurrentProcess(), &startupInfo.hStdError,
            0, TRUE, DUPLICATE_SAME_ACCESS)) {
            if (in)
                CloseHandle(startupInfo.hStdInput);
            if (out)
                CloseHandle(startupInfo.hStdError);
            MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("DuplicateHandle");
        }
    } else {
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }

    if (!CreateProcessW(NULL, (LPWSTR)commandLine.c_str(),
        NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT, (LPVOID)envp, NULL,
        &startupInfo, &processInfo)) {
        if (in)
            CloseHandle(startupInfo.hStdInput);
        if (out)
            CloseHandle(startupInfo.hStdOutput);
        if (err)
            CloseHandle(startupInfo.hStdError);
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("CreateProcessW");
    }
    CloseHandle(processInfo.hThread);
    if (in)
        CloseHandle(startupInfo.hStdInput);
    if (out)
        CloseHandle(startupInfo.hStdOutput);
    if (err)
        CloseHandle(startupInfo.hStdError);
    try {
        return Process::ptr(new Process(processInfo.hProcess));
    } catch (...) {
        CloseHandle(processInfo.hProcess);
        throw;
    }
}

Stream::ptr popen(const wchar_t *filename, wchar_t * const argv[],
    OpenMode openMode, StderrMode stderrMode, wchar_t * const envp[])
{
    std::pair<NativeStream::ptr, NativeStream::ptr> pipe = anonymousPipe();
    NativeStream::ptr in, out, err, null, result;
    HANDLE hNull = CreateFileW(L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hNull == INVALID_FILE_HANDLE)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("CreateFileW");
    null.reset(new HandleStream(hNull));
    switch (openMode) {
        case READ:
            in = null;
            out = pipe.second;
            result = pipe.first;
            break;
        case WRITE:
            in = pipe.first;
            out = null;
            result = pipe.second;
            break;
        default:
            MORDOR_NOTREACHED();
    }
    switch (stderrMode) {
        case DROP:
            err = null;
            break;
        case REDIRECT:
            err = out;
            break;
        case INHERIT:
            break;
        default:
            MORDOR_NOTREACHED();
    }
    Process::ptr process = popen(filename, argv, envp, in, out, err);
    return result;
}

#else

Process::~Process()
{
    // XXX: reap the process so it doesn't become a zombie
}

void
Process::wait()
{
    if (pid) {
        int status;
        waitpid(pid, &status, 0);
    }
    pid = 0;
}

Process::ptr
popen(const char *filename, char * const argv[], char * const envp[],
    NativeStream::ptr in, NativeStream::ptr out, NativeStream::ptr err)
{
    MORDOR_ASSERT(!in || in->supportsRead());
    MORDOR_ASSERT(!out || out->supportsWrite());
    MORDOR_ASSERT(!err || err->supportsWrite());

    static char * const noArgs[] = { NULL };
    if (!argv)
        argv = noArgs;
    if (!envp) {
#ifdef OSX
	char **environ = *_NSGetEnviron();
#endif
        envp = environ;
    }

    pid_t child = fork();
    if (child == -1)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("fork");
    if (child == 0) {
        // This is the child; set up the std streams, and then exec
        if (in) {
            close(STDIN_FILENO);
            if (dup2(in->fd(), STDIN_FILENO) == -1)
                _exit(-errno);
        }
        if (out) {
            close(STDOUT_FILENO);
            if (dup2(out->fd(), STDOUT_FILENO) == -1)
                _exit(-errno);
        }
        if (err) {
            close(STDERR_FILENO);
            if (dup2(err->fd(), STDERR_FILENO) == -1)
                _exit(-errno);
        }
        execve(filename, argv, envp);
        _exit(-errno);
    } else {
        // This is the parent; return
        // TODO: catch exception, and reap the process so it doesn't become a zombie
        return Process::ptr(new Process(child));
    }
}

#endif

Stream::ptr popen(const char *filename, char * const argv[], OpenMode openMode,
    StderrMode stderrMode, char * const envp[])
{
    std::pair<NativeStream::ptr, NativeStream::ptr> pipe = anonymousPipe();
    NativeStream::ptr in, out, err, null, result;
#ifdef WINDOWS
    HANDLE hNull = CreateFileW(L"NUL",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hNull == INVALID_FILE_HANDLE)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("CreateFileW");
    null.reset(new HandleStream(hNull));
#else
    int nullFd = open("/dev/null", O_RDWR);
    if (nullFd == -1)
        MORDOR_THROW_EXCEPTION_FROM_LAST_ERROR_API("open");
    null.reset(new FDStream(nullFd));
#endif
    switch (openMode) {
        case READ:
            in = null;
            out = pipe.second;
            result = pipe.first;
            break;
        case WRITE:
            in = pipe.first;
            out = null;
            result = pipe.second;
            break;
        default:
            MORDOR_NOTREACHED();
    }
    switch (stderrMode) {
        case DROP:
            err = null;
            break;
        case REDIRECT:
            err = out;
            break;
        case INHERIT:
            break;
        default:
            MORDOR_NOTREACHED();
    }
    Process::ptr process = popen(filename, argv, envp, in, out, err);
    return result;
}

}
