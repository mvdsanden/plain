#ifndef __INC_PLAIN_HTTPREQUESTHANDLER_H__
#define __INC_PLAIN_HTTPREQUESTHANDLER_H__

namespace plain {

  // Forward declaration.
  class HttpRequest;
  class HttpServer;

  class HttpRequestHandler {

    HttpServer *d_server;

  public:

    HttpRequestHandler()
      : d_server(0) {}

    void setServerInstance(HttpServer *server)
    {
      d_server = server;
    }

    /**
     *  Should handle a HTTP request.
     */
    virtual void request(HttpRequest const &request) = 0;

  protected:

    void respondWithStaticString(HttpRequest const &request, const char *str, size_t length)
    {
      if (d_server) {
	d_server->respondWithStaticString(request, str, length);
      }
    }

  };

}

#endif // __INC_PLAIN_HTTPREQUESTHANDLER_H__
