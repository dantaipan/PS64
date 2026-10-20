CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -D_DARWIN_C_SOURCE
SDL_FW  := third_party
LDFLAGS_SDL := -F$(SDL_FW) -framework SDL2 -Wl,-rpath,@executable_path/$(SDL_FW)

all: ps64 fake_sender

# Built-in emulator (VICE): included when tools/get_vice.sh has built the core.
VICE     := third_party/vice
VICE_LIB := $(VICE)/vice_x64sc_libretro_osx.a
LRC      := $(VICE)/libretro-common
# libretro-common helpers the statically linked core expects from its frontend.
LRC_SRC  := $(addprefix $(LRC)/, compat/compat_strl.c compat/compat_strcasestr.c compat/fopen_utf8.c \
              encodings/encoding_utf.c file/file_path.c file/file_path_io.c file/retro_dirent.c \
              streams/file_stream.c streams/file_stream_transforms.c string/stdstring.c \
              time/rtime.c vfs/vfs_implementation.c)
ifneq ($(wildcard $(VICE_LIB)),)
EMU_CFLAGS := -DPS64_EMU -I$(LRC)/include
EMU_SRC    := src/emulator.c src/emugames.c
EMU_LIBS   := lrc.a $(VICE_LIB) -lc++ -lz
EMU_DEPS   := lrc.a $(VICE_LIB)
endif

# The libretro-common files are built with their own flags (they aren't -std=c11 clean).
lrc.a: $(LRC_SRC)
	rm -rf lrc-obj && mkdir -p lrc-obj
	cd lrc-obj && $(CC) -O2 -w -D__LIBRETRO__ -I../$(LRC)/include -c $(addprefix ../,$(LRC_SRC))
	ar rcs $@ lrc-obj/*.o
	rm -rf lrc-obj

CORE_SRC := src/vicstream.c src/audiostream.c src/c64api.c src/c64keys.c src/c64remote.c src/osk.c \
            src/settings.c src/discover.c src/ftp.c src/c64files.c src/ui.c src/c64index.c src/a64.c src/favs.c src/localfs.c \
            src/image.c src/shots.c
UI_SRC   := src/main_sdl.c src/controls.c src/osk_sdl.c src/font_sdl.c src/ui_sdl.c
HDRS     := $(wildcard src/*.h)

ps64: $(UI_SRC) $(CORE_SRC) $(EMU_SRC) $(HDRS) $(EMU_DEPS)
	$(CC) $(CFLAGS) $(EMU_CFLAGS) -F$(SDL_FW) -o $@ $(UI_SRC) $(CORE_SRC) $(EMU_SRC) $(EMU_LIBS) \
	      $(LDFLAGS_SDL) -lpthread

fake_sender: tools/fake_sender.c
	$(CC) $(CFLAGS) -o $@ $<

test_vicstream: tests/test_vicstream.c src/vicstream.c src/audiostream.c $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/test_vicstream.c src/vicstream.c src/audiostream.c

test_input: tests/test_input.c $(CORE_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/test_input.c $(CORE_SRC) -lpthread

# Drives the real emulator core (needs tools/get_vice.sh).
test_emulator: tests/test_emulator.c $(EMU_SRC) $(CORE_SRC) $(HDRS) $(EMU_DEPS)
	$(CC) $(CFLAGS) $(EMU_CFLAGS) -o $@ tests/test_emulator.c $(EMU_SRC) $(CORE_SRC) $(EMU_LIBS) -lpthread

test: test_vicstream test_input $(if $(EMU_SRC),test_emulator)
	./test_vicstream
	./test_input
	$(if $(EMU_SRC),./test_emulator)

clean:
	rm -f ps64 fake_sender test_vicstream test_input test_emulator lrc.a

.PHONY: all test clean
