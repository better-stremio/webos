#!/bin/bash
# tests/collections/run.sh [export.json collections folders sources] : builds and runs the parser test; SB_ASAN=1 for a sanitizer build
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; OUT="${OUT:-$ROOT/build}"; mkdir -p "$OUT"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -O1 -g ${SB_ASAN:+-fsanitize=address,undefined} -I"$ROOT/src" -I"$ROOT/third_party/cjson" \
  "$ROOT/tests/collections/col_test.c" "$ROOT/src/core/collections.c" "$ROOT/third_party/cjson/cJSON.c" -lm -o "$OUT/col_test"
"$OUT/col_test" "$@"
