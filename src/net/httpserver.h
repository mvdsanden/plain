#ifndef __INC_PLAIN_HTTPSERVER_H__
#define __INC_PLAIN_HTTPSERVER_H__

#include <memory>

namespace plain {

  class HttpServer {
  public:

    HttpServer(int port);

    ~HttpServer();

  private:

    struct Internal;
    std::unique_ptr<Internal> d;

  };

};

#endif // __INC_PLAIN_HTTPSERVER_H__
