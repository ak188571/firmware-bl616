#!/usr/bin/env bash
set -euo pipefail
export PYTHONUTF8=1
cd "$(dirname "$0")/../.."
python3 tools/ui16/test_programmer.py
