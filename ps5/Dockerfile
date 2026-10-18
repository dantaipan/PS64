# Build environment for the PS5 payload: ps5-payload-sdk plus a static SDL2
# from the ps5-payload-dev SDL port, so ps64.elf has no runtime .so deps.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates clang-18 lld-18 llvm-18 make cmake git pkg-config \
        unzip wget python3 \
    && rm -rf /var/lib/apt/lists/*

RUN wget -q https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip \
    && unzip -q -d /opt ps5-payload-sdk.zip && rm ps5-payload-sdk.zip
ENV PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
ENV LLVM_CONFIG=/usr/bin/llvm-config-18

# Same recipe as pacbrew-repo/SDL2, minus the Mesa GL/Vulkan patch, built static.
RUN git clone --depth 1 https://github.com/ps5-payload-dev/SDL.git /tmp/SDL \
    && bash -c 'source $PS5_PAYLOAD_SDK/toolchain/prospero.sh \
        && $CMAKE -S /tmp/SDL -B /tmp/SDL/build -DCMAKE_BUILD_TYPE=Release \
                  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
        && make -C /tmp/SDL/build -j"$(nproc)" \
        && make -C /tmp/SDL/build install \
        && ln -sf ../target/$PREFIX/bin/sdl2-config $PS5_PAYLOAD_SDK/bin/prospero-sdl2-config' \
    && rm -rf /tmp/SDL

WORKDIR /src
