#include "scheduler.h"
#include "core/schedulable.h"
#include "core/private/schedulable.h"

#include <mutex>
#include <iostream>
#include <cassert>

using namespace plain;

struct Scheduler::Internal {

  std::mutex d_mutex;

  enum State {
    STATE_UNSCHEDULED = 0,    
    STATE_SCHEDULED = 1,    
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
	head[i].schedulable->next = &tail[i];
	head[i].schedulable->prev = NULL;
	tail[i].schedulable->prev = &head[i];
	tail[i].schedulable->next = NULL;
      }
    }

    /// Add the entry to the back of the scheduler list.
    /// Only if it is not already in the list.
    void push(Schedulable *entry)
    {
      // Add the entry to the secondary list.
      std::lock_guard<std::mutex> slk(mutex[s]);

      if (entry->schedulable->next != NULL) {
	// Already scheduled.
	//std::cout << "Push: already scheduled.\n";
	return;
      }
      
      tail[s].schedulable->prev->schedulable->next = entry;
      entry->schedulable->prev = tail[s].schedulable->prev;
      entry->schedulable->next = &tail[s];
      tail[s].schedulable->prev = entry;
    }

    Schedulable *popFront()
    {
      std::lock_guard<std::mutex> plk(mutex[p]);

      // Get the front entry.
      Schedulable *entry = head[p].schedulable->next;

      // Check if the list was empty.
      if (entry == &tail[p]) {
	std::lock_guard<std::mutex> slk(mutex[s]);
	
	// Swap lists.
	std::swap(p, s);

	// Get the new front entry.
	entry = head[p].schedulable->next;

	// If second list is also empty return NULL.
	if (entry == &tail[p]) {
	  return NULL;
	}
      }

      // Remove the front entry.
      head[p].schedulable->next = entry->schedulable->next;
      entry->schedulable->next->schedulable->prev = &head[p];

      entry->schedulable->next = NULL;
      entry->schedulable->prev = NULL;
      
      // Return the entry.
      return entry;
    }

    // \return true if the list is empty.
    bool empty() const
    {
      return (head[p].schedulable->next == &tail[p]) && (head[s].schedulable->next == &tail[s]);
    }
    
  };

  SchedList d_defaultPrio;

  Internal()
  {
  }

  void schedule(Schedulable *schedulable)
  {
    schedulable->schedulable->state = STATE_SCHEDULED;

    // If it is not already scheduled, add it to the schedule.
    schedulable->schedulable->scheduler = this;
    d_defaultPrio.push(schedulable);

    //    std::cout << "- Schedulable " << schedulable << " scheduled.\n";
  }

  void deschedule(Schedulable *schedulable)
  {
    schedulable->schedulable->state = STATE_UNSCHEDULED;
  }

  static void _resultCallback(Schedulable *schedulable, Schedulable::Result result)
  {
    Internal *obj = reinterpret_cast<Internal*>(schedulable->schedulable->scheduler);
    assert(obj != NULL);
    obj->resultCallback(schedulable, result);
  }

  void resultCallback(Schedulable *schedulable, Schedulable::Result result)
  {    
    // If the schedulable has more work to do, reinsert it at the end of the schedule.
    if (result == Schedulable::RESULT_NOT_DONE) {
      //      std::cout << "Readding schedulable " << schedulable << " to schedule.\n";
      schedulable->schedulable->state = STATE_SCHEDULED;
      d_defaultPrio.push(schedulable);
    }
  }
  
  void runNext()
  {
    //    std::cout << "Scheduler::runNext()\n";
    
    // Get the next schedulable that is up for running.
    Schedulable *schedulable = d_defaultPrio.popFront();

    // Check if it is not the tail.
    if (schedulable == NULL) {
      //      std::cout << "- schedule empty.\n";
      return;      
    }

    if (schedulable->schedulable->state == STATE_UNSCHEDULED) {
      //      std::cout << "- schedulable already unscheduled.\n";
      //resultCallback(schedulable, RESULT_REMOVED);
      return;
    }

    // Unschedule the scheulable, because from now on it can
    // be scheduled again.
    schedulable->schedulable->state = STATE_UNSCHEDULED;
    
    // Get the schedulable callback.
    Schedulable::Callback callback = schedulable->schedulable->callback;
    void *data = schedulable->schedulable->data;

    // If the schedulable has a callback, run it.
    if (callback != NULL) {
      //      std::cout << "- running schedulable.\n";
      callback(schedulable, data, _resultCallback);
    } else {
      //      std::cout << "- not running schedulable.\n";
      resultCallback(schedulable, Schedulable::RESULT_DONE);
    }
  }

  bool empty() const
  {
    return d_defaultPrio.empty();
  }
  
};

Scheduler::Scheduler()
  : d(new Internal)
{
}

Scheduler::~Scheduler()
{
}

void Scheduler::schedule(Schedulable *schedulable)
{
  d->schedule(schedulable);
}

void Scheduler::deschedule(Schedulable *schedulable)
{
  d->deschedule(schedulable);
}

void Scheduler::runNext()
{
  d->runNext();
}

bool Scheduler::empty() const
{
  return d->empty();
}
