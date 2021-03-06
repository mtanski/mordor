#include "../predef.h"

#include <memory>
#include <iostream>

#include <boost/thread.hpp>

#include "../config.h"
#include "../http/server.h"
#include "../iomanager.h"
#include "../main.h"
#include "../socket.h"
#include "../streams/file.h"
#include "../streams/socket.h"
#include "../streams/transfer.h"
#include "../streams/ssl.h"

using namespace Mordor;

static void httpRequest(HTTP::ServerRequest::ptr request)
{
	const std::string &method = request->request().requestLine.method;
	const URI &uri = request->request().requestLine.uri;

	if (method == HTTP::GET) {
		FileStream::ptr stream(new FileStream(uri.path.toString(), FileStream::READ, FileStream::OPEN, static_cast<IOManager*>(Scheduler::getThis()), Scheduler::getThis()));
		HTTP::respondStream(request, stream);
	} else {
		HTTP::respondError(request, HTTP::METHOD_NOT_ALLOWED);
	}
}

void serve(Socket::ptr listen)
{
	while (true)
	{
		Socket::ptr socket = listen->accept();
		SocketStream::ptr socketStream(new SocketStream(socket));

		Stream::ptr stream = socketStream;

		HTTP::ServerConnection::ptr conn(new HTTP::ServerConnection(stream, &httpRequest));
		Scheduler::getThis()->schedule(std::bind(&HTTP::ServerConnection::processRequests, conn));
	}
}

MORDOR_MAIN(int argc, char *argv[])
{
    try {
        Config::loadFromEnvironment();
        IOManager ioManager(2, false);

        Socket::ptr httpSocket = Socket::ptr(new Socket(ioManager, AF_INET, SOCK_STREAM));
        IPv4Address httpAddress(INADDR_ANY, 8080);
        httpSocket->setOption(SOL_SOCKET, SO_REUSEADDR, (int) 1);
        httpSocket->bind(httpAddress);
        httpSocket->listen();

	ioManager.schedule(std::bind(serve, httpSocket));
	ioManager.start();

	while (1) {
		sleep(1);
	}

    } catch (...) {
        std::cerr << boost::current_exception_diagnostic_information() << std::endl;
    }
    return 0;
}
