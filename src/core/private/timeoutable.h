#ifndef __INC_PLAIN_PRIVATE_TIMEOUTABLE_H__
#define __INC_PLAIN_PRIVATE_TIMEOUTABLE_H__

#include "core/timeoutable.h"

#include <chrono>

namespace plain {

  struct Timeoutable::Private {

    friend class TimeoutHandler;
    
    /**
     *  Doubly linked list handles.
     */
    Timeoutable *next;
    Timeoutable *prev;

    /**
     *  The time point when this descriptor times out and schedules an IO event with the timeout bit set.
     */
    std::chrono::steady_clock::time_point timeout;

    Private()
      : next(NULL), prev(NULL)
      {
      }

    ~Private()
      {
      }      
    
  };

}

#endif // __INC_PLAIN_PRIVATE_TIMEOUT_ABLE_H__
