#include "socketpair.h"

#include "exceptions/errnoexception.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace plain;

SocketPair::SocketPair()
  : d_fds{-1, -1}
{
  // Create a non-blocking stream socket pair.
  int ret = socketpair(AF_UNIX,
		       SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
		       0,
		       d_fds);

  if (ret == -1) {
    throw ErrnoException(errno);
  }
}

SocketPair::~SocketPair()
{
  if (d_fds[0] != -1) {
    close(d_fds[0]);
  }

  if (d_fds[1] != -1) {
    close(d_fds[1]);
  }
}
