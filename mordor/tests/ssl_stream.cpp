// Copyright (c) 2009 - Mozy, Inc.

#include "mordor/iomanager.h"
#include "mordor/parallel.h"
#include "mordor/streams/buffered.h"
#include "mordor/streams/hash.h"
#include "mordor/streams/null.h"
#include "mordor/streams/pipe.h"
#include "mordor/streams/random.h"
#include "mordor/streams/ssl.h"
#include "mordor/streams/transfer.h"
#include "mordor/test/test.h"
#include "mordor/workerpool.h"

using namespace Mordor;

static void test_accept(SSLStream::ptr server)
{
    server->accept();
    server->flush();
}

MORDOR_UNITTEST(SSLStream, basic)
{
    WorkerPool pool;
    std::pair<Stream::ptr, Stream::ptr> pipes = pipeStream();

    SSLStream::ptr sslserver(new SSLStream(pipes.first, false));
    SSLStream::ptr sslclient(new SSLStream(pipes.second, true));

    pool.schedule(std::bind(&test_accept, sslserver));
    sslclient->connect();
    pool.dispatch();

    Stream::ptr server = sslserver, client = sslclient;

    char buf[6];
    buf[5] = '\0';
    client->write("hello");
    client->flush(false);
    MORDOR_TEST_ASSERT_EQUAL(server->read(buf, 5), 5u);
    MORDOR_TEST_ASSERT_EQUAL((const char *)buf, "hello");
    server->write("world");
    server->flush(false);
    MORDOR_TEST_ASSERT_EQUAL(client->read(buf, 5), 5u);
    MORDOR_TEST_ASSERT_EQUAL((const char *)buf, "world");
}

static void writeLotsaData(
    Stream::ptr stream, unsigned long long toTransfer, bool &complete, std::string &hash)
{
    RandomStream::ptr random(new RandomStream());
    MD5Stream::ptr src(new MD5Stream(random));
    MORDOR_TEST_ASSERT_EQUAL(transferStream(src, stream, toTransfer), toTransfer);
    stream->flush();
    hash = src->hash();
    complete = true;
}

static void readLotsaData(
    Stream::ptr stream, unsigned long long toTransfer, bool &complete, std::string &hash)
{
    MD5Stream::ptr dest(new MD5Stream(NullStream::get_ptr()));
    MORDOR_TEST_ASSERT_EQUAL(transferStream(stream, dest, toTransfer), toTransfer);
    hash = dest->hash();
    complete = true;
}

MORDOR_UNITTEST(SSLStream, duplexStress)
{
    WorkerPool pool;
    // Force more fiber context switches by having a smaller buffer
    std::pair<Stream::ptr, Stream::ptr> pipes = pipeStream(1024);

    SSLStream::ptr sslserver(new SSLStream(pipes.first, false));
    SSLStream::ptr sslclient(new SSLStream(pipes.second, true));

    pool.schedule(std::bind(&test_accept, sslserver));
    sslclient->connect();
    pool.dispatch();

    // Transfer 1 MB
    long long toTransfer = 1024 * 1024;
    std::vector<std::function<void ()> > dgs;
    bool complete1 = false, complete2 = false, complete3 = false, complete4 = false;
    std::string hash1, hash2, hash3, hash4;
    dgs.push_back(
        std::bind(&writeLotsaData, sslserver, toTransfer, std::ref(complete1), std::ref(hash1)));
    dgs.push_back(
        std::bind(&readLotsaData, sslserver, toTransfer, std::ref(complete2), std::ref(hash2)));
    dgs.push_back(
        std::bind(&writeLotsaData, sslclient, toTransfer, std::ref(complete3), std::ref(hash3)));
    dgs.push_back(
        std::bind(&readLotsaData, sslclient, toTransfer, std::ref(complete4), std::ref(hash4)));
    parallel_do(dgs);
    MORDOR_ASSERT(complete1);
    MORDOR_ASSERT(complete2);
    MORDOR_ASSERT(complete3);
    MORDOR_ASSERT(complete4);
    MORDOR_TEST_ASSERT_EQUAL(hash1, hash4);
    MORDOR_TEST_ASSERT_EQUAL(hash2, hash3);
}

static void readWorld(Stream::ptr stream, int &sequence)
{
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 1);
    char buf[6];
    buf[5] = '\0';
    MORDOR_TEST_ASSERT_EQUAL(stream->read(buf, 5), 5u);
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 3);
    MORDOR_TEST_ASSERT_EQUAL((const char *)buf, "world");
}

MORDOR_UNITTEST(SSLStream, forceDuplex)
{
    WorkerPool pool;
    std::pair<Stream::ptr, Stream::ptr> pipes = pipeStream();

    SSLStream::ptr sslserver(new SSLStream(pipes.first, false));
    SSLStream::ptr sslclient(new SSLStream(pipes.second, true));

    Stream::ptr server = sslserver, client = sslclient;

    int sequence = 0;
    pool.schedule(std::bind(&test_accept, sslserver));
    sslclient->connect();
    pool.dispatch();

    pool.schedule(std::bind(&readWorld, client,
        std::ref(sequence)));
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 2);
    // Read is pending
    client->write("hello");
    client->flush(false);
    pool.dispatch();
    server->write("world");
    server->flush(false);
    pool.dispatch();
    MORDOR_TEST_ASSERT_EQUAL(++sequence, 4);
}

static void expectUnexpectedEof(Stream::ptr stream)
{
    unsigned char buffer;
    MORDOR_TEST_ASSERT_EQUAL(stream->read(&buffer, 1u), 0u);
}

MORDOR_UNITTEST(SSLStream, incomingDataAfterShutdown)
{
    WorkerPool pool;
    std::pair<Stream::ptr, Stream::ptr> pipes = pipeStream();

    SSLStream::ptr sslserver(new SSLStream(pipes.first, false));
    SSLStream::ptr sslclient(new SSLStream(pipes.second, true));

    Stream::ptr server = sslserver, client = sslclient;

    pool.schedule(std::bind(&test_accept, sslserver));
    sslclient->connect();
    pool.dispatch();

    MORDOR_TEST_ASSERT_EQUAL(sslclient->write("c", 1u), 1u);
    sslclient->flush(false);
    pool.schedule(std::bind(&expectUnexpectedEof, sslclient));
    sslserver->close();
}

MORDOR_UNITTEST(SSLStream, acceptOverBuffering)
{
    WorkerPool pool;
    std::pair<Stream::ptr, Stream::ptr> pipes = pipeStream();
    BufferedStream::ptr bufferedStream(new BufferedStream(pipes.first));
    bufferedStream->allowPartialReads(true);

    SSLStream::ptr sslserver(new SSLStream(bufferedStream, false));
    SSLStream::ptr sslclient(new SSLStream(pipes.second, true));

    pool.schedule(std::bind(&test_accept, sslserver));
    sslclient->connect();
    pool.dispatch();
}
