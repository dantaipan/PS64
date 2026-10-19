#!/bin/bash
# Runs inside the ps64-native container (see build.sh): builds the libraries
# PS64 links (each once), stages PS64 into the boilerplate and builds the app.
set -e
cd /src
JOBS=$(nproc)
SDK=$PS5_PAYLOAD_SDK
B=third_party/ps5-native-app-boilerplate
STAGE=$B/ps64                      # the boilerplate only builds from inside its own tree

# 1. VICE and the libretro-common helpers (the same libraries as the payload build).
make -C ps5 -s ../third_party/vice/vice_x64sc_libretro_ps5.a out/lrc.a

# 2. PS64's SDL2 with the native-app patches.
if [ ! -f third_party/sdl-native-build/libSDL2.a ]; then
    (source "$SDK/toolchain/prospero.sh" &&
     $CMAKE -S third_party/sdl-native -B third_party/sdl-native-build -DCMAKE_BUILD_TYPE=Release \
            -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF >/dev/null &&
     make -C third_party/sdl-native-build -j"$JOBS" -s)
fi

# 3. Stage sources, headers and libraries.
rm -rf "$STAGE"
mkdir -p "$STAGE/src" "$STAGE/include/SDL2" "$STAGE/include-lrc" "$STAGE/lib" "$STAGE/sce_sys"
cp src/*.c src/*.h "$STAGE/src/"
cp ps5/native/src/*.c "$STAGE/src/"
cp ps5/native/sce_sys/* "$STAGE/sce_sys/"
cp third_party/sdl-native-build/include/SDL2/* third_party/sdl-native-build/include-config-release/SDL2/* \
   "$STAGE/include/SDL2/"
cp third_party/dlmalloc/dlmalloc.c "$STAGE/include/"
cp -r third_party/vice/libretro-common/include/* "$STAGE/include-lrc/"
cp third_party/sdl-native-build/libSDL2.a "$STAGE/lib/"
cp third_party/vice/vice_x64sc_libretro_ps5.a "$STAGE/lib/vice.a"
cp ps5/out/lrc.a "$STAGE/lib/lrc.a"
# (renamed: the boilerplate accepts only [A-Za-z0-9_.-] in archive names)
cp "$SDK/target/lib/libc++.a" "$STAGE/lib/libcxx.a"
cp "$SDK/target/lib/libc++abi.a" "$STAGE/lib/libcxxabi.a"
cp "$SDK/target/lib/libunwind.a" "$STAGE/lib/libunwind.a"

# 4. Build. Wrapped functions (--wrap): see ps5/native/src.
WRAP="malloc free calloc realloc posix_memalign memalign aligned_alloc fcntl getcwd
      sceKeyboardInit sceKeyboardOpen sceKeyboardClose sceKeyboardReadState
      sceImeDialogInit sceImeDialogGetStatus sceImeDialogGetResult sceImeDialogTerm"
ARGS=(APP_SOURCE_DIR=ps64/src APP_SCE_SYS=ps64/sce_sys APP_PARAM=ps64/sce_sys/param.json APP_ASSETS=
      "APP_DEFINITIONS=PS64_NATIVE PS64_EMU"
      "APP_INCLUDE_PATHS=ps64/include ps64/include-lrc"
      "APP_STATIC_ARCHIVES=ps64/lib/lrc.a ps64/lib/vice.a ps64/lib/libSDL2.a ps64/lib/libcxx.a ps64/lib/libcxxabi.a ps64/lib/libunwind.a"
      "APP_WRAP_SYMBOLS=$(echo $WRAP)")
make -C "$B" "${ARGS[@]}"

rm -rf ps5/native/out
mkdir -p ps5/native/out
cp -r "$B/dist/PPSA06464" "$B/dist/PPSA06464.zip" ps5/native/out/
echo "native app: ps5/native/out/PPSA06464 and PPSA06464.zip"

if [ "$1" = deploy ]; then
    make -C "$B" deploy "PS5_HOST=${2:?deploy needs the PS5 address}" "${ARGS[@]}"
fi
