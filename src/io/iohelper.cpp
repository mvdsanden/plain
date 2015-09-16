#include "iohelper.h"

#include "exceptions/errnoexception.h"

#include <unistd.h>

#include <sys/resource.h>

using namespace plain;

Poll::EventResultMask IoHelper::readToBuffer(int fd, char *buffer, size_t &offset, size_t length)
{
  int ret = read(fd, buffer + offset, length);

  if (ret == -1) {
    if (errno == EAGAIN) {
      return Poll::READ_COMPLETED;
    }

    throw ErrnoException(errno);
  }

  offset += ret;

  return Poll::NONE_COMPLETED;
}

size_t IoHelper::getFileDescriptorLimit()
{
  rlimit l;
  int ret = getrlimit(RLIMIT_NOFILE, &l);
  
  if (ret == -1) {
    throw ErrnoException(errno);
  }

  return l.rlim_cur;
}
