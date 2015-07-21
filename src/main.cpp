#include "core/main.h"
#include "core/application.h"
#include "io/poll.h"
#include "io/socketpair.h"

#include <iostream>
#include <thread>

#include <unistd.h>

class App : public plain::Application {

  std::thread d_thread0;

  plain::SocketPair d_spair;

public:

  static plain::Poll::EventResultMask writeStuff(int fd, uint32_t events, void *data)
  {
    char buf[1024];

    int ret = write(fd, buf, 1024);

    //    std::cout << "write=" << ret << ".\n";

    if (ret == -1) {
      if (errno == EAGAIN) {
	return plain::Poll::WRITE_COMPLETED;
      }
    }

    return plain::Poll::NONE_COMPLETED;
  }

  static plain::Poll::EventResultMask readStuff(int fd, uint32_t events, void *data)
  {
    char buf[512];

    int ret = read(fd, buf, 512);

    //    std::cout << "read=" << ret << ".\n";

    if (ret == -1) {
      if (errno == EAGAIN) {
	return plain::Poll::READ_COMPLETED;
      }
    }

    return plain::Poll::NONE_COMPLETED;
  }

  virtual void create(int argc, char *argv[])
  {
    std::cout << "-- create --\n";

    // * If you want to stop the application immediatly:
    // plain::Main::instance().stop(1);

    plain::Main::instance().poll().add(d_spair.fdIn(), plain::Poll::OUT, writeStuff, this);
    plain::Main::instance().poll().add(d_spair.fdOut(), plain::Poll::IN, readStuff, this);

    d_thread0 = std::thread([this](){


	sleep(10);
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
  }
  
  virtual void destroy()
  {
    std::cout << "-- destroy --\n";
    d_thread0.join();
  }

  virtual void idle()
  {
    std::cout << "-- idle --\n";
  }

};

int main(int argc, char *argv[])
{
  App app;
  return plain::Main::instance().run(app, argc, argv);
}
