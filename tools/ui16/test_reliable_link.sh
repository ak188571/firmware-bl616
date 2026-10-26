#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -DUI16_RELIABLE_FONT -Itests/ui16 -Iui \
  tests/ui16/reliable_link_test.cpp ui/ui16_link.cpp \
  -o build-ui16-host/reliable_link_test
./build-ui16-host/reliable_link_test build-ui16-host/reliable_link_wire.hex
