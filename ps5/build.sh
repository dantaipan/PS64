#!/bin/sh
# Build the PS5 payload in Docker. Output: ps5/out/PS64/ (copy to /data/homebrew/ on the PS5).
set -e
cd "$(dirname "$0")"
PATH="$PATH:/Applications/Docker.app/Contents/Resources/bin"
../tools/get_vice.sh fetch          # the emulator's source (built inside Docker)
docker image inspect ps64-ps5-sdk >/dev/null 2>&1 || docker build -t ps64-ps5-sdk .
docker run --rm -e A64_CLIENT -v "$(cd .. && pwd)":/src -w /src/ps5 ps64-ps5-sdk make "$@"
