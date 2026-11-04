#!/bin/sh
# Installs .build/ProjectKey.app for the current user and registers it. Adding
# it as an input source is left to System Settings > Keyboard > Input Sources.
set -eu
cd "$(dirname "$0")"
APP=.build/ProjectKey.app
DEST="$HOME/Library/Input Methods/ProjectKey.app"
[ -d "$APP" ] || { echo "run ./build.sh first" >&2; exit 1; }

# A running copy keeps the old binary loaded; the system relaunches it on the next key.
pkill -x ProjectKey || true
mkdir -p "$HOME/Library/Input Methods"
rm -rf "$DEST"
cp -R "$APP" "$DEST"
"$DEST/Contents/MacOS/ProjectKey" --register
echo "installed $DEST"
