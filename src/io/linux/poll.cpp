#include "io/poll.h"
#include "exceptions/errnoexception.h"

#include <mutex>
#include <iostream>
#include <atomic>

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/resource.h>

using namespace plain;

struct Poll::Internal {

  enum {
    // The maximum size of the event buffer that is used to poll for events.
    DEFAULT_POLL_EVENTS_SIZE = 128,

    // The number of events that are handled between epoll_wait calls. A higher
    // number means a lower number of system calls, but a higher potential latency.
    DEFAULT_EVENT_HANDLE_COUNT = 16,
  };

  // The state of a file descriptor.
  enum TableEntryState {
    // Un used.
    TABLE_ENTRY_STATE_EMPTY,

    // In the process of adding it to the event poll list.
    TABLE_ENTRY_STATE_ADDING,

    // Active inside the event poll list.
    TABLE_ENTRY_STATE_ACTIVE,

    // Currently modifying the event registration.
    TABLE_ENTRY_STATE_MODIFYING
  };

  // This represents the data associated with a file descriptor in the polling system.
  struct TableEntry {

    // The current state of the file descriptor.
    std::atomic<int> state;

    // The registered event mask.
    uint32_t eventMask;

    // The current active events.
    uint32_t events;

    // The event callback and user data for the callback.
    EventCallback callback;
    void *data;

    // Scheduling linked list fields.
    TableEntry *schedNext;
    TableEntry *schedPrev;

    // Timeout linked list fields.
    TableEntry *timeoutNext;
    TableEntry *timeoutPrev;

    // The point in time at which this file descriptor should time out.
    std::chrono::steady_clock::time_point timeout;
  };

  std::recursive_mutex d_mutex;

  // The epoll handle.
  int d_epoll;

  // The size of the epoll event buffer, used for querying events.
  size_t d_pollEventsSize;
  epoll_event *d_pollEvents;

  // The file descriptor table size.
  size_t d_tableSize;
  TableEntry *d_table;

  // The currently scheduled events list.
  // - Events are processed from head to tail.
  // - New events are added in the middle.
  // - When an event is processed it is moved to the tail of the list.
  TableEntry *d_defaultPrioHead;
  TableEntry *d_defaultPrioMid;
  TableEntry *d_defaultPrioTail;

  // The global file descriptor timeout.
  std::chrono::steady_clock::duration d_timeout;

  // The timeout list and its mutex.
  std::recursive_mutex d_timeoutMutex;
  TableEntry *d_timeoutHead;
  TableEntry *d_timeoutTail;

  // The signal mask used for the epoll_waitp call.
  sigset_t d_signalMask;
  
  Internal()
    : d_pollEventsSize(DEFAULT_POLL_EVENTS_SIZE),
      d_pollEvents(new epoll_event [ DEFAULT_POLL_EVENTS_SIZE ]),
      d_tableSize(0), d_table(NULL),
      d_defaultPrioHead(NULL), d_defaultPrioTail(NULL),
      d_timeout(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(30))),
      d_timeoutHead(NULL), d_timeoutTail(NULL)
  {
    // Initialize the file descriptor table.
    initializeTable();

    // Create the epoll handle.
    d_epoll = epoll_create1(EPOLL_CLOEXEC);
    
    if (d_epoll == -1) {
      throw ErrnoException(errno);
    }

    // Setup the polling signal mask.
    sigemptyset(&d_signalMask);
    sigaddset(&d_signalMask, SIGPIPE);
  }

  ~Internal()
  {
    // Free the event buffers.
    delete [] d_pollEvents;
    d_pollEvents = NULL;

    // Close the epoll handle.
    if (d_epoll != -1) {
      close(d_epoll);
    }
  }

  // Resets the state of a file descriptor table entry.
  void resetTableEntry(TableEntry *entry)
  {
    entry->eventMask = 0;
    entry->events = 0;
    entry->callback = NULL;
    entry->data = NULL;
  }

  // Initializes the file descriptor table.
  void initializeTable()
  {
    // Get file descriptor limits.
    rlimit l;
    int ret = getrlimit(RLIMIT_NOFILE, &l);
  
    if (ret == -1) {
      throw ErrnoException(errno);
    }

    d_tableSize = l.rlim_cur;

    // Allocate a table large enough to hold all file descriptors
    // that can possible be open at one time.
    d_table = new TableEntry [ l.rlim_cur ];

    // Initialize the table to zero.
    for (size_t i = 0;i < l.rlim_cur; ++i) {
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

    // Reset the structure.
    resetTableEntry(entry);

    entry->eventMask = events;
    entry->callback = callback;
    entry->data = data;

    // Check if we need to add it to the timeout list.
    if (events & TIMEOUT) {
      timeoutAdd(d_timeoutHead, d_timeoutTail, entry);
    }

    // Update the state to active.
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

    // If necessary add the entry to the timeout list.
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

  // Remove the file descriptor from the polling system.
  void remove(int fd)
  {
    TableEntry *entry = d_table + fd;
    remove(entry);
  }

  // Remove the file descriptor associated with this entry from the polling system.
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

    // Remove from the timeout list if it is in it.
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

  // Remove and close the file descriptor.
  void close(int fd)
  {
    remove(fd);
    ::close(fd);
  }

  // Remove and close the file descriptor associated with this entry.
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

    
    // Poll for events.
    int ret = epoll_pwait(d_epoll,
			  d_pollEvents,
			  d_pollEventsSize,
			  timeout,
			  &d_signalMask);

    if (ret == -1) {
      if (errno != EINTR) {
	throw ErrnoException(errno);
      } else {
	ret = 0;
      }
    }

    // If we have new events add them to the scheduler list.
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

    // Run scheduled events.
    runEvents(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    return ret == 0;
  }

  // Add entry to the back of the timeout list.
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

  // Remove entry from the timeout list.
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

  // Add a timeout for the entry.
  void timeoutAdd(TableEntry *&head, TableEntry *&tail, TableEntry *entry)
  {
    std::lock_guard<std::recursive_mutex> lk(d_timeoutMutex);

    if (head != entry && entry->timeoutPrev == NULL) {
      entry->timeout = std::chrono::steady_clock::now() + d_timeout;
      timeoutPushBack(head, tail, entry);
    }
  }

  // Pop the first expired timeout from the list.
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

  // Add the entry to the back of the scheduler list.
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

  // Add the entry to the front of the scheduler list.
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

  // Add the entry to the middle of the scheduler list.
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

  // Print the scheduler list to cout.
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

  // Schedule the events.
  void schedule(epoll_event *events, size_t count)
  {
    // TODO: should we reverse this loop?
    epoll_event *end = events + count;
    for (epoll_event *event = events; event != end; ++event) {

      // Get the file descriptor table entry associated with the event.
      TableEntry *entry = reinterpret_cast<TableEntry*>(event->data.ptr);

      // Set the current active events for the file descriptor.
      entry->events = event->events;

      // If the file descriptor is not already scheduled, add it to the middle of
      // the scheduler list. Just after all unprocessed events and before all recently
      // processed events.
      if (entry->schedPrev == NULL &&
	  entry->schedNext == NULL &&
	  entry != d_defaultPrioHead) {

	schedulePushMid(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail, entry);
	//schedulePushFront(d_defaultPrioHead, d_defaultPrioTail, entry);

      }

      // Remove the file descriptor from the timeout list while it is scheduled.
      timeoutRemove(d_timeoutHead, d_timeoutTail, entry);

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }

  }

  // Schedule a file descriptor for timeout.
  void scheduleTimeout(TableEntry *entry)
  {
    // If it is not already in the scheduler list push it to the middle of the scheduler list.
    if (entry->schedPrev == NULL &&
	entry->schedNext == NULL &&
	entry != d_defaultPrioHead) {

      entry->events |= TIMEOUT;
      schedulePushMid(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail, entry);

    }
  }

  // Run all events.
  void runEvents(TableEntry *&head, TableEntry *&mid, TableEntry *&tail)
  {

    if (head == NULL) {
      return;
    }

    // Run a specific number of events before going back to poll for more.
    for (size_t i = 0; i < DEFAULT_EVENT_HANDLE_COUNT && head != NULL; ++i) {

      // Run the event handler for this entry.
      EventResultMask result = runEvent(head);

      // Check the result of the event handler.
      if (result == CLOSE_DESCRIPTOR) {
	close(head);
      } else if (result == REMOVE_DESCRIPTOR) {
	remove(head);
      } else {

	if (result & READ_COMPLETED) {
	  head->events &= ~EPOLLIN;
	}

	if (result & WRITE_COMPLETED) {
	  head->events &= ~EPOLLOUT;
	}

      }

      // If we just passed the mid point of the scheduler list reset it to null.
      if (head == mid) {
	mid = NULL;
      }

      if (head->events == 0) {
	// If all events are processed, remove the entry from the scheduler table.
	TableEntry *next = head->schedNext;
	head->schedPrev = NULL;
	head->schedNext = NULL;
	head = next;
      } else if (head != tail) {
	// If the event is not already at the back of the scheduler list, move it to the back.
	TableEntry *entry = head;

	head = head->schedNext;
	head->schedPrev = NULL;

	entry->schedPrev = tail;
	tail->schedNext = entry;
	entry->schedNext = NULL;
	tail = entry;
      }

      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }
  }

  // Run the event handler for the entry.
  EventResultMask runEvent(TableEntry *entry)
  {
    EventResultMask result = NONE_COMPLETED;
    EventCallback callback = entry->callback;
    void *data = entry->data;

    // If the handler was not removed in the mean time, call the callback.
    if (entry->events != 0 &&
	callback != NULL) {
      result = callback(entry - d_table, entry->events, data);
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
