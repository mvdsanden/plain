#ifndef __INC_PLAIN_IO_DESCRIPTOR_H__
#define __INC_PLAIN_IO_DESCRIPTOR_H__

#include "core/schedulable.h"
#include "core/timeoutable.h"

#include <memory>

#include <sys/epoll.h>

namespace plain {

  class IoDescriptor : public Schedulable, public Timeoutable {

    struct Private;
    std::unique_ptr<Private> ioDescriptor;
    
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
     *  The asynchronous result handler for an IO event.
     *
     *  @param result the result of the event.
     */
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
    
    IoDescriptor();

    ~IoDescriptor();

    /**
     *  Associate a file descriptor with the IO descriptor.
     */
    void setFileDescriptor(int fd);

    /**
     *  @return the associated file descriptor.
     */
    int fileDescriptor() const;

    /**
     *  Sets a event mask and a callback for IO events on this descriptor.
     *
     *  @param eventMask orred mask of event types (@see EventMask).
     *  @param callback the callback that is called when one of the events is triggered.
     *  @param data the user data associated with the event callback.
     */
    void setEventHandler(uint32_t eventMask, EventCallback callback, void *data = 0);
    
  };

}

#endif // __INC_PLAIN_IO_DESCRIPTOR_H__
