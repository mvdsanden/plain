/*
 *
 *
 */

#ifndef __INC_PLAIN_MAIN_H__
#define __INC_PLAIN_MAIN_H__

#include <memory>

namespace plain {

  // Forward declaration.
  class Application;
  class Poll;

  /**
   *
   */
  class Main {

    Main(Main const &) = delete;
    Main &operator=(Main const &) = delete;

    Main();

  public:
    
    static Main &instance();

    ~Main();

    /**
     *  This starts the main loop.
     */
    int run(Application &app, int argc, char *argv[]);

    /**
     *  This stops the main loop.
     *
     *  @param code the exit code to use.
     */
    void stop(int code = 0);

    void wakeup();

    Poll &poll();

    // Private data members structure.
    struct Data;
    std::unique_ptr<Data> d;

  };


}


#endif // __INC_PLAIN_MAIN_H__
