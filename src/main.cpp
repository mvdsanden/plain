#include "core/main.h"
#include "core/application.h"

#include <iostream>
#include <thread>

#include <unistd.h>

class App : public plain::Application {

  std::thread d_thread0;

public:

  virtual void create(int argc, char *argv[])
  {
    std::cout << "-- create --\n";

    // * If you want to stop the application immediatly:
    // plain::Main::instance().stop(1);

    d_thread0 = std::thread([](){
	sleep(10);
	plain::Main::instance().stop(1);
      });
  }
  
  virtual void destroy()
  {
    std::cout << "-- destroy --\n";
  }

  virtual void idle()
  {
    std::cout << "-- idle --\n";
  }

};

int main(int argc, char *argv[])
{
  App app;
  return plain::Main::instance().run(app, argc, argv);
}
