# Desktop (not ESP) alles Makefile

AMY = ../amy/src
TARGET = alles
LIBS = -lpthread -lsoundio -lm 
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -DDESKTOP_PLATFORM -I$(AMY)

.PHONY: default all clean

default: $(TARGET)
all: default


OBJECTS = $(patsubst %.c, %.o, sync.c multicast_desktop.c alles_desktop.c alles.c sounds.c $(AMY)/algorithms.c \
	$(AMY)/amy.c $(AMY)/envelope.c $(AMY)/filters.c $(AMY)/oscillators.c $(AMY)/pcm.c $(AMY)/partials.c $(AMY)/libsoundio-audio.c)
HEADERS = alles.h $(wildcard amy/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS += -L/opt/homebrew/lib -framework Cocoa -lstdc++ 
	CFLAGS += -I/opt/homebrew/include
endif


%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS)
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f amy/*.o
	-rm -f $(TARGET)
