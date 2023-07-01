# Desktop (not ESP) alles Makefile

AMY = ../amy/src
TARGET = alles
LIBS = -lpthread -lm 
CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -DDESKTOP_PLATFORM -I$(AMY) -I.

OBJECTS = $(patsubst %.c, %.o,  multicast_desktop.c alles_desktop.c alles.c sounds.c $(AMY)/algorithms.c \
	$(AMY)/amy.c $(AMY)/envelope.c $(AMY)/filters.c $(AMY)/oscillators.c $(AMY)/pcm.c $(AMY)/partials.c $(AMY)/libminiaudio-audio.c)
HEADERS = alles.h $(wildcard amy/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS += -L/opt/homebrew/lib -framework Cocoa -lstdc++ 
	CFLAGS += -I/opt/homebrew/include
endif

# on Raspberry Pi, at least under 32-bit mode, libatomic and libdl are needed.
ifeq ($(shell uname -m), armv7l)
	LIBS += -ldl  -latomic
endif

.PHONY: default all clean check-and-reinit-submodules

default: $(TARGET) check-and-reinit-submodules
all: default check-and-reinit-submodules

check-and-reinit-submodules:
	@if git submodule status | egrep -q '^[-]|^[+]' ; then \
		echo "INFO: Need to reinitialize git submodules"; \
		git submodule update --init; \
	fi

%.o: %.c $(HEADERS) check-and-reinit-submodules
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.mm $(HEADERS) check-and-reinit-submodules
	clang $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS) check-and-reinit-submodules
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f amy/*.o
	-rm -f $(TARGET)
