#ifndef __INC_PLAIN_APPLICATION_H__
#define __INC_PLAIN_APPLICATION_H__

namespace plain {

  class Application {
  public:

    virtual void create(int argc, char *argv[]) {}

    virtual void destroy() {}

    virtual void idle() {}

  };

}

#endif // __INC_PLAIN_APPLICATION_H__
