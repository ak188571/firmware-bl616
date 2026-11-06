#!/usr/bin/env bash
set -euo pipefail
export PATH=/ucrt64/bin:/usr/bin:$PATH
export MSYS2_ARG_CONV_EXCL='*'
cd "$(dirname "$0")/../.."
mkdir -p build-ui16-host
g++ -BC:/msys64/ucrt64/bin/ -std=c++17 -Wall -Wextra -Werror -pedantic \
 -DUI16_SYSTEM_MENU -DBL616 -Itests/storage -Iutils \
 tests/storage/snes_sd_guard_test.cpp utils/tc_snes_sd_guard.cpp -o build-ui16-host/snes_sd_guard.exe
for scenario in legacy success read-timeout write-error; do
 ./build-ui16-host/snes_sd_guard.exe "$scenario"
done
