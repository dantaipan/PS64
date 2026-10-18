CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_DARWIN_C_SOURCE
SDL_FW  := third_party
LDFLAGS_SDL := -F$(SDL_FW) -framework SDL2 -Wl,-rpath,@executable_path/$(SDL_FW)

all: ps64 fake_sender

CORE_SRC := src/vicstream.c src/audiostream.c src/c64api.c src/c64keys.c src/c64remote.c src/osk.c \
            src/settings.c src/discover.c src/ftp.c src/c64files.c src/ui.c src/c64index.c src/a64.c src/favs.c
UI_SRC   := src/main_sdl.c src/controls.c src/osk_sdl.c src/font_sdl.c src/ui_sdl.c
HDRS     := $(wildcard src/*.h)

ps64: $(UI_SRC) $(CORE_SRC) $(HDRS)
	$(CC) $(CFLAGS) -F$(SDL_FW) -o $@ $(UI_SRC) $(CORE_SRC) $(LDFLAGS_SDL) -lpthread

fake_sender: tools/fake_sender.c
	$(CC) $(CFLAGS) -o $@ $<

test_vicstream: tests/test_vicstream.c src/vicstream.c src/audiostream.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/test_vicstream.c src/vicstream.c src/audiostream.c

test_input: tests/test_input.c $(CORE_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/test_input.c $(CORE_SRC) -lpthread

test: test_vicstream test_input
	./test_vicstream
	./test_input

clean:
	rm -f ps64 fake_sender test_vicstream test_input

.PHONY: all test clean
