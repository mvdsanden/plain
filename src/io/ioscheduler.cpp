#include "ioscheduler.h"

#include <mutex>

using namespace plain;

struct IoScheduler::Internal {

  std::mutex d_mutex;

  // The currently scheduled events list.
  // - Events are processed from head to tail.
  // - New events are added in the middle.
  // - When an event is processed it is moved to the tail of the list.
  struct SchedList {
    Schedulable head;
    Schedulable tail;
    Schedulable *mid;

    SchedList()
    {
      head.schedNext = &tail;
      head.schedPrev = NULL;
      tail.schedPrev = &head;
      tail.schedNext = NULL;
      mid = &head;
    }

    /// Add the entry to the middle of the scheduler list.
    void pushMid(Schedulable *entry)
    {
      entry->schedPrev = mid;
      entry->schedNext = mid->schedNext;
      mid->schedNext->schedPrev = entry;
      mid->schedNext = entry;
      mid = entry;
    }

    /// Add the entry to the middle of the scheduler list.
    void pushBack(Schedulable *entry)
    {
      tail.schedPrev->schedNext = entry;
      entry->schedPrev = tail.schedPrev;
      entry->schedNext = &tail;
      tail.schedPrev = entry;
    }

    void remove(Schedulable *entry)
    {
      if (entry == mid) {
	mid = entry->schedPrev;
      }
      
      entry->schedPrev->schedNext = entry->schedNext;
      entry->schedNext->schedPrev = entry->schedPrev;
      entry->schedPrev = entry->schedNext = NULL;
    }
    
  };

  SchedList d_defaultPrio;
  SchedList d_running;

  Internal()
  {
  }

  void schedule(Schedulable *schedulable)
  {
    std::lock_guard<std::mutex> lk(d_mutex);

    // If it is not already scheduled, add it to the schedule.
    if (schedulable->schedState == STATE_UNSCHEDULED) {
      d_defaultPrio.pushMid(schedulable);
      schedulable->schedState = STATE_SCHEDULED;
    }
  }

  void deschedule(Schedulable *schedulable)
  {
    std::lock_guard<std::mutex> lk(d_mutex);
	
    if (schedulable->schedState == STATE_SCHEDULED) {
      d_defaultPrio.remove(schedulable);
    } else if (schedulable->schedState == STATE_RUNNING) {
      d_running.remove(schedulable);
    }

    schedulable->schedState = STATE_UNSCHEDULED;
  }

  void runNext()
  {
    std::unique_lock<std::mutex> lk(d_mutex);
    
    // Get the next schedulable that is up for running.
    Schedulable *schedulable = d_defaultPrio.head.schedNext;

    // Check if it is not the tail.
    if (schedulable == &d_defaultPrio.tail) {
      return;      
    }

    // Remove the schedulable from the schedule list.
    // If this is mid it will automatically point mid to the head of the list.
    d_defaultPrio.remove(schedulable);

    // Add it to the running list.
    d_running.pushBack(schedulable);

    // Set schedulable state to running.
    schedulable->schedState = STATE_RUNNING;
    
    // Get the schedulable callback.
    Callback callback = schedulable->schedCallback;

    // Unlock the mutex. This means that when a schedulable is unscheduled it
    // might still run the callback once.
    lk.unlock();
    
    Result result = RESULT_DONE;

    // If the schedulable has a callback, run it.
    if (callback != NULL) {
      result = callback(schedulable);
    }

    lk.lock();

    // If ths schedulable was descheduled in the mean time, return.
    if (schedulable->schedState == STATE_UNSCHEDULED) {
      return;
    }

    // Remove it from the running list.
    d_running.remove(schedulable);
    
    // If the schedulable has more work to do, reinsert it at the end of the schedule.
    if (result == RESULT_NOT_DONE) {
      d_defaultPrio.pushBack(schedulable);
      schedulable->schedState = STATE_SCHEDULED;
    } else {
      schedulable->schedState = STATE_UNSCHEDULED;
    }
  }
  
};

IoScheduler::IoScheduler()
  : d(new Internal)
{
}

IoScheduler::~IoScheduler()
{
}

void IoScheduler::schedule(Schedulable *schedulable)
{
  d->schedule(schedulable);
}

void IoScheduler::deschedule(Schedulable *schedulable)
{
  d->deschedule(schedulable);
}

void IoScheduler::runNext()
{
  d->runNext();
}
