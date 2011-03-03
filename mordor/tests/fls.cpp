// Copyright (c) 2009 - Mozy, Inc.

#include <boost/bind.hpp>

#include "mordor/fiber.h"
#include "mordor/scheduler.h"
#include "mordor/test/test.h"

using namespace Mordor;

static void basic(FiberLocalStorage<int> &fls)
{
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 0);
    fls = 2;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 2);
    Fiber::yield();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 2);
    fls = 4;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 4);
    Fiber::yield();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 4);
    fls = 6;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 6);
}

static void thread(FiberLocalStorage<int> &fls,
                   Fiber::ptr fiber)
{
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 0);
    fls = 3;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 3);
    fiber->call();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 3);
    fls = 5;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 5);
}

MORDOR_UNITTEST(FLS, basic)
{
    FiberLocalStorage<int> fls;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 0);
    fls = 1;
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 1);

    Fiber::ptr fiber(new Fiber(boost::bind(&basic, boost::ref(fls))));
    fiber->call();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 1);

    Thread thread1(boost::bind(&thread, boost::ref(fls), fiber));
    thread1.join();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 1);
    fiber->call();
    MORDOR_TEST_ASSERT_EQUAL(fls.get(), 1);
}

#ifdef WINDOWS
static void tlsFiber(tls_key_t tlsKey, Fiber::weak_ptr weakCaller)
{
    Fiber::ptr caller(weakCaller);
    MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)0);
    TlsSetValue(tlsKey, (LPVOID)2);
    Fiber::yield();
    MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)2);
    caller->yieldTo();
    MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)2);
}

MORDOR_UNITTEST(FLS, tlsBridge)
{
    tls_key_t tlsKey = TlsAlloc();
    try {
        Fiber::tlsFlsBridgeAlloc(tlsKey);
        Fiber::weak_ptr weakThis = Fiber::getThis();
        Fiber::ptr otherFiber(new Fiber(boost::bind(&tlsFiber, tlsKey, weakThis)));
        MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)0);
        TlsSetValue(tlsKey, (LPVOID)1);
        otherFiber->call();
        MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)1);
        otherFiber->yieldTo();
        MORDOR_TEST_ASSERT_EQUAL(TlsGetValue(tlsKey), (LPVOID)1);
    } catch(...) {
        TlsFree(tlsKey);
        throw;
    }
    TlsFree(tlsKey);
}
#endif
