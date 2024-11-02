# Desktop (not ESP) alles Makefile

AMY = ../amy/src
TARGET = alles
LIBS = -lpthread -lm 

# on macOS, need to link to AudioUnit, CoreAudio, and CoreFoundation
ifeq ($(shell uname -s), Darwin)
LIBS += -framework AudioUnit -framework CoreAudio -framework CoreFoundation
endif

CC = gcc
CFLAGS = -g -Wall -Wno-strict-aliasing -I$(AMY) -I.

OBJECTS = $(patsubst %.c, %.o,  multicast_desktop.c alles_desktop.c alles.c sounds.c $(AMY)/algorithms.c $(AMY)/delay.c \
	$(AMY)/amy.c $(AMY)/envelope.c $(AMY)/filters.c $(AMY)/oscillators.c $(AMY)/pcm.c $(AMY)/partials.c $(AMY)/libminiaudio-audio.c \
	$(AMY)/log2_exp2.c $(AMY)/custom.c $(AMY)/patches.c $(AMY)/transfer.c)
HEADERS = alles.h $(wildcard amy/*.h)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# Nothing needed 
else ifeq ($(shell uname -m), armv7l)
	# rpi etc 32 bit
	LIBS += -ldl  -latomic
else ifeq ($(shell uname -m), aarch64)
	# rpi etc 64 bit
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
