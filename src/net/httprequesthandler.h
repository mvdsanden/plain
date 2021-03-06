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

    void respondWithFile(HttpRequest const &request, std::string const &path)
    {
      if (d_server) {
	d_server->respondWithFile(request, path);
      }
    }

    void drop(HttpRequest const &request)
    {
      if (d_server) {
	d_server->drop(request);
      }
    }

  };

}

#endif // __INC_PLAIN_HTTPREQUESTHANDLER_H__
