#ifndef __INC_PLAIN_IOHELPER_H__
#define __INC_PLAIN_IOHELPER_H__

#include "poll.h"

namespace plain {

  class IoHelper {
  public:

    /**
     *  Read from an non-blocking file descriptor.
     *
     *  @param fd the file descriptor, should be set to non-blocking io.
     *  @param buffer the buffer to read to.
     *  @param offset the offset to start writing to the buffer, this is updated by adding the number of bytes read.
     *  @param length the maximum number of bytes to read.
     */
    static Poll::EventResultMask readToBuffer(int fd, char *buffer, size_t &offset, size_t length);

    /**
     *  @return the maximum value a file descriptor can get.
     */
    static size_t getFileDescriptorLimit();
    
  };

};

#endif // __INC_PLAIN_IOHELPER_H__
