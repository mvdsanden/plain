#include "main.h"
#include "application.h"
#include "io/socketpair.h"
#include "exceptions/errnoexception.h"

#include <mutex>

#include <sys/epoll.h>
#include <unistd.h>

using namespace plain;

struct Main::Data {
  bool running;

  int exitCode;

  SocketPair signalPair;

  std::mutex mutex;

  int epoll;

Data()
    : running(false),
      exitCode(0),
      epoll(-1)
  {
  }

};

Main &Main::instance()
{
  static Main s_instance;
  return s_instance;
}

void _connectSignalPair(Main *main)
{
  epoll_event event;
  event.data.fd = main->d->signalPair.fdOut();
  event.events = EPOLLIN | EPOLLET;

  int ret = epoll_ctl(main->d->epoll,
		      EPOLL_CTL_ADD,
		      main->d->signalPair.fdOut(),
		      &event);

  if (ret == -1) {
    throw ErrnoException(errno);
  }
}

Main::Main()
  : d(new Main::Data)
{
  d->epoll = epoll_create1(EPOLL_CLOEXEC);
  if (d->epoll == -1) {
    throw ErrnoException(errno);
  }

  _connectSignalPair(this);
}

Main::~Main()
{

}

void _signalLoop(Main *main, size_t signal)
{
  int ret = write(main->d->signalPair.fdIn(),
		  reinterpret_cast<char const *>(&signal),
		  sizeof(signal));

  if (ret == -1) {
    throw ErrnoException(errno);
  }
}

void Main::wakeup()
{
  _signalLoop(this, 0);
}

int _mainLoop(Main *main, Application &app)
{
  std::unique_lock<std::mutex> lk(main->d->mutex);

  epoll_event events[128];

  while (main->d->running) {
    lk.unlock();

    // Default 30 second timeout.
    int timeout = 30000;

    int ret = epoll_wait(main->d->epoll,
			 events,
			 128,
			 timeout);

    if (ret == -1) {
      main->d->exitCode = -1;
      main->d->running = false;
    }    

    app.idle();

    lk.lock();
  }

  return main->d->exitCode;
}

int Main::run(Application &app, int argc, char *argv[])
{
  d->running = true;
  app.create(argc, argv);
  int code = _mainLoop(this, app);
  app.destroy();
  return code;
}

void Main::stop(int code)
{
  std::lock_guard<std::mutex> lk(d->mutex);
  d->exitCode = code;
  d->running = false;
  _signalLoop(this, 1);
}
