#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -pedantic -DUI16_UART_SELFTEST \
  -Itests/ui16 -Iui tests/ui16/sender_test.cpp ui/ui16_selftest.cpp \
  -o build-ui16-host/sender_test
./build-ui16-host/sender_test
