#!/bin/sh
# Compile and run the Homey JSON mapping test. Needs a cJSON source tree.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CJSON_DIR=${CJSON_DIR:-}
if [ -z "$CJSON_DIR" ]; then
    for candidate in \
        "$ROOT/managed_components/espressif__cjson" \
        /usr/include
    do
        if [ -f "$candidate/cJSON.h" ]; then
            CJSON_DIR=$candidate
            break
        fi
    done
fi
if [ -z "$CJSON_DIR" ] || [ ! -f "$CJSON_DIR/cJSON.h" ]; then
    echo "cJSON.h not found. Set CJSON_DIR." >&2
    exit 1
fi
CJSON_SRC=$CJSON_DIR/cJSON.c
if [ ! -f "$CJSON_SRC" ]; then
    echo "cJSON.c not found in $CJSON_DIR" >&2
    exit 1
fi
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror \
    -I "$ROOT/components/homey_client/include" \
    -I "$CJSON_DIR" \
    "$ROOT/components/homey_client/src/homey_map.cpp" \
    "$ROOT/test/homey_map_test.cpp" \
    "$CJSON_SRC" \
    -o "$OUT"
"$OUT"
