#include "errnoexception.h"

#include <string.h>

using namespace plain;

std::string _errorString(int errnum)
{
  char buf[256] = {}; // initializes to zeros.
  strerror_r(errnum, buf, 255);
  return buf;
}

ErrnoException::ErrnoException(int errnum)
  : std::runtime_error(_errorString(errnum))
{  
}
