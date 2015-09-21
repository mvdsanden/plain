#include "iodescriptor.h"
#include "private/iodescriptor.h"

using namespace plain;

IoDescriptor::IoDescriptor()
  : ioDescriptor(new Private)
{
}

IoDescriptor::~IoDescriptor()
{
}

void IoDescriptor::setFileDescriptor(int fd)
{
  ioDescriptor->setFileDescriptor(fd);
}

int IoDescriptor::fileDescriptor() const
{
  return ioDescriptor->fileDescriptor();
}

void IoDescriptor::setEventHandler(uint32_t eventMask, EventCallback callback, void *data)
{
  ioDescriptor->setEventHandler(eventMask, callback, data);

  // If one of the events in the event mask is active, schedule the descriptor for
  // event handeling.
  if ((ioDescriptor->eventHandler.state & ioDescriptor->eventHandler.mask) != 0) {
    Main::instance().scheduler.schedule(this);
  }
}
