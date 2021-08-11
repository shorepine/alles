# Desktop (not ESP) alles Makefile


TARGET = alles
LIBS = -lpthread -lsoundio -lm 
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -DDESKTOP_PLATFORM

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, sync.c midi.c multicast_desktop.c alles_desktop.c sounds.c amy/algorithms.c \
	amy/amy.c amy/envelope.c amy/filters.c amy/oscillators.c amy/pcm.c amy/partials.c)
HEADERS = alles.h $(wildcard amy/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS += -L/opt/homebrew/lib -framework CoreMIDI -framework Cocoa -lstdc++ 
	CFLAGS += -I/opt/homebrew/include -DMAC_MIDI
	OBJECTS += amy/virtualmidi.o
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
