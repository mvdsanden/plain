#ifndef __INC_PLAIN_HTTP_H__
#define __INC_PLAIN_HTTP_H__

namespace plain {

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

  };

}

#endif // __INC_PLAIN_HTTP_H__
