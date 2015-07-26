#ifndef __INC_PLAIN_HTTPSERVER_H__
#define __INC_PLAIN_HTTPSERVER_H__

#include <memory>

namespace plain {

  // Forward declarations.
  class HttpRequest;
  class HttpRequestHandler;

  class HttpServer {
  public:

    HttpServer(int port, std::shared_ptr<HttpRequestHandler> const &requestHandler);

    ~HttpServer();

    /**
     *  Sends a static string as a response to the specified request.
     */
    void respondWithStaticString(HttpRequest const &request, const char *str, size_t length);

  private:

    struct Internal;
    std::unique_ptr<Internal> d;

  };

};

#endif // __INC_PLAIN_HTTPSERVER_H__
