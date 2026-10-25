#!/usr/bin/env bash
set -euo pipefail
vm="${XMAX_WINDOWS_VM:-Windows 11}"
source_root="${XMAX_WINDOWS_SOURCE:-\\\\Mac\\XmaxSDK}"
exec prlctl exec "$vm" --current-user powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$source_root\scripts\run-winui.ps1" "$@"
