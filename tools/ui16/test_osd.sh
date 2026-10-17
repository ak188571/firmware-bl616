#!/usr/bin/env bash
set -euo pipefail
export PYTHONUTF8=1
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -pedantic -DUI16_SYSTEM_MENU \
 -Itests/ui16 -Iui tests/ui16/osd_test.cpp ui/ui16_osd.cpp ui/ui16_osd_menu.cpp \
 ui/ui16_navigation.cpp ui/menu_manager.cpp -o build-ui16-host/osd_test
./build-ui16-host/osd_test
python3 tools/ui16/test_osd_driver.py
