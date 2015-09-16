#ifndef __INC_PLAIN_HTTP_FILESYSTEM_RESPONSE_HANDLER_H__
#define __INC_PLAIN_HTTP_FILESYSTEM_RESPONSE_HANDLER_H__

#include "httpresponsehandler.h"

#include <memory>

namespace plain {

  // Forward declaration.
  class HttpRequest;
  
  class HttpFilesystemResponseHandler : public HttpResponseHandler {

    HttpFilesystemResponseHandler();
    
  public:

    static HttpFilesystemResponseHandler &instance();

    ~HttpFilesystemResponseHandler();
    
    void respondWithFile(HttpRequest const &request, std::string const &filename);

  private:

    struct Internal;
    std::unique_ptr<Internal> d;
    
  };

};

#endif // __INC_PLAIN_HTTP_FILESYSTEM_RESPONSE_HANDLER_H__
