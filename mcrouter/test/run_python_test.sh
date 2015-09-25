#!/bin/bash
PYTHON_PATH="$1"
TEST_NAME="$(basename "$2")"
TEST_MODULE="${TEST_NAME%.*}"
SCRIPT_PATH="$(dirname "$0")"
cd "$SCRIPT_PATH"/../.. && $PYTHON_PATH -B -m unittest -q mcrouter.test."$TEST_MODULE"
