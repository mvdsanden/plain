#include "timeouthandler.h"

#include "core/timeoutable.h"
#include "core/private/timeoutable.h"

using namespace plain;

struct TimeoutHandler::Private {

  struct TimeoutableList {
    Timeoutable head;
    Timeoutable tail;

    TimeoutableList()
    {
      head.timeoutable.prev = NULL;
      head.timeoutable.next = &tail;
      tail.timeoutable.prev = &head;
      tail.timeoutable.next = NULL;
    }
  };

  // A 3600 second wheel.
  TimeoutableList wheel[3600];

  // The starting time of the wheel.
  std::chrono::steady_clock::time_point t0;

  // The last time frame that was processed.
  size_t lastProcessed;
  
  Private()
    : t0(std::chrono::steady_clock::now()),
      lastProcessed(0)
  {
  }

  ~Private();

  void setTimeout(Timeoutable *timeoutable, std::chrono::steady_clock::duration const &duration)
  {
    setTimeoutAt(std::chrono::steady_clock::now() + duration);
  }

  void setTimeoutAt(Timeoutable *timeoutable, std::chrono::steady_clock::time_point const &time)
  {
    if (timeoutable.timeoutable.next != NULL) {
      // Already in a timeout list.
      return;
    }
    
    std::chrono::seconds dt = std::chrono::duration_cast<std::chrono::seconds>(time - t0);
    
    wheel[dt.count() % 3600].push(timeoutable);
  }

  void cancelTimeout(Timeoutable *timeoutable)
  {
    if (timeoutable.timeoutable.next == NULL) {
      // Not in a timeout list.
      return;
    }

    // Remove the timeoutable from the timeout list it is in.
    timeoutable.timeoutable.prev->timeoutable.next = timeoutable.timeoutable.next;
    timeoutable.timeoutable.next->timeoutable.prev = timeoutable.timeoutable.prev;
    timeoutable.timeoutable.prev = NULL;
    timeoutable.timeoutable.next = NULL;
  }

  void update()
  {
    size_t current = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() % 3600;

    for (size_t i = (lastProcessed + 1) % 3600; i < current; current = (current + 1) % 3600) {

      
      
    }
  }
  
};

TimeoutHandler::TimeoutHandler()
  : timeoutHandler(new Private)
{
}

TimeoutHandler::~TimeoutHandler()
{
}

void TimeoutHandler::setTimeout(Timeoutable *timeoutable, std::chrono::steady_clock::duration const &duration)
{
  timeoutHandler->setTimeout(timeoutable, duration);
}

void TimeoutHandler::setTimeoutAt(Timeoutable *timeoutable, std::chrono::steady_clock::time_point const &time)
{
  timeoutHandler->setTimeoutAt(timeoutable, duration);
}

void TimeoutHandler::cancelTimeout(Timeoutable *timeoutable)
{
  timeoutHandler->cancelTimeout(timeoutable);
}

void TimeoutHandler::update()
{
  timeoutHandler->update();
}
