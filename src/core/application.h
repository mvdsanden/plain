#ifndef __INC_PLAIN_APPLICATION_H__
#define __INC_PLAIN_APPLICATION_H__

namespace plain {

  /**
   *  The application framework.
   */
  class Application {
  public:

    virtual ~Application() {}
    
    /**
     *  This is called with the application is initialized.
     */
    virtual void create(int argc, char *argv[]) {}

    /**
     *  This is called right before the application ends.
     */
    virtual void destroy() {}

    /**
     *  This is called between event handler updates.
     */
    virtual void idle() {}

  };

}

#endif // __INC_PLAIN_APPLICATION_H__
