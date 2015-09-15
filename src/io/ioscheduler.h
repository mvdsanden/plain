#ifndef __INC_PLAIN_IOSCHEDULER_H__
#define __INC_PLAIN_IOSCHEDULER_H__

#include <memory>
#include <atomic>

namespace plain {

  class IoScheduler {
  public:

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

    // Forward declaration.
    struct Schedulable;

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
     *  The schedulable states.
     */
    enum State {
      /**
       *  The schedulable is not scheduled.
       */
      STATE_UNSCHEDULED = 0,

      /**
       *  The schedulable is currently in the schedule.
       */
      STATE_SCHEDULED = 1,

      STATE_COUNT,
      
    };

    /**
     *  The schedulable.
     *
     *  All schedulables should derive from this structure.
     */
    struct Schedulable {

      /**
       *  Private data pointer.
       */
      void *priv;
      
      /**
       *  The schedulable state.
       */
      std::atomic<int> schedState;
      
      /**
       * Callback that is called when the schedulable is run.
       */
      Callback schedCallback;

      /**
       *  User data member for the scheduler callback.
       */
      void *schedData;
      
      /**
       * Scheduling doubly linked list fields.
       */
      Schedulable *schedNext;
      Schedulable *schedPrev;

      Schedulable()
      : priv(NULL),
	schedState(STATE_UNSCHEDULED),
	schedCallback(NULL),
	schedData(NULL),
	schedNext(NULL),
	schedPrev(NULL)
      {
      }
      
    };

    IoScheduler();

    ~IoScheduler();
    
    /**
     *  This schedules the specified schedulable object to schedule for running.
     *
     *  When the schedulable is run its callback is called. It will stay scheduled is long
     *  as the callback returns RESULT_NOT_DONE or it is called as parameter to unschedule().
     *  All tasks will run in a round-robin fashion.
     *
     *  Note:
     *  This does not take ownership of schedulable. The schedulable should be a valid pointer
     *  as long as it is scheduled.
     */
    void schedule(Schedulable *schedulable);

    /**
     *  This will remove the schedulable from the schedule.
     *
     *  Note:
     *  The schedulable might still run once after is has been removed. Or it might be running
     *  at the moment it is removed. When this method is called from another thread.
     */
    void deschedule(Schedulable *schedulable);

    /**
     *  Runs the next scheduled schedulable.
     */
    void runNext();

    /**
     *  \returns true when nothing is scheduled to run.
     */
    bool empty() const;
    
  private:

    struct Internal;
    std::unique_ptr<Internal> d;

  };

}

#endif // __INC_PLAIN_IOSCHEDULER_H__
