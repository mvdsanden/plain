#include "io/poll.h"
#include "exceptions/errnoexception.h"

#include <mutex>
#include <iostream>

#include <string.h>

#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/resource.h>

using namespace plain;

struct Poll::Internal {

  enum {
    DEFAULT_POLL_EVENTS_SIZE = 128,
    DEFAULT_EVENT_HANDLE_COUNT = 256,
  };

  struct TableEntry {

    uint32_t events;

    EventCallback callback;
    void *data;

    // Scheduling linked list fields.
    TableEntry *schedNext;
    TableEntry *schedPrev;

  };

  std::recursive_mutex d_mutex;

  int d_epoll;

  size_t d_pollEventsSize;
  epoll_event *d_pollEvents;

  size_t d_tableSize;
  TableEntry *d_table;

  TableEntry *d_defaultPrioHead;
  TableEntry *d_defaultPrioMid;
  TableEntry *d_defaultPrioTail;

  Internal()
    : d_pollEventsSize(DEFAULT_POLL_EVENTS_SIZE),
      d_pollEvents(new epoll_event [ DEFAULT_POLL_EVENTS_SIZE ]),
      d_tableSize(0), d_table(NULL),
      d_defaultPrioHead(NULL), d_defaultPrioTail(NULL)
  {
    initializeTable();

    d_epoll = epoll_create1(EPOLL_CLOEXEC);
    
    if (d_epoll == -1) {
      throw ErrnoException(errno);
    }
  }

  ~Internal()
  {
    delete [] d_pollEvents;
    d_pollEvents = NULL;
  }

  void initializeTable()
  {
    rlimit l;
    int ret = getrlimit(RLIMIT_NOFILE, &l);
  
    if (ret == -1) {
      throw ErrnoException(errno);
    }

    d_tableSize = l.rlim_max;

    // Allocate a table large enough to hold all file descriptors
    // that can possible be open at one time.
    d_table = new TableEntry [ l.rlim_max ];

    // Initialize the table to zero.
    memset(d_table, 0, l.rlim_max * sizeof(TableEntry));
  }

  void add(int fd, uint32_t events, EventCallback callback, void *data)
  {
    TableEntry *entry = d_table + fd;

    epoll_event event;
    event.data.ptr = entry;
    event.events = events | EPOLLET;

    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_ADD,
			fd,
			&event);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    std::lock_guard<std::recursive_mutex> lk(d_mutex);

    entry->callback = callback;
    entry->data = data;
  }

  void modify(int fd, uint32_t events, EventCallback callback, void *data)
  {
    TableEntry *entry = d_table + fd;

    epoll_event event;
    event.data.ptr = entry;
    event.events = events | EPOLLET;

    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_MOD,
			fd,
			&event);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    std::lock_guard<std::recursive_mutex> lk(d_mutex);

    entry->callback = callback?callback:entry->callback;
    entry->data = data?data:entry->data;
  }

  void remove(int fd)
  {
    TableEntry *entry = d_table + fd;

    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_DEL,
			fd,
			NULL);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    std::lock_guard<std::recursive_mutex> lk(d_mutex);

    entry->callback = NULL;
    entry->data = NULL;
    entry->events = 0;
  }

  bool update(int timeout)
  {
    // There are still events to be run.
    if (d_defaultPrioHead != NULL) {
      timeout = 0;
    }

    //    std::cout << "timeout=" << timeout << ".\n";

    // TODO: run the events more often than epoll_wait() to save on syscalls.
    int ret = epoll_wait(d_epoll,
			 d_pollEvents,
			 d_pollEventsSize,
			 timeout);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

    std::unique_lock<std::recursive_mutex> lk(d_mutex);

    if (ret > 0) {
      schedule(d_pollEvents, ret);
    }

    lk.unlock();

    runEvents(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    return ret == 0;
  }

  void schedulePushBack(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    entry->schedPrev = tail;
    entry->schedNext = NULL;

    if (head == NULL) {
      head = tail = entry;
    } else {
      tail->schedNext = entry;
    }
  }

  void schedulePushFront(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    entry->schedPrev = NULL;
    entry->schedNext = head;

    if (head == NULL) {
      head = tail = entry;
    } else {
      head->schedPrev = entry;
      head = entry;
    }
  }

  void schedulePushMid(TableEntry *&head, TableEntry *&mid, TableEntry *&tail, TableEntry *entry)
  {

    if (head == NULL) {
      mid = head = tail = entry;
      entry->schedPrev = NULL;
      entry->schedNext = NULL;
    } else if (mid == NULL) {
      entry->schedPrev = NULL;
      entry->schedNext = head;
      head->schedPrev = entry;
      mid = head = entry;
    } else {
      entry->schedPrev = mid;
      entry->schedNext = mid->schedNext;

      if (mid->schedNext != NULL) {
	mid->schedNext->schedPrev = entry;
      }

      mid->schedNext = entry;

      if (mid == tail) {
	tail = entry;
      }
      mid = entry;
    }
  }

  void printSchedule(TableEntry const *head, TableEntry const *mid, TableEntry const *tail)
  {
    std::cout << "Schedule:\n";
    TableEntry const *i = head;
    while (i != NULL) {
      if (i == head) std::cout << "H";
      if (i == mid) std::cout << "M";
      if (i == tail) std::cout << "T";
      std::cout << "\t" << (i - d_table) << ".\n";
      i = i->schedNext;
    }
  }

  void schedule(epoll_event *events, size_t count)
  {
    // TODO: should we reverse this loop?
    epoll_event *end = events + count;
    for (epoll_event *event = events; event != end; ++event) {

      TableEntry *entry = reinterpret_cast<TableEntry*>(event->data.ptr);

      //      std::cout << "Schedule: " << (entry - d_table) << ".\n";

      entry->events = event->events;

      if (entry->schedPrev == NULL &&
	  entry->schedNext == NULL &&
	  entry != d_defaultPrioHead) {

	schedulePushMid(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail, entry);
	//schedulePushFront(d_defaultPrioHead, d_defaultPrioTail, entry);

      }

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }



  }

  void runEvents(TableEntry *&head, TableEntry *&mid, TableEntry *&tail)
  {
    TableEntry *first = head;

    //    while (head != NULL) {
    for (size_t i = 0; i < DEFAULT_EVENT_HANDLE_COUNT; ++i) {
      EventResultMask result = runEvent(head);

      if (result & READ_COMPLETED) {
	head->events &= ~EPOLLIN;
      }

      if (result & WRITE_COMPLETED) {
	head->events &= ~EPOLLOUT;
      }

      if (head == mid) {
	mid = NULL;
      }

      if (head->events == 0) {
	TableEntry *next = head->schedNext;
	head->schedPrev = NULL;
	head->schedNext = NULL;
	head = next;
      } else if (head != tail) {
	TableEntry *entry = head;

	head = head->schedNext;
	head->schedPrev = NULL;

	entry->schedPrev = tail;
	tail->schedNext = entry;
	entry->schedNext = NULL;
	tail = entry;
      } else {
	break;
      }

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

      if (head == first) {
	break;
      }
    }
  }

  EventResultMask runEvent(TableEntry *entry)
  {
    //    std::cout << "runEvent: " << (entry - d_table) << ".\n";

    std::lock_guard<std::recursive_mutex> lk(d_mutex);

    if (entry->events != 0 &&
	entry->callback != NULL) {
      return entry->callback(entry - d_table, entry->events, entry->data);
    }

    return NONE_COMPLETED;
  }

};


Poll::Poll()
  : internal(new Internal)
{
}

Poll::~Poll()
{
}

void Poll::add(int fd, uint32_t events, EventCallback callback, void *data)
{
  internal->add(fd, events, callback, data);
}

void Poll::modify(int fd, uint32_t events, EventCallback callback, void *data)
{
  internal->modify(fd, events, callback, data);
}

void Poll::remove(int fd)
{
  internal->remove(fd);
}

bool Poll::update(int timeout)
{
  internal->update(timeout);
}
