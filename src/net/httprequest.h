#ifndef __INC_PLAIN_HTTPREQUEST_H__
#define __INC_PLAIN_HTTPREQUEST_H__

#include "http.h"

namespace plain {

  class HttpRequest {

    int d_fd;

    Http::Method d_method;
    char const *d_uri;
    Http::Version d_version;

    char const *d_host;
    Http::Connection d_connection;
    size_t d_contentLength;

  public:

    /**
     *  \return the associated file descriptor.
     */
    int fd() const { return d_fd; }

    void setFd(int fd) { d_fd = fd; }
    
    /**
     *  \return the request method.
     */
    Http::Method method() const { return d_method; }

    /**
     *  Sets the request method.
     */
    void setMethod(Http::Method method) { d_method = method; }

    /**
     *  \return the request uri.
     */
    char const *uri() const { return d_uri; }

    /**
     *  Sets the requesr uri.
     */
    void setUri(char const *uri) { d_uri = uri; }

    /**
     *  \return the http version of the request.
     */
    Http::Version version() const { return d_version; }

    /**
     *  Sets the requets version.
     */
    void setVersion(Http::Version version) { d_version = version; }

    /**
     *  \return the host header field of the request.
     */
    char const *host() const { return d_host; }

    /**
     *  Sets the request host header field value.
     */
    void setHost(char const *host) { d_host = host; }

    /**
     *  \return the connection type (close|keep-alive).
     */
    Http::Connection connection() const { return d_connection; }

    /**
     *  Sets the request connnection header field value.
     */
    void setConnection(Http::Connection connection) { d_connection = connection; }

    /**
     *  \return the content length of the request.
     */
    size_t contentLength() const { return d_contentLength; }

    /**
     *  Sets the request content-length header field value.
     */
    void setContentLength(size_t contentLength) { d_contentLength = contentLength; }

  };

};

#endif // __INC_PLAIN_HTTPREQUEST_H__
