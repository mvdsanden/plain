#ifndef __INC_PLAIN_SCHEDULABLE_H__
#define __INC_PLAIN_SCHEDULABLE_H__

#include <memory>

namespace plain {

  class Schedulable {

    friend class Scheduler;
    
    struct Private;
    std::unique_ptr<Private> schedulable;

  public:

    Schedulable();

    ~Schedulable();
    
    /**
     *  The result of the schedulable callback.
     */
    enum Result {
      /**
       *  This should be returned by the callback when the schedulable is done and it
       *  should be removed from the schedule.
       */
      RESULT_DONE,

      /**
       *  This should be returned by the callback when the schedulable has more work to
       *  to and it should be reinserted in the schedule for further running.
       */
      RESULT_NOT_DONE,

    };

    /**
     *  The result callback type.
     *
     *  @param schedulable the schedulable involved.
     *  @param result the result of the callback.
     */
    typedef void (*ResultCallback)(Schedulable *schedulable, Result result);
    
    /**
     *  The callback type.
     *
     *  @param schedulable is the schedulable to which this callback belongs.
     */
    typedef void (*Callback)(Schedulable *schedulable, void *data, ResultCallback asyncResultCallback);

    /**
     *  Sets the schedulable callback and user data.
     */
    void setSchedulableCallback(Callback callback, void *data = 0);
    
  };

}

#endif // __INC_PLAIN_SCHEDULABLE_H__
