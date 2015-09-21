#ifndef __INC_PLAIN_DOUBLE_LINKED_LIST_H__
#define __INC_PLAIN_DOUBLE_LINKED_LIST_H__

namespace plain {

  class DoubleLinkedList {
  public:

    template <class T>
    struct List {
    };
    
    template <class T>
    struct Handles {
      T *next;
      T *prev;
    };

  };
  
}

#endif // __INC_PLAIN_DOUBLE_LINKED_LIST_H__
