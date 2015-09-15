#include "ioscheduler.h"

#include <mutex>
#include <iostream>
#include <cassert>

using namespace plain;

struct IoScheduler::Internal {

  std::mutex d_mutex;

  enum State {

    STATE_REMOVED = STATE_COUNT,
    
  };

  // TODO: list does not need to be doubly-linked!
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
    /// Only if it is not already in the list.
    void push(Schedulable *entry)
    {
      // Add the entry to the secondary list.
      std::lock_guard<std::mutex> slk(mutex[s]);

      if (entry->schedNext != NULL) {
	// Already scheduled.
	//std::cout << "Push: already scheduled.\n";
	return;
      }
      
      tail[s].schedPrev->schedNext = entry;
      entry->schedPrev = tail[s].schedPrev;
      entry->schedNext = &tail[s];
      tail[s].schedPrev = entry;
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

      entry->schedNext = NULL;
      entry->schedPrev = NULL;
      
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

  Internal()
  {
  }

  void schedule(Schedulable *schedulable)
  {
    schedulable->schedState = STATE_SCHEDULED;

    // If it is not already scheduled, add it to the schedule.
    schedulable->priv = this;
    d_defaultPrio.push(schedulable);

    //    std::cout << "- Schedulable " << schedulable << " scheduled.\n";
  }

  void deschedule(Schedulable *schedulable)
  {
    schedulable->schedState = STATE_UNSCHEDULED;
  }

  static void _resultCallback(Schedulable *schedulable, Result result)
  {
    Internal *obj = reinterpret_cast<Internal*>(schedulable->priv);
    assert(obj != NULL);
    obj->resultCallback(schedulable, result);
  }

  void resultCallback(Schedulable *schedulable, Result result)
  {    
    // If the schedulable has more work to do, reinsert it at the end of the schedule.
    if (result == RESULT_NOT_DONE) {
      //      std::cout << "Readding schedulable " << schedulable << " to schedule.\n";
      schedulable->schedState = STATE_SCHEDULED;
      d_defaultPrio.push(schedulable);
    }
  }
  
  void runNext()
  {
    //    std::cout << "IoScheduler::runNext()\n";
    
    // Get the next schedulable that is up for running.
    Schedulable *schedulable = d_defaultPrio.popFront();

    // Check if it is not the tail.
    if (schedulable == NULL) {
      //      std::cout << "- schedule empty.\n";
      return;      
    }

    if (schedulable->schedState == STATE_UNSCHEDULED) {
      //      std::cout << "- schedulable already unscheduled.\n";
      //resultCallback(schedulable, RESULT_REMOVED);
      return;
    }

    // Unschedule the scheulable, because from now on it can
    // be scheduled again.
    schedulable->schedState = STATE_UNSCHEDULED;
    
    // Get the schedulable callback.
    Callback callback = schedulable->schedCallback;
    void *data = schedulable->schedData;

    // If the schedulable has a callback, run it.
    if (callback != NULL) {
      //      std::cout << "- running schedulable.\n";
      callback(schedulable, data, _resultCallback);
    } else {
      //      std::cout << "- not running schedulable.\n";
      resultCallback(schedulable, RESULT_DONE);
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
