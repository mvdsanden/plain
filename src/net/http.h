#ifndef __INC_PLAIN_HTTP_H__
#define __INC_PLAIN_HTTP_H__

#include "exceptions/errnoexception.h"

namespace plain {

  // Forward declaration.
  class HttpRequest;
  
  class Http {
  public:

    enum Method {
      METHOD_UNKNOWN = 0,
      METHOD_GET = 1,
      METHOD_PUT = 2,
      METHOD_POST = 3,
    };

    enum Version {
      VERSION_UNKNOWN = 0x0000,
      VERSION_10 = 0x0100,
      VERSION_11 = 0x0101,
    };

    // Supported request header fields.
    enum HeaderField {
      HEADER_FIELD_UNKNOWN = -1,
      HEADER_FIELD_HOST = 0,
      HEADER_FIELD_CONNECTION = 1,
      HEADER_FIELD_CONTENT_LENGTH = 2,  
      HEADER_FIELD_COUNT,
    };
    
    enum Connection {
      CONNECTION_CLOSE = 0,
      CONNECTION_KEEP_ALIVE = 1,
    };

    /**
     *  Parses a HTTP request method.
     */
    static Method parseMethod(char const *str, size_t len)
    {
      switch (*reinterpret_cast<uint32_t const*>(str)) {
      case ('G' | 'E' << 8 | 'T' << 16 | '\0' << 24):
	return Http::METHOD_GET;

      case ('P' | 'U' << 8 | 'T' << 16 | '\0' << 24):
	return Http::METHOD_PUT;

      case ('P' | 'O' << 8 | 'S' << 16 | 'T' << 24):
	return Http::METHOD_POST;

      default:
	return Http::METHOD_UNKNOWN;
      };
    }

    /**
     *  Parses a HTTP request version.
     */
    static Version parseVersion(char const *str, size_t len)
    {
      // Parse version.
      switch (*reinterpret_cast<uint32_t const*>(str)) {
      case ('1' | '.' << 8 | '0' << 16 | '\0' << 24):
	return Http::VERSION_10;

      case ('1' | '.' << 8 | '1' << 16 | '\0' << 24):
	return Http::VERSION_11;

      default:
	return Http::VERSION_UNKNOWN;
      };
    }

    static void parseHttpRequestHeaders(HttpRequest &req, char *buffer, size_t length);
    
    /**
     *  Conveniance class used to fill a buffer with HTTP response headers.
     */
    class Response {
      char *d_buffer;
      size_t d_capacity;
      size_t d_size;

      template <class... ARGV>
      void print(char const *str, ARGV... argv) {
	int ret = snprintf(d_buffer + d_size, d_capacity - d_size, str, argv...);

	if (ret < 0) {
	  throw ErrnoException(errno);
	}

	if (ret > d_capacity - d_size) {
	  throw std::runtime_error("buffer overflow");
	}

	d_size += ret;
      }
      
    public:

      /**
       *  Create a new response in the specified buffer with the specified status code and line.
       *
       *  @param buffer the buffer to write the response headers to.
       *  @param size the size of the buffer.
       *  @param statusCode the HTTP status code for the response.
       *  @param statusLine the HTTP status line for the response.
       */
      Response(char *buffer, size_t size, size_t statusCode, std::string const &statusLine)
	: d_buffer(buffer), d_capacity(size), d_size(0)
      {
	print("HTTP/1.1 %d %s\r\n\r\n", statusCode, statusLine.c_str());
	d_size -= 2;
      }

      /**
       *  @return the size of the headers in bytes.
       */
      size_t size() const { return d_size + 2; }

      /**
       *  Adds a string typed header field to the headers.
       *
       *  @param key the header field name.
       *  @param value the header field value.
       */
      void addHeaderField(std::string const &key, std::string const &value)
      {
	print("%s: %s\r\n\r\n", key.c_str(), value.c_str());
	d_size -= 2;
      }

      /**
       *  Adds un unsigned integer typed header field to the headers.
       *
       *  @param key the header field name.
       *  @param value the header field value.
       */
      void addHeaderField(std::string const &key, size_t value)
      {
	print("%s: %llu\r\n\r\n", key.c_str(), value);
	d_size -= 2;	
      }
      
    };

  };

}

#endif // __INC_PLAIN_HTTP_H__
