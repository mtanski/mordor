// Copyright (c) 2010 - Mozy, Inc.

#include "mordor/iomanager.h"
#include "mordor/streams/file.h"
#include "mordor/streams/namedpipe.h"
#include "mordor/streams/temp.h"
#include "mordor/test/test.h"

using namespace Mordor;

MORDOR_UNITTEST(HandleStream, peekFile)
{
    IOManager ioManager;
    TempStream tempStream(std::string(), true, &ioManager);

    MORDOR_TEST_ASSERT_EQUAL(tempStream.write("hello", 5u), 5u);

    char buffer[6];
    tempStream.seek(0);
    std::pair<size_t, bool> result = tempStream.peek(&buffer, 2u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 2u);
    MORDOR_TEST_ASSERT(!result.second);
    buffer[2] = '\0';
    MORDOR_TEST_ASSERT_EQUAL((const char *)buffer, "he");
    MORDOR_TEST_ASSERT_EQUAL(tempStream.tell(), 0);

    result = tempStream.peek(&buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 5u);
    MORDOR_TEST_ASSERT(!result.second);
    buffer[5] = '\0';
    MORDOR_TEST_ASSERT_EQUAL((const char *)buffer, "hello");
    MORDOR_TEST_ASSERT_EQUAL(tempStream.tell(), 0);

    tempStream.seek(5);
    result = tempStream.peek(&buffer, 2u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 0u);
    MORDOR_TEST_ASSERT(result.second);
}

MORDOR_UNITTEST(HandleStream, peekPipe)
{
    IOManager ioManager;

    NamedPipeStream::ptr serverStream;
    std::wstring pipename;
    while (true) {
        std::wostringstream os;
        os << L"\\\\.\\Pipe\\Anonymous." << gettid() << "." << rand();
        pipename = os.str();
        try {
            serverStream.reset(new NamedPipeStream(pipename,
                NamedPipeStream::READWRITE, &ioManager));
            break;
        } catch (NativeException &ex) {
            if (*boost::get_error_info<errinfo_nativeerror>(ex) != ERROR_ALREADY_EXISTS)
                throw;
        }
    }
    NamedPipeStream &readStream = *serverStream;
    FileStream writeStream(pipename);
    readStream.accept();

    char buffer[6];
    std::pair<size_t, bool> result = readStream.peek(buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 0u);
    MORDOR_TEST_ASSERT(!result.second);

    MORDOR_TEST_ASSERT_EQUAL(writeStream.write("hello", 5u), 5u);

    result = readStream.peek(buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 5u);
    MORDOR_TEST_ASSERT(!result.second);
    buffer[5] = '\0';
    MORDOR_TEST_ASSERT_EQUAL((const char *)buffer, "hello");

    // Repeatable
    result = readStream.peek(buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 5u);
    MORDOR_TEST_ASSERT(!result.second);
    MORDOR_TEST_ASSERT_EQUAL((const char *)buffer, "hello");

    writeStream.close();

    result = readStream.peek(buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 5u);
    MORDOR_TEST_ASSERT(!result.second);
    MORDOR_TEST_ASSERT_EQUAL((const char *)buffer, "hello");

    readStream.skip(5u);
    result = readStream.peek(buffer, 6u);
    MORDOR_TEST_ASSERT_EQUAL(result.first, 0u);
}
