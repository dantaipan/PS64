# PS5 build of ps64, run inside the ps64-ps5-sdk Docker image (see build.sh).
ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined - run ps5/build.sh)
endif

SDL_ROOT := $(PS5_PAYLOAD_SDK)/target/user/homebrew
SRC      := ../src

# Built-in emulator: the VICE libretro core (fetched by tools/get_vice.sh) as a static library.
VICE     := ../third_party/vice
VICE_LIB := $(VICE)/vice_x64sc_libretro_ps5.a
LRC      := $(VICE)/libretro-common
LRC_SRC  := $(addprefix $(LRC)/, compat/compat_strl.c compat/compat_strcasestr.c compat/fopen_utf8.c \
              encodings/encoding_utf.c file/file_path.c file/file_path_io.c file/retro_dirent.c \
              streams/file_stream.c streams/file_stream_transforms.c string/stdstring.c \
              time/rtime.c vfs/vfs_implementation.c)
LRC_LIB  := out/lrc.a

CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -I$(SDL_ROOT)/include -DPS64_EMU -I$(LRC)/include
LDLIBS  := $(LRC_LIB) $(VICE_LIB) -lc++ -lc++abi -lunwind \
           -L$(SDL_ROOT)/lib -lSDL2main -lSDL2 \
           -lSceSystemService -lSceUserService -lScePad -lSceAudioOut \
           -lSceVideoOut -lSceKeyboard -lSceImeDialog -lSceNotification -lSceNet -lkernel_sys

OUT := out/PS64

all: $(OUT)/eboot.elf $(OUT)/ps64.cfg $(OUT)/sce_sys/icon0.png $(OUT)/INSTALL.txt

SOURCES := $(addprefix $(SRC)/, main_sdl.c controls.c osk_sdl.c font_sdl.c ui_sdl.c \
                                 vicstream.c audiostream.c c64api.c c64keys.c c64remote.c \
                                 osk.c settings.c discover.c ftp.c c64files.c ui.c c64index.c a64.c favs.c \
                                 localfs.c emulator.c emugames.c image.c shots.c)

$(OUT)/eboot.elf: $(SOURCES) $(wildcard $(SRC)/*.h) $(VICE_LIB) $(LRC_LIB)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

# The core takes a few minutes the first time; its objects stay in build-ps5.
$(VICE_LIB):
	$(MAKE) -C $(VICE) -j$$(nproc) platform=ps5 EMUTYPE=x64sc STATIC_LINKING=1 \
	        CC=$(PS5_PAYLOAD_SDK)/bin/prospero-clang CXX=$(PS5_PAYLOAD_SDK)/bin/prospero-clang++ \
	        AR=$(PS5_PAYLOAD_SDK)/bin/prospero-ar OBJDIR=build-ps5 TARGET=vice_x64sc_libretro_ps5.a

$(LRC_LIB): $(LRC_SRC)
	@mkdir -p out/lrc-obj
	cd out/lrc-obj && $(CC) -O2 -w -D__LIBRETRO__ -I../../$(LRC)/include -c $(addprefix ../../,$(LRC_SRC))
	$(PS5_PAYLOAD_SDK)/bin/prospero-ar rcs $@ out/lrc-obj/*.o
	rm -rf out/lrc-obj

$(OUT)/ps64.cfg: ps64.cfg
	@mkdir -p $(@D)
	cp $< $@

$(OUT)/sce_sys/icon0.png: icon0.png
	@mkdir -p $(@D)
	cp $< $@

$(OUT)/INSTALL.txt: INSTALL.txt
	@mkdir -p $(@D)
	cp $< $@

# Zip for sharing: unzip into /data/homebrew/ on the PS5.
dist: all
	cd out && rm -f PS64.zip && python3 -c "import shutil; shutil.make_archive('PS64', 'zip', '.', 'PS64')"

clean:
	rm -rf out

.PHONY: all clean dist
