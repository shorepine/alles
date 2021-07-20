# Desktop (not ESP) alles Makefile

TARGET = alles
LIBS = -lpthread -lsoundio -lm
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -DDESKTOP_PLATFORM

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, sync.c multicast_desktop.c alles_desktop.c sounds.c amy/algorithms.c amy/amy.c amy/envelope.c amy/filters.c amy/oscillators.c amy/partials.c)
HEADERS = alles.h $(wildcard amy/*.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
