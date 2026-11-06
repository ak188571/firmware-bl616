#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
extra=()
if command -v cygpath >/dev/null 2>&1; then
  extra+=("-B$(cygpath -m /ucrt64/bin)/")
  export MSYS2_ARG_CONV_EXCL='*'
fi
for system in 1 2; do
g++ "${extra[@]}" -std=c++17 -Wall -Wextra -Werror -pedantic -DUI16_SYSTEM_MENU -DTEST_CRT_SYSTEM="$system" \
 -Itests/ui16 -Iui tests/ui16/crt_save_test.cpp ui/ui16_crt_save.cpp ui/ui16_crt_restore.cpp ui/ui16_rom_id.cpp -o build-ui16-host/crt_save_test
./build-ui16-host/crt_save_test
done
