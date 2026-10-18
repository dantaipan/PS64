#!/bin/sh
# Build the PS5 payload in Docker. Output: ps5/out/PS64/ (copy to /data/homebrew/ on the PS5).
set -e
cd "$(dirname "$0")"
PATH="$PATH:/Applications/Docker.app/Contents/Resources/bin"
docker image inspect ps64-ps5-sdk >/dev/null 2>&1 || docker build -t ps64-ps5-sdk .
docker run --rm -v "$(cd .. && pwd)":/src -w /src/ps5 ps64-ps5-sdk make "$@"
