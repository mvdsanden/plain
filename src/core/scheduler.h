#ifndef __INC_PLAIN_SCHEDULER_H__
#define __INC_PLAIN_SCHEDULER_H__

#include <memory>
#include <atomic>

namespace plain {

  // Forward declaration.
  class Schedulable;
  
  class Scheduler {
  public:

    Scheduler();

    ~Scheduler();
    
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

#endif // __INC_PLAIN_SCHEDULER_H__
