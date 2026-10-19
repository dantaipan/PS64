FROM ps64-ps5-sdk
RUN apt-get update && apt-get install -y --no-install-recommends \
        ninja-build ccache curl pkg-config python3-venv python3-pip tar xz-utils zstd \
        clang-format-18 clang-tidy-18 file \
    && rm -rf /var/lib/apt/lists/*
