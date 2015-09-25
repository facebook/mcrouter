#!/bin/bash
ROOT_PATH="$(pwd)"
SCRIPT_PATH="$(dirname "$0")"
cd "$SCRIPT_PATH"/../.. && "$ROOT_PATH/$1"
