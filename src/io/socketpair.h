#ifndef __INC_PLAIN_SOCKETPAIR_H__
#define __INC_PLAIN_SOCKETPAIR_H__

namespace plain {

  class SocketPair {

    int d_fds[2];

  public:

    SocketPair();

    ~SocketPair();

    /**
     *  @returns the input file descriptor.
     */
    int fdIn() const { return d_fds[0]; }

    /**
     *  @returns the output file descriptor.
     */
    int fdOut() const { return d_fds[1]; }

  };

}

#endif // __INC_PLAIN_SOCKETPAIR_H__
