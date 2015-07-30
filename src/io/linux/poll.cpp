#include "io/poll.h"
#include "exceptions/errnoexception.h"

#include <mutex>
#include <iostream>
#include <atomic>

#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/resource.h>

using namespace plain;

struct Poll::Internal {

  enum {
    DEFAULT_POLL_EVENTS_SIZE = 128,
    DEFAULT_EVENT_HANDLE_COUNT = 16,
  };

  enum TableEntryState {
    TABLE_ENTRY_STATE_EMPTY,
    TABLE_ENTRY_STATE_ADDING,
    TABLE_ENTRY_STATE_ACTIVE,
    TABLE_ENTRY_STATE_MODIFYING
  };

  struct TableEntry {

    std::atomic<int> state;

    std::mutex mutex;

    uint32_t eventMask;

    uint32_t events;

    EventCallback callback;
    void *data;

    // Scheduling linked list fields.
    TableEntry *schedNext;
    TableEntry *schedPrev;

    // Timeout linked list fields.
    TableEntry *timeoutNext;
    TableEntry *timeoutPrev;

    std::chrono::steady_clock::time_point timeout;

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

  std::chrono::steady_clock::duration d_timeout;

  std::recursive_mutex d_timeoutMutex;
  TableEntry *d_timeoutHead;
  TableEntry *d_timeoutTail;

  Internal()
    : d_pollEventsSize(DEFAULT_POLL_EVENTS_SIZE),
      d_pollEvents(new epoll_event [ DEFAULT_POLL_EVENTS_SIZE ]),
      d_tableSize(0), d_table(NULL),
      d_defaultPrioHead(NULL), d_defaultPrioTail(NULL),
      d_timeout(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(30))),
      d_timeoutHead(NULL), d_timeoutTail(NULL)
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

  void resetTableEntry(TableEntry *entry)
  {
    entry->eventMask = 0;
    entry->events = 0;
    entry->callback = NULL;
    entry->data = NULL;
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
    for (size_t i = 0;i < l.rlim_max; ++i) {
      TableEntry *entry = d_table + i;

      resetTableEntry(entry);

      // Special fields that should only be set to NULL once outside
      // of the scheduling thread.
      entry->state = TABLE_ENTRY_STATE_EMPTY;
      entry->schedNext = NULL;
      entry->schedPrev = NULL;
      entry->timeoutNext = NULL;
      entry->timeoutPrev = NULL;
    }
  }

  void add(int fd, uint32_t events, EventCallback callback, void *data)
  {
    // Get the table entry associated with the file descriptor.
    TableEntry *entry = d_table + fd;

    // This makes sure no other thread interfers with the structure.
    int state = TABLE_ENTRY_STATE_EMPTY;
    if (!std::atomic_compare_exchange_strong<int>(&entry->state,
						  &state,
						  TABLE_ENTRY_STATE_ADDING)) {
      throw std::runtime_error("file descriptor is already registered");
    }

    // Create the epoll event structure to point to the table entry and
    // setup the right events.
    epoll_event event;
    event.data.ptr = entry;
    event.events = events | EPOLLET;

    resetTableEntry(entry);

    entry->eventMask = events;
    entry->callback = callback;
    entry->data = data;

    if (events & TIMEOUT) {
      timeoutAdd(d_timeoutHead, d_timeoutTail, entry);
    }

    entry->state = TABLE_ENTRY_STATE_ACTIVE;

    // Add the fd to the polling queue.
    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_ADD,
			fd,
			&event);

    if (ret == -1) {
      throw ErrnoException(errno);
    }

  }

  void modify(int fd, uint32_t events, EventCallback callback, void *data)
  {
    TableEntry *entry = d_table + fd;

    // This makes sure no other thread interferce with the structure.
    int state = TABLE_ENTRY_STATE_ACTIVE;
    if (!std::atomic_compare_exchange_strong<int>(&entry->state,
						  &state,
						  TABLE_ENTRY_STATE_MODIFYING)) {
      throw std::runtime_error("file descriptor is not active");
    }					    

    epoll_event event;
    event.data.ptr = entry;
    event.events = events | EPOLLET;

    entry->eventMask = events;
    entry->callback = callback?callback:entry->callback;
    entry->data = data?data:entry->data;

    if (events & TIMEOUT) {
      timeoutAdd(d_timeoutHead, d_timeoutTail, entry);
    }

    entry->state = TABLE_ENTRY_STATE_ACTIVE;

    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_MOD,
			fd,
			&event);

    if (ret == -1) {
      throw ErrnoException(errno);
    }
  }

  void remove(int fd)
  {
    TableEntry *entry = d_table + fd;
    remove(entry);
  }

  void remove(TableEntry *entry)
  {  
    // This makes sure no other thread interferce with the structure.
    int state = TABLE_ENTRY_STATE_ACTIVE;
    if (!std::atomic_compare_exchange_strong<int>(&entry->state,
						  &state,
						  TABLE_ENTRY_STATE_MODIFYING)) {
      throw std::runtime_error("file descriptor is not active");
    }					    

    entry->eventMask = 0;
    entry->callback = NULL;
    entry->data = NULL;
    entry->events = 0;

    timeoutRemove(d_timeoutHead, d_timeoutTail, entry);

    entry->state = TABLE_ENTRY_STATE_EMPTY;

    int ret = epoll_ctl(d_epoll,
			EPOLL_CTL_DEL,
			entry - d_table,
			NULL);

    if (ret == -1) {
      throw ErrnoException(errno);
    }
  }

  void close(int fd)
  {
    remove(fd);
    ::close(fd);
  }

  void close(TableEntry *entry)
  {
    // TODO: optimize, because we don't need the epoll_ctl system call.
    remove(entry);
    ::close(entry - d_table);
  }

  bool update(int timeout)
  {
    //    std::unique_lock<std::recursive_mutex> lk(d_mutex);

    // There are still events to be run.
    if (d_defaultPrioHead != NULL) {
      timeout = 0;
    }


    /* else if (d_timeoutHead != NULL) {
      std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();

      if (d_timeoutHead->timeout > t) {
	timeout = std::min<int>(timeout, std::chrono::duration_cast<std::chrono::milliseconds>(d_timeoutHead->timeout - t).count());
      } else {
	timeout = 0;
      }
    }
    */

    //lk.unlock();

    int ret = epoll_wait(d_epoll,
			 d_pollEvents,
			 d_pollEventsSize,
			 timeout);

    if (ret == -1) {
      if (errno != EINTR) {
	throw ErrnoException(errno);
      } else {
	ret = 0;
      }
    }

    if (ret > 0) {
      schedule(d_pollEvents, ret);
    }

    // Schedule timeouts.
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    for (TableEntry *i = timeoutPop(d_timeoutHead, d_timeoutTail, t);
	 i != NULL;
	 i = timeoutPop(d_timeoutHead, d_timeoutTail, t)) {
      scheduleTimeout(i);
    }

    runEvents(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    return ret == 0;
  }

  void timeoutPushBack(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    std::lock_guard<std::recursive_mutex> lk(d_timeoutMutex);

    entry->timeoutPrev = tail;
    entry->timeoutNext = NULL;

    if (head == NULL) {
      head = tail = entry;
    } else {
      tail->timeoutNext = entry;
      tail = entry;
    }
  }

  void timeoutRemove(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    std::lock_guard<std::recursive_mutex> lk(d_timeoutMutex);

    if (head == entry) {
      head = entry->timeoutNext;

      if (head != NULL) {
	head->timeoutPrev = NULL;
      } else {
	tail = NULL;
      }
    } else if (entry->timeoutPrev != NULL) {
      entry->timeoutPrev->timeoutNext = entry->timeoutNext;
      if (entry->timeoutNext != NULL) {
	entry->timeoutNext->timeoutPrev = entry->timeoutPrev;
      } else {
	tail = entry->timeoutPrev;
      }
    }

    entry->timeoutNext = NULL;
    entry->timeoutPrev = NULL;
  }

  void timeoutAdd(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    std::lock_guard<std::recursive_mutex> lk(d_timeoutMutex);

    if (head != entry && entry->timeoutPrev == NULL) {
      entry->timeout = std::chrono::steady_clock::now() + d_timeout;
      timeoutPushBack(head, tail, entry);
    }
  }

  TableEntry *timeoutPop(TableEntry *&head, TableEntry *&tail, std::chrono::steady_clock::time_point const &t)
  {
    std::lock_guard<std::recursive_mutex> lk(d_timeoutMutex);

    TableEntry *front = NULL;

    if (head != NULL && head->timeout < t) {
      front = head;

      head = head->timeoutNext;

      if (head != NULL) {
	head->timeoutPrev = NULL;
      } else {
	tail = NULL;
      }
    }

    return front;
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

      timeoutRemove(d_timeoutHead, d_timeoutTail, entry);

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }

  }

  void scheduleTimeout(TableEntry *entry)
  {
    if (entry->schedPrev == NULL &&
	entry->schedNext == NULL &&
	entry != d_defaultPrioHead) {

      entry->events |= TIMEOUT;
      schedulePushMid(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail, entry);

    }
  }

  void runEvents(TableEntry *&head, TableEntry *&mid, TableEntry *&tail)
  {
    //  std::cout << "-- RUN EVENTS --\n";

    if (head == NULL) {
      return;
    }

    //    while (head != NULL) {
    for (size_t i = 0; i < DEFAULT_EVENT_HANDLE_COUNT && head != NULL; ++i) {

      //      std::cout << "-- running events for fd " << (head - d_table) << ".\n";

      EventResultMask result = runEvent(head);

      //      std::cout << "-- result=" << result << ".\n";

      if (result == CLOSE_DESCRIPTOR) {
	//	std::cout << "-- closing descriptor.\n";
	close(head);
	//	head->callback = NULL;
	//	head->data = NULL;
	//	head->events = 0;
      } else if (result == REMOVE_DESCRIPTOR) {
	remove(head);
      } else {

	if (result & READ_COMPLETED) {
	  //	  std::cout << "-- read completed.\n";
	  head->events &= ~EPOLLIN;
	}

	if (result & WRITE_COMPLETED) {
	  //	  std::cout << "-- write completed.\n";
	  head->events &= ~EPOLLOUT;
	}

      }

      if (head == mid) {
	mid = NULL;
      }

      //      std::cout << "- events left: " << head->events << ".\n";

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
      }

      //      std::cout << "- run completed.\n";

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }

    //      std::cout << "- all runs completed.\n";
  }

  EventResultMask runEvent(TableEntry *entry)
  {
    //    std::cout << "runEvent: " << (entry - d_table) << ".\n";

    EventResultMask result = NONE_COMPLETED;

    EventCallback callback = entry->callback;

    if (entry->events != 0 &&
	callback != NULL) {
      result = callback(entry - d_table, entry->events, entry->data);
    }

    // Add back to the timeout list if timeout was set.
    if (entry->eventMask & TIMEOUT) {
      timeoutAdd(d_timeoutHead, d_timeoutTail, entry);
    }

    return result;
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

void Poll::close(int fd)
{
  internal->close(fd);
}

bool Poll::update(int timeout)
{
  internal->update(timeout);
}
