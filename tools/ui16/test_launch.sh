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
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -DUI16_SYSTEM_MENU \
 -Itests/ui16 -Iui tests/ui16/launch_test.cpp ui/ui16_browser.cpp ui/ui16_launch.cpp ui/ui16_navigation.cpp \
 ui/ui16_game_list.cpp ui/ui16_titles.cpp ui/ui16_system_menu.cpp -o build-ui16-host/launch_test
./build-ui16-host/launch_test
python3 tools/ui16/test_launch_adapters.py
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter -pedantic -DUI16_SYSTEM_MENU \
 -Itests/ui16 -Iui tests/ui16/popup_test.cpp ui/menu_manager.cpp -o build-ui16-host/popup_test
./build-ui16-host/popup_test
