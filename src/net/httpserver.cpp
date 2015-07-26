#include "httpserver.h"
#include "io/poll.h"
#include "io/iohelper.h"
#include "core/main.h"
#include "http.h"
#include "httprequest.h"
#include "httprequesthandler.h"

#include "exceptions/errnoexception.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <unistd.h>

#include <sys/time.h>
#include <sys/resource.h>

#include <string.h>

#include <iostream>
#include <iomanip>
#include <unordered_map>

/** TODO: rename to HttpServer. */

using namespace plain;

enum {
  // This also signifies the max header length in bytes.
  DEFAULT_BUFFER_SIZE = 8192,

  // Default backlog size of the server socket.
  DEFAULT_BACKLOG = 1024,

  // Try to accept up to this number of connections per io event.
  DEFAULT_ACCEPTS_PER_EVENT = 16,

  END_OF_HEADER_MARKER = ('\r' | '\n' << 8 | '\r' << 16 | '\n' << 24),
};

enum State {
  HTTP_STATE_CONNECTION_ACCEPTED = 0,
  HTTP_STATE_HEADER_RECEIVED = 1,
  HTTP_STATE_SENDING_RESPONSE = 2,
};

enum HeaderField {
  HTTP_HEADER_FIELD_HOST = 0,
  HTTP_HEADER_FIELD_CONNECTION = 1,
  HTTP_HEADER_FIELD_CONTENT_LENGTH = 2,
  
  HTTP_HEADER_FIELD_COUNT,
};

/*
 *  This contains the client connection context.
 */
struct ClientContext {

  // The current state of the connection.
  State state;

  // The client connection buffer.
  char buffer[DEFAULT_BUFFER_SIZE + 4] __attribute__((aligned(16)));

  // The current fill of the buffer in bytes.
  size_t bufferFill;

  // Header fields.
  HttpRequest request;

  char const *sendBuffer;
  size_t sendBufferSize;
  size_t sendBufferPosition;

};

struct HttpServer::Internal {

  // The port the server runs on.
  int d_port;

  // The request handler object.
  std::shared_ptr<HttpRequestHandler> d_requestHandler;

  // The server file descriptor.
  int d_fd;

  // The server socket address.
  sockaddr_in d_serverAddress;

  // The size of the client connection table.
  size_t d_clientTableSize;

  // The client connection table. This table is pre allocated to save on
  // memory managerment complexity. Also this guarantees that losing a
  // connection does not cause memory leaks.
  ClientContext *d_clientTable;

  std::unordered_map<std::string, size_t> d_headerFieldTable;

  Internal(int port, std::shared_ptr<HttpRequestHandler> const &requestHandler)
    : d_port(port),
      d_requestHandler(requestHandler),
      d_fd(-1),
      d_clientTableSize(0),
      d_clientTable(NULL)
  {
    initialzieHeaderFieldTable();
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

  void initialzieHeaderFieldTable()
  {
    d_headerFieldTable["host"] = HTTP_HEADER_FIELD_HOST;
    d_headerFieldTable["connection"] = HTTP_HEADER_FIELD_CONNECTION;
    d_headerFieldTable["content-length"] = HTTP_HEADER_FIELD_CONTENT_LENGTH;
  }

  /*
   *  Initializes the client table to have entries for all possible file descriptors.
   */
  void initializeClientTable()
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

  /*
   *  Creates the server socket, binds it to the specified port and starts listening for connections.
   */
  void initializeServerSocket()
  {
    // Initialize the server socket address.
    memset(&d_serverAddress, 0, sizeof(d_serverAddress));
    d_serverAddress.sin_family = AF_INET;
    d_serverAddress.sin_port = htons(d_port);

    // Create the socket descriptor.
    d_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (d_fd == -1) {
      throw ErrnoException(errno);
    }

    // Set a socket option so that we will reuse the socket if it did not close correctly before.
    int val = 1;
    setsockopt(d_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // Bind the socket to the address.
    int ret = bind(d_fd,
		     reinterpret_cast<sockaddr*>(&d_serverAddress),
		     sizeof(sockaddr_in));

    if (ret == -1) {
      throw ErrnoException(errno);
    }
    
    // Start listening on the socket.
    ret = listen(d_fd, DEFAULT_BACKLOG);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    // Add the socket to the polling list so we get events on connection attempts.
    Main::instance().poll().add(d_fd, Poll::IN, _doServerAccept, this);
  }

  static Poll::EventResultMask _doServerAccept(int fd, uint32_t events, void *data)
  {
    HttpServer::Internal *obj = reinterpret_cast<HttpServer::Internal*>(data);
    return obj->doServerAccept(fd, events);
  }

  /*
   *  Accepts a new connections.
   */
  Poll::EventResultMask doServerAccept(int fd, uint32_t events)
  {
    // So we don't need to go through the whole event handeling chain we
    // try to accept multiple connections.
    for (size_t i = 0; i < DEFAULT_ACCEPTS_PER_EVENT; ++i) {
      sockaddr address;
      socklen_t addressLength = 0;

      int clientFd = accept4(d_fd,
			     &address, &addressLength,
			     SOCK_NONBLOCK | SOCK_CLOEXEC);

      if (clientFd == -1) {
	if (errno == EAGAIN) {
	  return Poll::READ_COMPLETED;
	}

	throw ErrnoException(errno);
      }

      initializeNewConnection(clientFd, address, addressLength);
    }

    // More accepts waiting but yielding back to the IO event scheduler to
    // not hold up handeling of other events.
    return Poll::NONE_COMPLETED;
  }

  /*
   *  Initializes the client context for a new connection.
   */
  void initializeNewConnection(int fd, sockaddr const &address, socklen_t addressLength)
  {
    // Just in case check if the file descriptor is in bounds.
    if (fd >= d_clientTableSize) {
      throw std::runtime_error("file descriptor out of client table bounds");
    }

    std::cout << "Accepted new connection.\n";

    // Get the client context from the table.
    ClientContext *context = d_clientTable + fd;

    resetConnection(context);

    // Add an event to read the incomming header data.
    Main::instance().poll().add(fd, Poll::IN, _doClientReadHeader, this);
  }

  void resetConnection(ClientContext *context)
  {
    // Zero the structure.
    memset(context, 0, sizeof(ClientContext));

    // Set the initial state.
    context->state = HTTP_STATE_CONNECTION_ACCEPTED;

    context->request.setFd(context-d_clientTable);
  }

  static Poll::EventResultMask _doClientReadHeader(int fd, uint32_t events, void *data)
  {
    HttpServer::Internal *obj = reinterpret_cast<HttpServer::Internal*>(data);
    return obj->doClientReadHeader(fd, events);
  }

  void printHex(char const *buffer, size_t count)
  {
    char const *end = buffer + count;
    for (char const *i = buffer; i != end; ++i) {
      std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*i) << " " << std::dec;
    }
    std::cout << ".\n";
  }

  /*
   *  Checks if the end of header sequence can be found in the buffer in the specified range.
   */
  int findEndOfHeader(char const *buffer, size_t offset, size_t count)
  {
    size_t marg = std::min<size_t>(offset, 4);
    offset -= marg;
    count += marg;

    if (count < 4) {
      return -1;
    }

    char const *end = buffer + offset + count - 3;
    for (char const *i = buffer + offset; i != end; ++i) {
      if (*reinterpret_cast<uint32_t const *>(i) == END_OF_HEADER_MARKER) {
	return i - buffer;
      }
    }

    return -1;
  }

  /*
   *  Reads the header.
   */
  Poll::EventResultMask doClientReadHeader(int fd, uint32_t events)
  {
    ClientContext *context = d_clientTable + fd;

    size_t bufferFill = context->bufferFill;

    // Read a chunk of data.
    Poll::EventResultMask result = IoHelper::readToBuffer(fd,
							  context->buffer,
							  context->bufferFill,
							  DEFAULT_BUFFER_SIZE - context->bufferFill);

    std::cout << fd << ": current buffer fill: " << context->bufferFill << ".\n";

    // Check if the buffer contains the "\r\n\r\n" sequence that indicates the end of the header.
    int endOfHeaderOffset = findEndOfHeader(context->buffer, bufferFill, context->bufferFill - bufferFill);

    // The header is received.
    if (endOfHeaderOffset != -1) {
      std::cout << "Header received.\n";
      context->state = HTTP_STATE_HEADER_RECEIVED;

      parseHttpHeader(context);

      //      context->state = HTTP_STATE_HEADER_PARSED;

      if (d_requestHandler) {
	d_requestHandler->request(context->request);
	result = Poll::READ_COMPLETED;
      } else {
	close(fd);
	result = Poll::CLOSE_COMPLETED;
      }
    }

    // Read did not return EAGAIN, but it returned zero bytes read. Assume
    // client has disconnected.
    if (result != Poll::READ_COMPLETED && context->bufferFill == bufferFill) {
      // This means the connection is closed from the other side.
      close(fd);
      result = Poll::CLOSE_COMPLETED;
    }

    // Buffer is full without end of header.
    if (context->bufferFill == DEFAULT_BUFFER_SIZE) {
      close(fd);
      result = Poll::CLOSE_COMPLETED;
    }
    
    return result;
  }

  void parseHttpHeader(ClientContext *context)
  {
    char *head = context->buffer;
    char const *end = context->buffer + context->bufferFill;

    // Parse the HTTP method.
    char *method = head;
    while (head != end && *head != ' ') ++head;
    size_t methodLength = head - method;
    *(head++) = 0;

    if (methodLength > 4) {
      throw std::runtime_error("malformed header");
    }

    // Parse request uri.
    char *uri = head;
    while (head != end && *head != ' ') ++head;
    *(head++) = 0;

    // Sanity check?
    char *http = head;
    while (head != end && *head != '/') ++head;
    *(head++) = 0;    

    if (head - http != 5) {
      throw std::runtime_error("malformed headers");
    }

    // Http version.
    char *version = head;
    while (head != end && *head != '\r') ++head;
    size_t versionLength = head - version;
    *(head++) = 0;

    if (versionLength != 3) {
      throw std::runtime_error("unsupported HTTP version");
    }

    if (*head != '\n') {
      throw std::runtime_error("malformed headers");
    }

    ++head;

    while (head != end) {

      if (*head == '\r') {
	++head;

	if (*head != '\n') {
	  throw std::runtime_error("malformed headers");
	}

	++head;
	break;
      }

      char *key = head;
      while (head != end && *head != ':') *head = tolower(*head), ++head;
      *(head++) = 0;

      while (head != end && *head == ' ') ++head;

      char *value = head;
      while (head != end && *head != '\r') ++head;
      *(head++) = 0;

      if (*head != '\n') {
	throw std::runtime_error("malformed headers");
      }

      std::cout << key << "=" << value << ".\n";

      auto i = d_headerFieldTable.find(key);
      if (i != d_headerFieldTable.end()) {
	switch (i->second) {
	case HTTP_HEADER_FIELD_HOST:
	  context->request.setHost(value);
	  break;

	case HTTP_HEADER_FIELD_CONNECTION:
	  if (strcmp(value, "keep-alive") == 0) {
	    context->request.setConnection(Http::CONNECTION_KEEP_ALIVE);
	  }
	  break;

	case HTTP_HEADER_FIELD_CONTENT_LENGTH:
	  context->request.setContentLength(strtoull(value, NULL, 10));
	  break;
	};
      }

      ++head;
    }

    // Check sanity.
    if (*reinterpret_cast<uint32_t const *>(http) != ('H' | 'T' << 8 | 'T' << 16 | 'P' << 24)) {
      throw std::runtime_error("malformed headers");
    }

    context->request.setVersion(Http::parseVersion(version, versionLength));
    if (context->request.version() == Http::VERSION_UNKNOWN) {
      throw std::runtime_error("unsupported HTTP version");
    };

    context->request.setMethod(Http::parseMethod(method, methodLength));
    if (context->request.method() == Http::METHOD_UNKNOWN) {
      throw std::runtime_error("unsupported request method");
    }

    std::cout << "method=" << method << " (" << context->request.method() << ").\n";
    std::cout << "uri=" << uri << ".\n";
    std::cout << "http=" << http << ".\n";
    std::cout << "version=" << version << " (" << context->request.version() << ").\n";
    std::cout << "host=" << context->request.host() << ".\n";
    std::cout << "connection=" << context->request.connection() << ".\n";
    std::cout << "contentLength=" << context->request.contentLength() << ".\n";

    

  }

  void respondWithStaticString(HttpRequest const &request, const char *str, size_t length)
  {

    if (request.fd() < 0 || request.fd() > d_clientTableSize) {
      throw std::runtime_error("file descriptor out of bounds");
    }

    ClientContext *context = d_clientTable + request.fd();

    context->sendBuffer = str;
    context->sendBufferSize = length;
    context->sendBufferPosition = 0;

    context->state = HTTP_STATE_SENDING_RESPONSE;

    // Add an event to read the incomming header data.
    Main::instance().poll().modify(request.fd(), Poll::OUT, _doClientWriteStaticString, this);
  }

  static Poll::EventResultMask _doClientWriteStaticString(int fd, uint32_t events, void *data)
  {
    HttpServer::Internal *obj = reinterpret_cast<HttpServer::Internal*>(data);
    return obj->doClientWriteStaticString(fd, events);
  }

  Poll::EventResultMask doClientWriteStaticString(int fd, uint32_t events)
  {
    ClientContext *context = d_clientTable + fd;

    int ret = write(fd, context->sendBuffer, context->sendBufferSize - context->sendBufferPosition);

    if (ret == -1) {
      if (errno == EAGAIN) {
	return Poll::WRITE_COMPLETED;
      }

      close(fd);
      return Poll::CLOSE_COMPLETED;
    } else if (ret == 0) {
      close(fd);
      return Poll::CLOSE_COMPLETED;
    }

    context->sendBufferPosition += ret;

    if (context->sendBufferPosition == context->sendBufferSize) {
      if (context->request.connection() == Http::CONNECTION_KEEP_ALIVE) {
	resetConnection(context);

	// Add an event to read the incomming header data.
	Main::instance().poll().modify(context-d_clientTable, Poll::IN, _doClientReadHeader, this);

	return Poll::WRITE_COMPLETED;
      }

      close(fd);
      return Poll::CLOSE_COMPLETED;
    }

    return Poll::NONE_COMPLETED;
  }

};

HttpServer::HttpServer(int port, std::shared_ptr<HttpRequestHandler> const &requestHandler)
  : d(new Internal(port, requestHandler))
{
  requestHandler->setServerInstance(this);
}

HttpServer::~HttpServer()
{
}

void HttpServer::respondWithStaticString(HttpRequest const &request, const char *str, size_t length)
{
  d->respondWithStaticString(request, str, length);
}
