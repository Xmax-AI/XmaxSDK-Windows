#!/usr/bin/env bash
set -euo pipefail

action="${1:-build}"
if [[ $# -gt 0 ]]; then shift; fi
case "$action" in
  configure|build|test|open|run) ;;
  *) echo "Usage: $0 [configure|build|test|open|run] [PowerShell options]" >&2; exit 2 ;;
esac
if ! command -v prlctl >/dev/null 2>&1; then
  echo "Parallels Desktop prlctl is required." >&2
  exit 1
fi

# The configured share points to this repository; scripts are read directly, never copied.
vm="${XMAX_WINDOWS_VM:-Windows 11}"
source_root="${XMAX_WINDOWS_SOURCE:-\\\\Mac\\XmaxSDK}"
exec prlctl exec "$vm" --current-user powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$source_root\scripts\windows-dev.ps1" -Action "$action" "$@"
