#include "iohelper.h"

#include "exceptions/errnoexception.h"

#include <unistd.h>

using namespace plain;

Poll::EventResultMask IoHelper::readToBuffer(int fd, char *buffer, size_t &offset, size_t length)
{
  int ret = read(fd, buffer + offset, length);

  if (ret == -1) {
    if (errno == EAGAIN) {
      return Poll::IN_COMPLETED;
    }

    throw ErrnoException(errno);
  }

  offset += ret;

  return Poll::NONE_COMPLETED;
}
