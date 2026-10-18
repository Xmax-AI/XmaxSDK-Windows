#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exec "$PWD/.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab" "$@"
