#include "httpfilesystemresponsehandler.h"

#include "httprequest.h"
#include "io/poll.h"
#include "core/main.h"
#include "http.h"

#include "exceptions/errnoexception.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <iostream>

using namespace plain;

struct HttpFilesystemResponseHandler::Internal {

  struct ClientContext {

    ClientContext()
      : sourceFd(-1),
	destinationFd(-1)
    {
    }
    
    int sourceFd;
    int destinationFd;
    
  };

  size_t d_clientTableSize;
  ClientContext *d_clientTable;

  Internal()
    : d_clientTableSize(0),
      d_clientTable(NULL)
  {
    // Allocate the client context table.
    d_clientTableSize = IoHelper::getFileDescriptorLimit();
    d_clientTable = new ClientContext [ d_clientTableSize ];
  }

  ~Internal()
  {
    delete [] d_clientTableSize;
  }
  
#define IO_EVENT_HANDLER(NAME)\
  static void _##NAME(int fd, uint32_t events, void *data, Poll::AsyncResult &asyncResult) \
  {\
    HttpServer::Internal *obj = reinterpret_cast<HttpServer::Internal*>(data);\
    return obj->NAME(fd, events, asyncResult);					\
  }\
  void NAME(int fd, uint32_t events, Poll::AsyncResult &asyncResult)		\

  void cork(int fd)
  {
    //    std::cout << "cork(" << fd << ").\n";
    int state = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
  }

  void uncork(int fd)
  {
    //    std::cout << "uncork(" << fd << ").\n";
    int state = 0;
    setsockopt(fd, IPPROTO_TCP, TCP_CORK, &state, sizeof(state));
  }
  
  void respondWithFile(HttpRequest const &request, std::string const &filename)
  {
    // Check if the file descriptor is in bounds.
    if (request.fd() < 0 || request.fd() > d_clientTableSize) {
      throw std::runtime_error("file descriptor out of bounds");
    }

    // Get the client context associated with the file descriptor.
    ClientContext *context = d_clientTable + request.fd();

    //    std::cout << "Request fd=" << request.fd() << ".\n";
    
    // Open the file.
    int fileFd = open(path.c_str(), O_RDONLY);

    if (fileFd == -1) {
      throw ErrnoException(errno);
    }

    std::cout << "Opening " << fileFd << " (respondWithFile).\n";
    
    struct stat st;
    int ret = fstat(fileFd, &st);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    // Get the length of the file in bytes.
    context->contentLength = st.st_size;
    
    //    std::cout << "Source file fd=" << fileFd << ".\n";
    
    int pipeFds[2];
    
    // Create an intermediate pipe.
    ret = pipe2(pipeFds, O_NONBLOCK | O_CLOEXEC);

    if (ret == -1) {
      close(fileFd);
      throw ErrnoException(errno);
    }

    // TODO: make pipe buffer size dependent on file size?
    fcntl(pipeFds[0], F_SETPIPE_SZ, DEFAULT_PIPE_BUFFER_SIZE);
    fcntl(pipeFds[1], F_SETPIPE_SZ, DEFAULT_PIPE_BUFFER_SIZE);
    
    std::cout << "Opening " << pipeFds[0] << " (pipe[0]).\n";
    std::cout << "Opening " << pipeFds[1] << " (pipe[1]).\n";
    
    //    std::cout << "Pipe fd0=" << pipeFds[0] << ", fd1=" << pipeFds[1] << ".\n";
    
    ClientContext *pipeInContext = d_clientTable + pipeFds[1];
    ClientContext *pipeOutContext = d_clientTable + pipeFds[0];

    pipeInContext->sourceFd = fileFd;
    pipeOutContext->destinationFd = request.fd();
    context->sourceFd = pipeFds[0];

    try {
      // Create the response headers.
      Http::Response response(context->buffer, DEFAULT_BUFFER_SIZE, 200, "Okay");
      response.addHeaderField("Content-Length", context->contentLength);
      response.addHeaderField("Connection", "keep-alive");
      
      // Set the buffer fill to the header size.
      context->bufferFill = response.size();

      // Set the send buffer.
      context->sendBuffer = context->buffer;
      context->sendBufferSize = context->bufferFill;
      context->sendBufferPosition = 0;

      //      std::cout << "- Sending header...\n";
      
      // Asynchronously write the header to the socket.
      Main::instance().poll().modify(request.fd(), Poll::OUT, _doWriteHeader, this);
      Main::instance().poll().add(pipeFds[1], Poll::OUT, _doCopyFromSource, this);
    } catch (...) {
      std::cout << "Closing " << fileFd << ".\n";
      std::cout << "Closing " << pipeFds[0] << ".\n";
      std::cout << "Closing " << pipeFds[1] << ".\n";
      close(fileFd);
      close(pipeFds[0]);
      close(pipeFds[1]);
      throw;
    }

  }

  IO_EVENT_HANDLER(doWriteHeader)
  {
    //    std::cout << "doWriteHeader()\n";
    
    ClientContext *context = d_clientTable + fd;

    if (events & Poll::TIMEOUT) {
      //      close(fd);
      std::cout << "closing " << fd << ".\n";
      asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
      return;
    }

    cork(fd);
    
    // Write part of the buffer.
    int ret = write(fd, context->sendBuffer + context->sendBufferPosition, context->sendBufferSize - context->sendBufferPosition);

    if (ret == -1) {
      if (errno == EAGAIN) {
	// Non blocking behavior, so we need to wait for the socket to become writable again.
	asyncResult.completed(Poll::WRITE_COMPLETED);
      } else if (errno == EPIPE) {
	// Connection was dropped.
	std::cout << "closing " << fd << ".\n";
	asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
      } else {
	//      std::cout << "- Error writing header.\n";
	// TODO: log error.
	// Another error occured, close the file descriptor.
	//      close(fd);
	std::cout << "closing " << fd << ".\n";
	asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
      }
      return;
    } else if (ret == 0) {
      // Zero write, socket probably has closed
      //      close(fd);
      //      std::cout << "- Connection closed while writing header.\n";
      std::cout << "closing " << fd << ".\n";
      asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
      return;
    }

    // Update the send buffer position.
    context->sendBufferPosition += ret;

    // Check if we are done sending data.
    if (context->sendBufferPosition == context->sendBufferSize) {
      context->sendBufferPosition = 0;
      context->sendBufferSize = context->contentLength;
      //      std::cout << "- Done sending header (setting pipe ready event for " << context->sourceFd << ").\n";      
      Main::instance().poll().add(context->sourceFd, Poll::IN, _doPipeReady, this);
      Main::instance().poll().modify(fd, 0, _doCopyFromPipeToSocket, this);
      asyncResult.completed(Poll::REMOVE_DESCRIPTOR);
      return;
    }

    asyncResult.completed(Poll::NONE_COMPLETED);
  }

    IO_EVENT_HANDLER(doPipeReady)
  {
    //    std::cout << "- doPipeReady().\n";
    ClientContext *context = d_clientTable + fd;
    Main::instance().poll().add(context->destinationFd, Poll::OUT, _doCopyFromPipeToSocket, this);
    asyncResult.completed(Poll::REMOVE_DESCRIPTOR);
  }

  IO_EVENT_HANDLER(doCopyFromPipeToSocket)
  {
    //    std::cout << "- doCopyFromPipeToSocket(" << fd << ", " << events << ").\n";
    ClientContext *context = d_clientTable + fd;

    //    std::cout << "splice(" << context->sourceFd << ", " << fd << ").\n";

    for (size_t i = 0; i < DEFAULT_SPLICE_COUNT; ++i) {
    
      ssize_t ret = splice(context->sourceFd,
			   NULL,
			   fd,
			   NULL,
			   DEFAULT_CHUNK_SIZE,
			   SPLICE_F_MOVE | SPLICE_F_MORE | SPLICE_F_NONBLOCK);

      if (ret == -1) {
	if (errno == EAGAIN) {
	  // Check if the destination socket would block or if the source pipe would block.
	  pollfd p = {fd, POLLOUT, 0};
	  while (true) {
	    int pret = poll(&p, 1, 0);
	    if (pret == -1) {
	      if (errno == EINTR) {
		continue;
	      }
	      throw ErrnoException(errno);
	    }
	    break;
	  }
	
	  if (p.revents & POLLIN == 0) {
	    // Socket write would block, wait for the socket buffer to free up.
	    asyncResult.completed(Poll::WRITE_COMPLETED);
	  } else {
	    // Pipe read would block, we should wait for the pipe buffer to fill up.
	    Main::instance().poll().add(context->sourceFd, Poll::IN, _doPipeReady, this);
	    asyncResult.completed(Poll::REMOVE_DESCRIPTOR);
	  }
	  return;
	} else if (errno == EPIPE || errno == ECONNRESET) {
	  goto closed;
	}
	throw ErrnoException(errno);
      } else if (ret == 0) {
	goto closed;
      }

      //    std::cout << "Send " << ret << " bytes of " << context->sendBufferSize << ".\n";
    
      context->sendBufferPosition += ret;
    
      // Check if we are done sending data.
      if (context->sendBufferPosition >= context->sendBufferSize) {
	//	std::cout << "- Content done.\n";

	uncork(fd);
      
	std::cout << "Closing " << context->sourceFd << ".\n";
	close(context->sourceFd);
      
	if (context->request.connection() == Http::CONNECTION_KEEP_ALIVE) {
	  // We have a keep alive connection, so reset the connection state to expect
	  // a new request.
	  resetConnection(context);

	  // Modify the poll event handler to wait for input data.
	  Main::instance().poll().modify(fd, Poll::IN | Poll::TIMEOUT, _doClientReadHeader, this);

	  // Indicate that the write was completed and we do not need another iteration.
	  asyncResult.completed(Poll::WRITE_COMPLETED);
	  return;
	}

	// Connection is not keep-alive, so clode the socket and indicate this back to the poll system.
	//      close(fd);
	//      std::cout << "closing " << fd << ".\n";
	asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
	return;
      }

    }

    asyncResult.completed(Poll::NONE_COMPLETED);
    return;

  closed:
    std::cout << "Closing " << context->sourceFd << ".\n";
    //Main::instance().poll().close(context->sourceFd);
    close(context->sourceFd);
    asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
  }

  IO_EVENT_HANDLER(doCopyFromSource)
  {
    //    std::cout << "- doCopyFromSource().\n";
    ClientContext *context = d_clientTable + fd;

    for (size_t i = 0; i < DEFAULT_SPLICE_COUNT; ++i) {
    
      ssize_t ret = splice(context->sourceFd,
			   NULL,
			   fd,
			   NULL,
			   DEFAULT_CHUNK_SIZE,
			   SPLICE_F_MOVE | SPLICE_F_NONBLOCK);

      if (ret == -1) {
	if (errno == EAGAIN) {
	  asyncResult.completed(Poll::WRITE_COMPLETED);
	  return;
	} else if (errno == EPIPE || errno == ECONNRESET) {
	  goto closed;
	}
	throw ErrnoException(errno);
      } else if (ret == 0) {
	goto closed;
      }

    }
    
    asyncResult.completed(Poll::NONE_COMPLETED);
    return;
    
  closed:
    std::cout << "Closing " << context->sourceFd << ".\n";
    close(context->sourceFd);
    asyncResult.completed(Poll::CLOSE_DESCRIPTOR);
  }
  
};

HttpFilesystemResponseHandler::HttpFilesystemResponseHandler()
  : d(new Internal)
{
}

HttpFilesystemResponseHandler &HttpFilesystemResponseHandler::instance()
{
  static HttpFilesystemResponseHandler s_instance;
  return s_instance;
}

HttpFilesystemResponseHandler::~HttpFilesystemResponseHandler()
{
}

void HttpFilesystemResponseHandler::respondWithFile(HttpRequest const &request, std::string const &filename)
{
  d->respondWithFile(request, filename);
}
