# PS5 build of ps64, run inside the ps64-ps5-sdk Docker image (see build.sh).
ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined - run ps5/build.sh)
endif

SDL_ROOT := $(PS5_PAYLOAD_SDK)/target/user/homebrew
SRC      := ../src

CFLAGS  := -O2 -Wall -Wextra -std=gnu11 -I$(SDL_ROOT)/include
LDLIBS  := -L$(SDL_ROOT)/lib -lSDL2main -lSDL2 \
           -lSceSystemService -lSceUserService -lScePad -lSceAudioOut \
           -lSceVideoOut -lSceKeyboard -lSceImeDialog -lSceNotification -lSceNet -lkernel_sys

OUT := out/PS64

all: $(OUT)/eboot.elf $(OUT)/ps64.cfg $(OUT)/sce_sys/icon0.png $(OUT)/INSTALL.txt

SOURCES := $(addprefix $(SRC)/, main_sdl.c controls.c osk_sdl.c font_sdl.c ui_sdl.c \
                                 vicstream.c audiostream.c c64api.c c64keys.c c64remote.c \
                                 osk.c settings.c discover.c ftp.c c64files.c ui.c c64index.c a64.c favs.c)

$(OUT)/eboot.elf: $(SOURCES) $(wildcard $(SRC)/*.h)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDLIBS)

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
