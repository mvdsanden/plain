#include "schedulable.h"
#include "private/schedulable.h"

using namespace plain;

Schedulable::Schedulable()
  : schedulable(new Private)
{
}

Schedulable::~Schedulable()
{
}

void Schedulable::setSchedulableCallback(Callback callback, void *data)
{
  schedulable->setCallback(callback, data);
}
