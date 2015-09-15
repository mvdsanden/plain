#include "io/poll.h"
#include "exceptions/errnoexception.h"

#include "io/ioscheduler.h"

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

  // Forward declaration.
  struct TableEntry;
    
  // This represents the data associated with a file descriptor in the polling system.
  struct TableEntry : public IoScheduler::Schedulable, public Poll::AsyncResult {

    Internal *internal;
    
    // The current state of the file descriptor.
    std::atomic<int> state;

    // The registered event mask.
    uint32_t eventMask;

    // The current active events.
    uint32_t events;

    // The event callback and user data for the callback.
    EventCallback callback;
    void *data;

    // Schedulable result callback.
    IoScheduler::ResultCallback resultCallback;
    
    // Timeout linked list fields.
    TableEntry *timeoutNext;
    TableEntry *timeoutPrev;

    // The point in time at which this file descriptor should time out.
    std::chrono::steady_clock::time_point timeout;

    // The asynchronous result callback.
    virtual void completed(EventResultMask result);

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

  // The global file descriptor timeout.
  std::chrono::steady_clock::duration d_timeout;

  // The timeout list and its mutex.
  std::recursive_mutex d_timeoutMutex;
  TableEntry *d_timeoutHead;
  TableEntry *d_timeoutTail;

  // The signal mask used for the epoll_waitp call.
  sigset_t d_signalMask;

  IoScheduler d_scheduler;
  
  Internal()
    : d_pollEventsSize(DEFAULT_POLL_EVENTS_SIZE),
      d_pollEvents(new epoll_event [ DEFAULT_POLL_EVENTS_SIZE ]),
      d_tableSize(0), d_table(NULL),
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
      std::cout << "Closing " << d_epoll << ".\n";
      ::close(d_epoll);
    }

    if (d_table != NULL) {
      delete [] d_table;
      d_table = NULL;
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
      entry->timeoutNext = NULL;
      entry->timeoutPrev = NULL;
      entry->schedCallback = _schedulerCallback;
      entry->schedData = this;
      entry->internal = this;
    }
  }

  void add(int fd, uint32_t events, EventCallback callback, void *data)
  {
    std::cout << "add(" << fd << ", " << events << ").\n";
    
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
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;

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
    //    std::cout << "modify(" << fd << ", " << events << ").\n";
    
    TableEntry *entry = d_table + fd;

    // This makes sure no other thread interferce with the structure.
    int state = TABLE_ENTRY_STATE_ACTIVE;
    if (!std::atomic_compare_exchange_strong<int>(&entry->state,
						  &state,
						  TABLE_ENTRY_STATE_MODIFYING)) {
      throw std::runtime_error("file descriptor is not active");
    }					    

    entry->eventMask = events;
    entry->callback = callback?callback:entry->callback;
    entry->data = data?data:entry->data;

    // If necessary add the entry to the timeout list.
    if (events & TIMEOUT) {
      timeoutAdd(d_timeoutHead, d_timeoutTail, entry);
    }

    entry->state = TABLE_ENTRY_STATE_ACTIVE;

    //    std::cout << "- events: " << entry->events << ", mask: " << entry->eventMask << ".\n";
    
    if (entry->events & entry->eventMask != 0) {
      schedule(entry);
      // TODO: wake up thread!!
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
    std::cout << "Closing " << fd << ".\n";
    ::close(fd);
  }

  // Remove and close the file descriptor associated with this entry.
  void close(TableEntry *entry)
  {
    // TODO: optimize, because we don't need the epoll_ctl system call.
    remove(entry);
    int fd = entry - d_table;
    std::cout << "Closing " << fd << ".\n";
    ::close(fd);
  }

  bool update(int timeout)
  {
    //    std::unique_lock<std::recursive_mutex> lk(d_mutex);

    // There are still events to be run.
    if (!d_scheduler.empty()) {
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

    //    std::cout << "epoll_pwait(t=" << timeout << ").\n";
    
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
    runEvents();

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

  void schedule(TableEntry *entry)
  {
    //    std::cout << "- scheduling entry " << (entry - d_table) << " (" << entry->events << " & " << entry->eventMask << " = " << (entry->events & entry->eventMask) << ").\n";
    
    if ((entry->events & entry->eventMask) != 0) {
      // std::cout << "- really scheduling...\n";

      d_scheduler.schedule(entry);
      
      // Remove the file descriptor from the timeout list while it is scheduled.
      timeoutRemove(d_timeoutHead, d_timeoutTail, entry);
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

      //      std::cout << "- adding events " << event->events << " to fd " << (entry - d_table) << ".\n";
      
      // Set the current active events for the file descriptor.
      entry->events |= event->events;
      
      schedule(entry);
	
      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }

  }

  // Schedule a file descriptor for timeout.
  void scheduleTimeout(TableEntry *entry)
  {
    entry->events |= TIMEOUT;
    d_scheduler.schedule(entry);
  }

  // Run all events.
  void runEvents()
  {
    //    std::cout << "runEvents()\n";
    
    // Run a specific number of events before going back to poll for more.
    for (size_t i = 0; i < DEFAULT_EVENT_HANDLE_COUNT && !d_scheduler.empty(); ++i) {

      d_scheduler.runNext();
      
      //      printSchedule(d_defaultPrioHead, d_defaultPrioMid, d_defaultPrioTail);

    }
  }
  
  // The callback called by the scheduler.
  static void _schedulerCallback(IoScheduler::Schedulable *schedulable, void *data, IoScheduler::ResultCallback asyncResult)
  {
    //    std::cout << "_schedulerCallback()\n";
    reinterpret_cast<Internal*>(data)->schedulerCallback(schedulable, asyncResult);
  }

  void schedulerCallback(IoScheduler::Schedulable *schedulable, IoScheduler::ResultCallback asyncResultCallback)
  {
    TableEntry *entry = static_cast<TableEntry*>(schedulable);

    //    std::cout << "schedulerCallback for entry " << (entry - d_table) << " events=" << entry->events << ".\n";
    
    EventCallback callback = entry->callback;
    void *data = entry->data;

    // If the handler was not removed in the mean time, call the callback.
    if ((entry->events & entry->eventMask) != 0 &&
	callback != NULL) {
      entry->resultCallback = asyncResultCallback;
      callback(entry - d_table, entry->events, data, *entry);
    } else {
      entry->completed(REMOVE_DESCRIPTOR);
    } 
  }
  
};

// NOTE: this method could be called from an arbitrary thread.
void Poll::Internal::TableEntry::completed(EventResultMask result)
{
  //  std::cout << "completed(" << result << ").\n";
  //    std::cout << "schedulerCallback result=" << result << ".\n";
    
  // Add back to the timeout list if timeout was set.
  if (eventMask & TIMEOUT) {
    internal->timeoutAdd(internal->d_timeoutHead, internal->d_timeoutTail, this);
  }

  // Check the result of the event handler.
  if (result == CLOSE_DESCRIPTOR) {
    internal->close(this);
  } else if (result == REMOVE_DESCRIPTOR) {
    internal->remove(this);
  } else {

    if (result & READ_COMPLETED) {
      events &= ~EPOLLIN;
    }

    if (result & WRITE_COMPLETED) {
      events &= ~EPOLLOUT;
    }

  }

  //    std::cout << "schedulerCallback events=" << entry->events << ".\n";

  if (events == 0) {
    // This will remove the entry from the schedule, so it is no longer scheduled.
    resultCallback(this, IoScheduler::RESULT_DONE);
  } else {
    // This will automatically re-add the entry to the schedule, so it keeps on being scheduled.
    resultCallback(this, IoScheduler::RESULT_NOT_DONE);
  } 
}

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
