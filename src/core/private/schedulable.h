#ifndef __INC_PLAIN_PRIVATE_SCHEDULABLE_H__
#define __INC_PLAIN_PRIVATE_SCHEDULABLE_H__

#include "core/schedulable.h"

#include <atomic>

namespace plain {

  struct Schedulable::Private {

    friend class IoScheduler;

    /**
     *  The schedulable state.
     */
    std::atomic<int> state;
    
    /**
     *  Pointer to the scheduler in which this schedulable has been scheduled.
     */
    void *scheduler;
      
    /**
     * Callback that is called when the schedulable is run.
     */
    Callback callback;

    /**
     *  User data member for the scheduler callback.
     */
    void *data;
      
    /**
     * Scheduling doubly linked list fields.
     */
    Schedulable *next;
    Schedulable *prev;

    Private()
      : state(0),
	scheduler(NULL),
	callback(NULL),
	data(NULL),
	next(NULL),
	prev(NULL)
    {
    }
    
    void setCallback(Callback _callback, void *_data)
    {
      callback = _callback;
      data = _data;
    }
    
  };
  
}


#endif // __INC_PLAIN_PRIVATE_SCHEDULABLE_H__
