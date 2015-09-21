#ifndef __INC_PLAIN_TIMEOUTABLE_H__
#define __INC_PLAIN_TIMEOUTABLE_H__

#include <memory>

namespace plain {

  class Timeoutable {

    friend class TimeoutHandler;
    
    struct Private;
    std::unique_ptr<Private> timeoutable;
    
  public:

    Timeoutable();

    ~Timeoutable();

  };

}

#endif // __INC_PLAIN_TIMEOUTABLE_H__
