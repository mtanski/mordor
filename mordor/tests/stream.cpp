// Copyright (c) 2010 - Mozy, Inc.

#include "mordor/streams/memory.h"
#include "mordor/streams/stream.h"
#include "mordor/streams/test.h"
#include "mordor/test/test.h"
#include "mordor/util.h"

using namespace Mordor;

namespace {

class NoStream : public Stream
{
    bool supportsRead() { return true; }
    size_t read(Buffer &buffer, size_t length) { return 0; }
};

}

MORDOR_UNITTEST(Stream, emulatedDirectReadEOF)
{
    NoStream realstream;
    Stream *stream = &realstream;
    char buf[1];
    MORDOR_TEST_ASSERT_EQUAL(stream->read(buf, 1), 0u);
}

MORDOR_UNITTEST(Stream, skipSeekableSizeable)
{
    MemoryStream stream("cody");

    MORDOR_TEST_ASSERT_EQUAL(stream.skip(2u), 2u);
    stream.seek(5);
    MORDOR_TEST_ASSERT_EQUAL(stream.skip(1u), 0u);
    stream.seek(4);
    MORDOR_TEST_ASSERT_EQUAL(stream.skip(1u), 0u);
    stream.seek(2);
    MORDOR_TEST_ASSERT_EQUAL(stream.skip(5u), 2u);
}

class NonSizeableStream : public MemoryStream
{
public:
    NonSizeableStream(const Buffer &buffer) : MemoryStream(buffer) {}
    bool supportsSize() { return false; }
};

MORDOR_UNITTEST(Stream, skipSeekable)
{
    NonSizeableStream stream("cody");

    MORDOR_TEST_ASSERT_EQUAL(stream.skip(2u), 2u);
}

class NonSeekableStream : public MemoryStream
{
public:
    NonSeekableStream(const Buffer &buffer) : MemoryStream(buffer) {}
    bool supportsSeek() { return false; }
};

MORDOR_UNITTEST(Stream, skipNonSeekable)
{
    NonSeekableStream stream("cody");

    MORDOR_TEST_ASSERT_EQUAL(stream.skip(5u), 4u);
}

MORDOR_UNITTEST(Stream, skipNonSeekablePartialRead)
{
    NonSeekableStream stream("cody");
    TestStream testStream(Stream::ptr(&stream, &nop<Stream *>));
    testStream.maxReadSize(2u);

    MORDOR_TEST_ASSERT_EQUAL(stream.skip(5u), 4u);
}
