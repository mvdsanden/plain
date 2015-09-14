#ifndef __INC_PLAIN_HTTPSERVER_H__
#define __INC_PLAIN_HTTPSERVER_H__

#include <memory>

namespace plain {

  // Forward declarations.
  class HttpRequest;
  class HttpRequestHandler;

  /**
   *  Http server
   *
   *
   */
  class HttpServer {
  public:

    /**
     *  Creates a new Http server,
     *
     *  @param port the port number to run the server on.
     *  @param requestHandler the request handler the is responsible for mapping requests to responses.
     *
     *  @throw ErrnoException when the server fails to initialize.
     */
    HttpServer(int port, std::shared_ptr<HttpRequestHandler> const &requestHandler);

    ~HttpServer();

    /**
     *  Sends a static string as a response to the specified request.
     */
    void respondWithStaticString(HttpRequest const &request, const char *str, size_t length);

    /**
     *  Sends the content of a file as a response to the specified request.
     */
    void respondWithFile(HttpRequest const &request, std::string const &path);

    /**
     *  Drops the request.
     */
    void drop(HttpRequest const &request);
    
  private:

    struct Internal;
    std::unique_ptr<Internal> d;

  };

};

#endif // __INC_PLAIN_HTTPSERVER_H__
