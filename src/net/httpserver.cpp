#include "httpserver.h"

#include "exceptions/errnoexception.h"

#include <sys/types.h>
#include <sys/socket.h>

/** TODO: rename to HttpServer. */

using namespace plain;

enum {
  // This also signifies the max header length in bytes.
  DEFAULT_BUFFER_SIZE = 8192,
};

enum State {
  HTTP_STATE_CONNECTION_ACCEPTED = 0,
};

struct ClientContext {

  State state;

  char buffer[DEFAULT_BUFFER_SIZE];

  size_t bufferFill;

};

struct HttpServer::Internal {

  int d_port;

  int d_fd;

  sockaddr_in d_serverAddress;

  size_t d_clientTableSize;

  ClientContext *d_clientTable;

  Internal(int port)
    : d_port(port),
      d_fd(-1),
      d_clientTableSize(0),
      d_clientTable(NULL)
  {
    initializeClientTable();

    initializeServerSocket();
  }

  ~Internal()
  {
    if (d_fd != -1) {
      close(d_fd);
    }

    if (d_clientTable) {
      delete [] d_clientTable;
    }
  }

  void initalizeClientTable()
  {
    rlimit l;
    int ret = getrlimit(RLIMIT_NOFILE, &l);
  
    if (ret == -1) {
      throw ErrnoException(errno);
    }

    d_clientTableSize = l.rlim_max;

    // Allocate a table large enough to hold all file descriptors
    // that can possible be open at one time.
    d_clientTable = new ClientContext [ l.rlim_max ];

    // Initialize the table to zero.
    memset(d_clientTable, 0, l.rlim_max * sizeof(ClientContext));
  }

  void initalizeServerSocket()
  {
    memset(&d_serverAddress, 0, sizeof(d_serverAddress));
    d_serverAddress.sin_family = AF_INET;
    d_serverAddress.sin_port = htons(d_port);

    d_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (d_fd == -1) {
      throw ErrnoException(errno);
    }

    int ret = bind(d_fd, d_serverAddress, sizeof(sockaddr_in));

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    Main::instance().poll().add(d_fd, Poll::IN, _doServerAccept, this);
  }

  static Poll::EventResultMask _doServerAccept(int fd, uint32_t events, void *data)
  {
    HttpServer *obj = reinterpret_cast<HttpServer*>(data);
    return obj->doServerAccept(fd, events);
  }

  Poll::EventResultMask doServerAccept(int fd, uint32_t events)
  {
    sockaddr_in address;

    int fd = accept(d_fd,
		    &address, sizeof(address),
		    SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (fd == -1) {
      if (errno == EAGAIN) {
	return Poll::READ_COMPLETED;
      }

      throw ErrnoException(errno);
    }

    initializeNewConnection(fd, address);
    
    return Poll::NONE_COMPLETED;
  }

  void initializeNewConnection(int fd, sockaddr_in const &address)
  {
    if (fd >= d_clientTableSize) {
      throw std::runtime_error("file descriptor out of client table bounds");
    }

    ClientContext *context = d_clientTable + fd;

    context->state = HTTP_STATE_CONNECTION_ACCEPTED;
    context->bufferFill = 0;

    Main::instance().poll().add(fd, Poll::IN, _doClientReadHeader, this);
  }

  static Poll::EventResultMask _doClientReadHeader(int fd, uint32_t events, void *data)
  {
    HttpServer *obj = reinterpret_cast<HttpServer*>(data);
    return obj->doClientReadHeader(fd, events);
  }

  Poll::EventResultMask doClientReadHeader(int fd, uint32_t events)
  {
    ClientContext *context = d_clientTable + fd;

    // Read a chunk of data.
    Poll::EventResultMask result = IoHelper::readToBuffer(fd,
							  context->buffer,
							  context->bufferFill,
							  DEFAULT_BUFFER_SIZE - context->bufferFill);

    std::cout << "Current buffer fill: " << context->bufferFill << ".\n";

    if (context->bufferFill == DEFAULT_BUFFER_SIZE) {
      Main::instance().poll().remove(fd);
    }
    
    return result;
  }

};

HttpServer::HttpServer(int port)
  : d(new Internal(port))
{
}

HttpServer::~HttpServer()
{
}
