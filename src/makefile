
CC=g++
CXXFLAGS=-std=c++11 -I. -pthread -ggdb
LDFLAGS=-pthread -ggdb

OBJECTS=\
main.o \
core/main.o \
io/socketpair.o \
io/linux/poll.o \
exceptions/errnoexception.o

EXECUTABLE=plain

all: $(EXECUTABLE)

$(EXECUTABLE) : $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

%.o : %.cpp
	$(CC) -c $(CXXFLAGS) $< -o $@

clean :
	rm -f $(OBJECTS)
	rm -f $(EXECUTABLE)
