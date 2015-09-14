#include "ioscheduler.h"

#include <mutex>
#include <iostream>

using namespace plain;

struct IoScheduler::Internal {

  std::mutex d_mutex;

  struct SchedList {
    std::mutex mutex[2];
    Schedulable head[2];
    Schedulable tail[2];
    size_t p; // primary
    size_t s; // secundary

    SchedList()
      : p(0), s(1)
    {      
      for (size_t i = 0; i < 2; ++i) {
	head[i].schedNext = &tail[i];
	head[i].schedPrev = NULL;
	tail[i].schedPrev = &head[i];
	tail[i].schedNext = NULL;
      }
    }

    /// Add the entry to the back of the scheduler list.
    void push(Schedulable *entry)
    {
      // Add the entry to the secondary list.
      std::lock_guard<std::mutex> slk(mutex[s]);
      tail[s].schedPrev->schedNext = entry;
      entry->schedPrev = tail[s].schedPrev;
      entry->schedNext = &tail[s];
      tail[s].schedPrev = entry;
    }

    // Remove the entry from the lists.
    void remove(Schedulable *entry)
    {
      std::lock_guard<std::mutex> plk(mutex[p]);
      std::lock_guard<std::mutex> slk(mutex[s]);
      entry->schedPrev->schedNext = entry->schedNext;
      entry->schedNext->schedPrev = entry->schedPrev;
      entry->schedPrev = entry->schedNext = NULL;
    }

    Schedulable *popFront()
    {
      std::lock_guard<std::mutex> plk(mutex[p]);

      // Get the front entry.
      Schedulable *entry = head[p].schedNext;

      // Check if the list was empty.
      if (entry == &tail[p]) {
	std::lock_guard<std::mutex> slk(mutex[s]);
	
	// Swap lists.
	std::swap(p, s);

	// Get the new front entry.
	entry = head[p].schedNext;

	// If second list is also empty return NULL.
	if (entry == &tail[p]) {
	  return NULL;
	}
      }

      // Remove the front entry.
      head[p].schedNext = entry->schedNext;
      entry->schedNext->schedPrev = &head[p];

      // Return the entry.
      return entry;
    }

    // \return true if the list is empty.
    bool empty() const
    {
      return (head[p].schedNext == &tail[p]) && (head[s].schedNext == &tail[s]);
    }
    
  };

  SchedList d_defaultPrio;
  SchedList d_running;

  Internal()
  {
  }

  void schedule(Schedulable *schedulable)
  {
    // If it is not already scheduled, add it to the schedule.
    if (schedulable->schedState == STATE_UNSCHEDULED) {
      schedulable->schedState = STATE_SCHEDULED;
      d_defaultPrio.push(schedulable);
    }
  }

  void deschedule(Schedulable *schedulable)
  {
    if (schedulable->schedState == STATE_SCHEDULED) {
      d_defaultPrio.remove(schedulable);
    } else if (schedulable->schedState == STATE_RUNNING) {
      d_running.remove(schedulable);
    }

    schedulable->schedState = STATE_UNSCHEDULED;
  }

  void runNext()
  {
    std::cout << "IoScheduler::runNext()\n";
    
    // Get the next schedulable that is up for running.
    Schedulable *schedulable = d_defaultPrio.popFront();

    // Check if it is not the tail.
    if (schedulable == NULL) {
      std::cout << "- schedule empty.\n";
      return;      
    }

    // Remove the schedulable from the schedule list.
    d_defaultPrio.remove(schedulable);

    // Add it to the running list.
    d_running.push(schedulable);

    // Set schedulable state to running.
    schedulable->schedState = STATE_RUNNING;
    
    // Get the schedulable callback.
    Callback callback = schedulable->schedCallback;
    void *data = schedulable->schedData;

    Result result = RESULT_DONE;

    // If the schedulable has a callback, run it.
    if (callback != NULL) {
      std::cout << "- running schedulable.\n";
      result = callback(schedulable, data);
    }

    // If this schedulable was descheduled in the mean time, return.
    if (schedulable->schedState == STATE_UNSCHEDULED) {
      return;
    }

    // Remove it from the running list.
    d_running.remove(schedulable);
    
    // If the schedulable has more work to do, reinsert it at the end of the schedule.
    if (result == RESULT_NOT_DONE) {
      std::cout << "Readding schedulable to schedule.\n";
      d_defaultPrio.push(schedulable);
      schedulable->schedState = STATE_SCHEDULED;
    } else {
      schedulable->schedState = STATE_UNSCHEDULED;
    }
  }

  bool empty() const
  {
    return d_defaultPrio.empty();
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

bool IoScheduler::empty() const
{
  return d->empty();
}
