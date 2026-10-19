#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'USAGE'
Usage: bash scripts/run-xlab.sh [--preview | --preview-session]
  No arguments: normal startup; Start Camera requires an API Key and an RTC-enabled build.
  --preview: UI layout preview; camera and generation are disabled.
  --preview-session: open the UI session preview directly; camera and generation are disabled.
The current macOS development build has no Agora RTC backend. Use the Windows demo for live video.
USAGE
  exit 0
fi
if [[ ! -x "$PWD/.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab" ]]; then
  echo "XLab has not been built. Run: bash scripts/verify.sh --example" >&2
  exit 1
fi
exec "$PWD/.build/xlab/Examples/XLab/XLab.app/Contents/MacOS/XLab" "$@"
