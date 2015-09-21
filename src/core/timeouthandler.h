#ifndef __INC_PLAIN_TIMEOUT_HANDLER_H__
#define __INC_PLAIN_TIMEOUT_HANDLER_H__

#include <memory>

namespace plain {

  class TimeoutHandler {

    struct Private;
    std::unique_ptr<Private> timeoutHandler;
    
  public:

    TimeoutHandler();

    ~TimeoutHandler();

    /**
     *  Schedules a timeout event callback at now + duration.
     *
     *  @param timeoutable the Timeoutable object.
     *  @param duration the time interval after which to trigger the timeout.
     */
    void setTimeout(Timeoutable *timeoutable, std::chrono::steady_clock::duration const &duration);

    /**
     *  Schedules a timeout event callback at the specified time point.
     *
     *  @param timeoutable the Timeoutable object.
     *  @param time the time at which a timeout should be triggered.
     */
    void setTimeoutAt(Timeoutable *timeoutable, std::chrono::steady_clock::time_point const &time);

    /**
     *  This cancels the timeout for the specified Timeoutable.
     */
    void cancelTimeout(Timeoutable *timeoutable);

    /**
     *  Update the timeout handler.
     */
    void update();
    
  };

}

#endif // __INC_PLAIN_TIMEOUT_HANDLER_H__
