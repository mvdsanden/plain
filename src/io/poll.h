#ifndef __INC_PLAIN_POLL_H__
#define __INC_PLAIN_POLL_H__

#include <memory>

#include <sys/epoll.h>

namespace plain {

  class Poll {
  public:

    enum EventResultMask {
      NONE_COMPLETED = 0,
      READ_COMPLETED = 1,
      WRITE_COMPLETED = 2,
      CLOSE_COMPLETED = 255,
    };

    enum EventMask {
      IN = EPOLLIN,
      OUT = EPOLLOUT,
      ERR = EPOLLERR,
      HUP = EPOLLHUP,
    };

    /**
     *  The IO event callback.
     *
     *  @param fd the file descriptor.
     *  @param events the event mask.
     *  @param data the user data pointer.
     *  @return the result mask.
     *
     *  The event callback should return a mask which indicates if a
     *  operation is completed. When read() (or equivalent) returns with the EAGAIN error
     *  code it should return READ_COMPLETED. When write() (or equivalent) returns with the
     *  EAGAIN error code it should return WRITE_COMPLETED.
     */
    typedef EventResultMask (*EventCallback)(int fd, uint32_t events, void *data);
   
    
    Poll();

    ~Poll();

    /**
     *  Add an event handler to the poll list for the specified file descriptor.
     *
     *  @param fd the file descriptor to poll.
     *  @param events the events to poll for.
     *  @param callback the callback to call on an IO event.
     *  @param data the user data pointer to pass on to the callback.
     *
     *  Note: there can be only one event handler for a file descriptor. An
     *        exception will be thrown when a second one is registered.
     *
     */
    void add(int fd, uint32_t events, EventCallback callback, void *data = 0);

    /**
     *  Modify an event handler for the specified file descriptor.
     *
     *  @param fd the file descriptor to poll.
     *  @param events the events to poll for.
     *  @param callback the callback to call on an IO event (when NULL it is not changed).
     *  @param data the user data pointer to pass on to the callback (when NULL it is not changed).
     */
    void modify(int fd, uint32_t events, EventCallback callback = 0, void *data = 0);

    /**
     *  Removes the event handler for the specified file descriptor.
     */
    void remove(int fd);

    /**
     *  Run the poll.
     *
     *  @param timeout the time to wait for events in milliseconds.
     *  @return true when a timeout occured.
     */
    bool update(int timeout);

  private:

    struct Internal;
    std::unique_ptr<Internal> internal;

  };

}

#endif // __INC_PLAIN_POLL_H__
