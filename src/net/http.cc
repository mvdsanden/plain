#include "http.h"
#include "httprequest.h"

#include <iostream>
#include <unordered_map>
#include <cstring>

using namespace plain;

class HttpInternal {

  // Table used for translating header field names to enumeration values.
  std::unordered_map<std::string, Http::HeaderField> d_headerFieldTable;
  
public:

  static HttpInternal &instance();

  HttpInternal()
  {
    initializeHeaderFieldTable();
  }

  Http::HeaderField lookupHeaderField(std::string const &name)
  {
    auto i = d_headerFieldTable.find(name);
    return (i != d_headerFieldTable.end()?i->second:Http::HEADER_FIELD_UNKNOWN);
  }

private:
  
  /*
   *  Initializes the header field table for resolving a header name to an enum key.
   */
  void initializeHeaderFieldTable()
  {
    d_headerFieldTable["host"] = Http::HEADER_FIELD_HOST;
    d_headerFieldTable["connection"] = Http::HEADER_FIELD_CONNECTION;
    d_headerFieldTable["content-length"] = Http::HEADER_FIELD_CONTENT_LENGTH;
  }

  void parseHeaderFieldHost(HttpRequest &request, char const *value)
  {
    request.setHost(value);
  }

  void parseHeaderFieldConnection(HttpRequest &request, char const *value)
  {
    if (std::strcmp(value, "keep-alive") == 0) {
      request.setConnection(Http::CONNECTION_KEEP_ALIVE);
    }
  }

  void parseHeaderFieldContentLength(HttpRequest &request, char const *value)
  {
    request.setContentLength(strtoull(value, NULL, 10));
  }

public:
  
  void parseHttpRequestHeaders(HttpRequest &request, char *buffer, size_t length)
  {
    char *head = buffer;
    char const *end = buffer + length;

    // Parse the HTTP method.
    char *method = head;
    while (head != end && *head != ' ') ++head;
    size_t methodLength = head - method;
    *(head++) = 0;

    if (methodLength > 4) {
      throw std::runtime_error("malformed header");
    }

    // Parse request uri.
    char *uri = head;
    while (head != end && *head != ' ') ++head;
    *(head++) = 0;

    // Sanity check?
    char *http = head;
    while (head != end && *head != '/') ++head;
    *(head++) = 0;    

    if (head - http != 5) {
      throw std::runtime_error("malformed headers");
    }

    // Http version.
    char *version = head;
    while (head != end && *head != '\r') ++head;
    size_t versionLength = head - version;
    *(head++) = 0;

    if (versionLength != 3) {
      throw std::runtime_error("unsupported HTTP version");
    }

    if (*head != '\n') {
      throw std::runtime_error("malformed headers");
    }

    ++head;

    while (head != end) {

      if (*head == '\r') {
	++head;

	if (*head != '\n') {
	  throw std::runtime_error("malformed headers");
	}

	++head;
	break;
      }

      char *key = head;
      while (head != end && *head != ':') *head = tolower(*head), ++head;
      *(head++) = 0;

      while (head != end && *head == ' ') ++head;

      char *value = head;
      while (head != end && *head != '\r') ++head;
      *(head++) = 0;

      if (*head != '\n') {
	throw std::runtime_error("malformed headers");
      }

      std::cout << key << "=" << value << ".\n";

      switch (lookupHeaderField(key)) {
      case Http::HEADER_FIELD_HOST: parseHeaderFieldHost(request, value); break;
      case Http::HEADER_FIELD_CONNECTION: parseHeaderFieldConnection(request, value); break;
      case Http::HEADER_FIELD_CONTENT_LENGTH: parseHeaderFieldContentLength(request, value); break;
      default:
	// Unknown header field.
	break;
      };

      ++head;
    }

    // Check sanity.
    if (*reinterpret_cast<uint32_t const *>(http) != ('H' | 'T' << 8 | 'T' << 16 | 'P' << 24)) {
      throw std::runtime_error("malformed headers");
    }

    request.setVersion(Http::parseVersion(version, versionLength));
    if (request.version() == Http::VERSION_UNKNOWN) {
      throw std::runtime_error("unsupported HTTP version");
    };

    request.setMethod(Http::parseMethod(method, methodLength));
    if (request.method() == Http::METHOD_UNKNOWN) {
      throw std::runtime_error("unsupported request method");
    }

    request.setUri(uri);
    
    //std::cout<< "method=" << method << " (" << request.method() << ").\n";
    //    std::cout << "uri=" << uri << ".\n";
    //    std::cout << "http=" << http << ".\n";
    //    std::cout << "version=" << version << " (" << request.version() << ").\n";
    //    std::cout << "host=" << request.host() << ".\n";
    //    std::cout << "connection=" << request.connection() << ".\n";
    //    std::cout << "contentLength=" << request.contentLength() << ".\n";
  }

  
};

HttpInternal &HttpInternal::instance()
{
  static HttpInternal s_instance;
  return s_instance;
}

void Http::parseHttpRequestHeaders(HttpRequest &request, char *buffer, size_t length)
{
  HttpInternal::instance().parseHttpRequestHeaders(request, buffer, length);
}
