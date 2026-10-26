#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
MONITOR="$(cd ../monitor && pwd)"
mkdir -p build-ui16-host
python3 tools/ui16/test_sd_font.py
python3 tools/ui16/build_sd_font.py --monitor "$MONITOR" --output build-ui16-host/test_sd_font.ui16
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic \
 -Itests/ui16 -Iui tests/ui16/sd_font_test.cpp ui/ui16_sd_font.cpp \
 -o build-ui16-host/sd_font_test
./build-ui16-host/sd_font_test build-ui16-host/test_sd_font.ui16
