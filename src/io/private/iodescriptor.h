#ifndef __INC_PLAIN_PRIVATE_IO_DESCRIPTOR_H__
#define __INC_PLAIN_PRIVATE_IO_DESCRIPTOR_H__

#include "utils/doublelinkedlist.h"

namespace plain {

  struct IoDescriptor::Private {

    /**
     *  The file descriptor associated with the IO descriptor.
     */
    int fd;

    /**
     *  IO event handler context.
     */
    struct {
      /**
       *  Or-ed mask of event types that should trigger a callback.
       */
      uint32_t mask;

      /**
       *  A mask of currently active events.
       */
      uint32_t state;

      /**
       *  The callback to be called to handle events.
       */
      EventCallback callback;

      /**
       *  User data to pass on to the callback.
       */
      void *userData;

      /**
       *  Private data for the polling handler.
       */
      void *pollData;
    } eventHandler;
    
    /**
     *  Handles for the timeout list.
     */
    struct {
      
    } timeoutHandler;
    
    void setFileDescriptor(int _fd)
    {
      fd = _fd;
    }

    int fileDescriptor() const
    {
      return fd;
    }

    void setEventHandler(uint32_t eventMask, EventCallback callback, void *data)
    {
      eventHandler.mask = eventMask;
      eventHandler.callback = callback;
      eventHandler.data = data;
    }

    Private()
      : fd(-1)
      {
	// Initialize event handler to NULL.
       	eventHandler.mask = 0;
	eventHandler.state = 0;
	eventHandler.callback = NULL;
	eventHandler.userData = NULL;
	eventHandler.pollData = NULL;

	// Initialize timeout handles to NULL.
	timeoutHandles.next = NULL;
	timeoutHandles.prev = NULL;	
      }
    
  };

}

#endif // __INC_PLAIN_PRIVATE_IO_DESCRIPTOR_H__
