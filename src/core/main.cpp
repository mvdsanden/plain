#include "main.h"
#include "application.h"
#include "io/socketpair.h"
#include "exceptions/errnoexception.h"
#include "io/poll.h"

#include <mutex>
#include <iostream>

#include <string.h>
#include <unistd.h>

using namespace plain;

enum Signals {
  SIGNAL_NONE = 0,
  SIGNAL_STOP = 1,
};

struct Main::Data {
  bool running;

  int exitCode;

  SocketPair signalPair;

  std::mutex mutex;

  Poll poll;

  size_t signalBuffer;
  size_t signalBufferFill;

  Data()
    : running(false),
      exitCode(0),
      signalBuffer(0),
      signalBufferFill(0)
  {
  }

};

Main &Main::instance()
{
  static Main s_instance;
  return s_instance;
}

Poll::EventResultMask _onSignal(int fd, uint32_t events, void *data)
{
  Main *main = reinterpret_cast<Main*>(data);
  std::cout << "-- signal --\n";

  int ret = read(fd,
		 reinterpret_cast<char *>(&main->d->signalBuffer)  + main->d->signalBufferFill,
		 sizeof(size_t) - main->d->signalBufferFill);

  std::cout << "* " << ret << ".\n";

  if (ret == -1) {
    if (errno == EAGAIN) {
      return Poll::READ_COMPLETED;
    }

    throw ErrnoException(errno);
  }

  main->d->signalBufferFill += ret;

  if (main->d->signalBufferFill == sizeof(size_t)) {

    main->d->signalBufferFill = 0;

    if (main->d->signalBuffer == SIGNAL_STOP) {
      main->d->running = false;
    }

  }

  return Poll::NONE_COMPLETED;
}

void _connectSignalPair(Main *main)
{
  main->d->poll.add(main->d->signalPair.fdOut(),
		    Poll::IN,
		    _onSignal,
		    main);
}

Main::Main()
  : d(new Main::Data)
{
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

  while (main->d->running) {
    lk.unlock();

    // Default 30 second timeout.
    int timeout = 30000;

    main->d->poll.update(timeout);    

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
  //d->running = false;
  _signalLoop(this, SIGNAL_STOP);
}

Poll &Main::poll()
{
  return d->poll;
}
