#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
if [[ ! -f build-ui16-host/test_sd_font.ui16 ]]; then
  python3 tools/ui16/build_sd_font.py --monitor ../monitor --output build-ui16-host/test_sd_font.ui16
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -DUI16_SYSTEM_MENU -DUI16_SD_FONT -DUI16_RELIABLE_FONT \
  -Itests/ui16 -Iui tests/ui16/reliable_menu_test.cpp \
  ui/ui16_system_menu.cpp ui/ui16_font_cache.cpp ui/ui16_sd_font.cpp ui/ui16_font_sender.cpp \
  -o build-ui16-host/reliable_menu_test
./build-ui16-host/reliable_menu_test build-ui16-host/test_sd_font.ui16
