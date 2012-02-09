CC=gcc
CFLAGS=-c -Wall -DDEBUG -O3 -fPIC
LDFLAGS=-ldl -Wl,-soname,keylogger.so -shared 
SOURCES=$(wildcard keylogger.c)

OBJECTS=$(SOURCES:.c=.o)
INCLUDE = 
EXECUTABLE=keylogger.so

all: $(SOURCES) $(EXECUTABLE)

compile: $(OBJECTS)

recompile: clean all

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $(INCLUDE) $< -o $@

clean:
	rm $(OBJECTS) $(EXECUTABLE)
