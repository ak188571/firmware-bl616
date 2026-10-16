#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
compiler_extra=()
if command -v cygpath >/dev/null 2>&1; then
  compiler_extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -DUI16_SYSTEM_MENU \
  -Itests/ui16 -Iui tests/ui16/navigation_test.cpp \
  ui/ui16_navigation.cpp ui/ui16_system_menu.cpp -o build-ui16-host/navigation_test
./build-ui16-host/navigation_test

# Compile the unchanged UART-function prefix of the actual firmware source with
# a host SDK shim. Remaining utils.cpp functions require the hardware SDK.
python3 - <<'PY'
from pathlib import Path
source = Path("utils/utils.cpp").read_text()
marker = "uint32_t get_file_size("
assert source.count(marker) == 1
Path("build-ui16-host/uart_functions.cpp").write_text(source.split(marker)[0])
PY
for mode in on off; do
  definitions=()
  if [[ "$mode" == on ]]; then definitions=(-DUI16_SYSTEM_MENU); fi
  g++ "${compiler_extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic "${definitions[@]}" \
    -Itests/ui16 -Iui tests/ui16/transport_owner_test.cpp build-ui16-host/uart_functions.cpp \
    -o "build-ui16-host/transport_owner_$mode"
  "./build-ui16-host/transport_owner_$mode"
done
