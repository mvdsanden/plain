#ifndef __INC_PLAIN_ERRNOEXCEPTION_H__
#define __INC_PLAIN_ERRNOEXCEPTION_H__

#include <stdexcept>

namespace plain {

  class ErrnoException : public std::runtime_error {
  public:

    ErrnoException(int errnum);

  };

}

#endif // __INC_PLAIN_ERRNOEXCEPTION_H__
