#ifndef __INC_PLAIN_POLL_H__
#define __INC_PLAIN_POLL_H__

#include <memory>

#include <sys/epoll.h>

namespace plain {

  class Poll {
  public:

    // Returned by event handlers.
    enum EventResultMask {
      /// Signals back to the event scheduler that all events are still active.
      NONE_COMPLETED = 0,

      /// Signals back to the event scheduler that the read event was completed
      /// this should be returned when a read() call returns EAGAIN.
      READ_COMPLETED = 1,

      // Signals back to the event scheduler that the write event was completed
      // this should be returned when a write() call returns EAGAIN.
      WRITE_COMPLETED = 2,

      // Signals back to the event scheduler that this descriptor should be
      // removed from the polling system.
      REMOVE_DESCRIPTOR = 127,

      // Signals back to the event scheduler that this descriptor should be
      // closed and removed from the polling system.
      CLOSE_DESCRIPTOR = 255,
    };

    /// Event masks.
    enum EventMask {
      /// A read call would not block.
      IN = EPOLLIN,

      /// Priorty data available in the descriptor buffer.
      PRI = EPOLLPRI,

      /// A write call would not block.
      OUT = EPOLLOUT,

      /// An error occured with the file descriptor.
      ERR = EPOLLERR,

      /// The read side of the descriptor was closed by the other side.
      RDHUP = EPOLLRDHUP,

      /// The other side hang up (closed the connection).
      HUP = EPOLLHUP,

      /// The file descriptor timed out.
      TIMEOUT = EPOLLET,
    };

    /**
     *  The asynchronous result callback for an IO event.
     *
     *  @param fd the file descriptor of the IO event.
     *  @param result the result of the event.
     */
    typedef void (*EventResultCallback)(int fd, EventResultMask result);

    struct AsyncResult {
      virtual void completed(EventResultMask result) = 0;
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
    typedef void (*EventCallback)(int fd, uint32_t events, void *data, AsyncResult &asyncResult);
   
    
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
     *  Closes the file descriptor and removes the event handler for the specified file descriptor.
     */
    void close(int fd);

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
