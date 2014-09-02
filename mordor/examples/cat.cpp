// Copyright (c) 2009 - Mozy, Inc.

#include "../predef.h"

#include <iostream>

#include "../config.h"
#include "../main.h"
#include "../streams/file.h"
#include "../streams/std.h"
#include "../streams/transfer.h"
#include "../workerpool.h"

using namespace Mordor;

MORDOR_MAIN(int argc, const char * const argv[])
{
    try {
        Config::loadFromEnvironment();
        StdoutStream stdoutStream;
        WorkerPool pool(2);
        if (argc == 1) {
            argc = 2;
            const char * const hyphen[] = { "", "-" };
            argv = hyphen;
        }
        for (int i = 1; i < argc; ++i) {
            Stream::ptr inStream;
            std::string arg(argv[i]);
            if (arg == "-") {
                inStream.reset(new StdinStream());
            } else {
                inStream.reset(new FileStream(arg, FileStream::READ));
            }
            transferStream(inStream, stdoutStream);
        }
    } catch (...) {
        std::cerr << boost::current_exception_diagnostic_information()
            << std::endl;
    }
    return 0;
}
