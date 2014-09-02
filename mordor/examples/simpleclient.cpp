// Copyright (c) 2009 - Mozy, Inc.

#include "../predef.h"

#include <iostream>

#include "../config.h"
#include "../fiber.h"
#include "../iomanager.h"
#include "../main.h"
#include "../socket.h"

using namespace Mordor;

MORDOR_MAIN(int argc, char *argv[])
{
    try {
        Config::loadFromEnvironment();
        IOManager ioManager;
        std::vector<Address::ptr> addresses = Address::lookup(argv[1]);
        Socket::ptr s(addresses[0]->createSocket(ioManager, SOCK_STREAM));
        s->connect(addresses[0]);
        size_t rc = s->send("hello\r\n", 7);
        char buf[8192];
        rc = s->receive(buf, 8192);
        buf[rc] = 0;
        std::cout << "Read " << buf << " from conn" << std::endl;
        s->shutdown();
    } catch (...) {
        std::cerr << boost::current_exception_diagnostic_information() << std::endl;
    }
    return 0;
}
