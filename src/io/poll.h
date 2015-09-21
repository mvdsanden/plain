#ifndef __INC_PLAIN_POLL_H__
#define __INC_PLAIN_POLL_H__

#include <memory>

namespace plain {

  class Poll {
  public:

   
    
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
