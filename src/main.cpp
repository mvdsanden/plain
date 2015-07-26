#include "core/main.h"
#include "core/application.h"
#include "io/poll.h"
#include "io/socketpair.h"
#include "net/httpserver.h"
#include "net/httprequesthandler.h"
#include "net/httprequest.h"

#include <memory>
#include <iostream>
#include <thread>
#include <chrono>

#include <unistd.h>

char s_pageNotFound[] = "HTTP 404 Not Found\r\nContent-Length: 35\r\n\r\n<HTML><BODY>Not Found</BODY></HTML>\0";

class RequestHandler : public plain::HttpRequestHandler {
public:

  virtual void request(plain::HttpRequest const &request)
  {
    respondWithStaticString(request, s_pageNotFound, sizeof(s_pageNotFound));
  }

};

class App : public plain::Application {

  std::thread d_thread0;

  plain::SocketPair d_spair0;
  plain::SocketPair d_spair1;

  std::chrono::steady_clock::time_point t0;

  size_t bytesWritten, bytesRead;

  std::shared_ptr<plain::HttpServer> d_httpServer;

  int d_port;

public:

  static plain::Poll::EventResultMask writeStuff(int fd, uint32_t events, void *data)
  {
    char buf[1024];

    int ret = write(fd, buf, 1024);

    //    std::cout << "write=" << ret << ".\n";

    if (ret == -1) {
      if (errno == EAGAIN) {
	std::cout << "Write completed: " << fd << ".\n";
	return plain::Poll::WRITE_COMPLETED;
      }
    }

    reinterpret_cast<App*>(data)->bytesWritten += ret;

    return plain::Poll::NONE_COMPLETED;
  }

  static plain::Poll::EventResultMask readStuff(int fd, uint32_t events, void *data)
  {
    char buf[512];

    int ret = read(fd, buf, 512);

    //    std::cout << "read=" << ret << ".\n";

    if (ret == -1) {
      if (errno == EAGAIN) {
	std::cout << "Read completed: " << fd << ".\n";
	return plain::Poll::READ_COMPLETED;
      }
    }

    reinterpret_cast<App*>(data)->bytesRead += ret;

    return plain::Poll::NONE_COMPLETED;
  }

  virtual void create(int argc, char *argv[])
  {
    if (argc > 1) {
      d_port = std::atoi(argv[1]);
    } else {
      d_port = 8080;
    }

    bytesWritten = 0;
    bytesRead = 0;

    t0 = std::chrono::steady_clock::now();

    std::cout << "-- create --\n";

    // * If you want to stop the application immediatly:
    // plain::Main::instance().stop(1);

    /*
    plain::Main::instance().poll().add(d_spair0.fdIn(), plain::Poll::OUT, writeStuff, this);
    plain::Main::instance().poll().add(d_spair0.fdOut(), plain::Poll::IN, readStuff, this);

    plain::Main::instance().poll().add(d_spair1.fdIn(), plain::Poll::OUT, writeStuff, this);
    plain::Main::instance().poll().add(d_spair1.fdOut(), plain::Poll::IN, readStuff, this);

    d_thread0 = std::thread([this](){

	sleep(5);

	writeStuff(d_spair0.fdIn(), plain::Poll::OUT, this);
	
	sleep(5);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
	plain::Main::instance().stop(1);
      });
    */

    d_httpServer = std::make_shared<plain::HttpServer>(d_port, std::make_shared<RequestHandler>());
  }
  
  virtual void destroy()
  {
    std::cout << "-- destroy --\n";
    std::cout << "Bytes written: " << bytesWritten << ".\n";
    std::cout << "Bytes read: " << bytesRead << ".\n";
    d_thread0.join();
  }

  virtual void idle()
  {
    std::cout << "-- idle -- " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() << ".\n";
  }

};

int main(int argc, char *argv[])
{
  App app;
  return plain::Main::instance().run(app, argc, argv);
}
