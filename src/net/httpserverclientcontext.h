#ifndef __INC_PLAIN_HTTP_SERVER_CLIENT_CONTEXT_H__
#define __INC_PLAIN_HTTP_SERVER_CLIENT_CONTEXT_H__

namespace plain {

  enum {
    // This also signifies the max header length in bytes.
    DEFAULT_BUFFER_SIZE = 1024 * 8,
  };

  // Forward declaration.
  class HttpRequest;
  
  struct HttpServerClientContext {

    // The client connection buffer.
    char buffer[DEFAULT_BUFFER_SIZE + 4] __attribute__((aligned(16)));

    // The current fill of the buffer in bytes.
    size_t bufferFill;

    // Header fields.
    HttpRequest request;

    // Send buffer.
    char const *sendBuffer;
    size_t sendBufferSize;
    size_t sendBufferPosition;

    // The length in bytes of the current content being transfered.
    size_t contentLength;

    // Private data for the response handler.
    void *responsePriv;
    
  };

};

#endif // __INC_PLAIN_HTTP_SERVER_CLIENT_CONTEXT_H__
