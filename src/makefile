
CC=g++
CXXFLAGS=-std=c++11 -I.
LDFLAGS=-lpthread

OBJECTS=\
main.o \
core/main.o \
io/socketpair.o \
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
