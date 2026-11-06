#!/bin/sh
# Installs build/ProjectKey.app for the current user and registers it. Adding
# it as an input source is left to System Settings > Keyboard > Input Sources.
set -eu
cd "$(dirname "$0")"
APP=build/ProjectKey.app
DEST="$HOME/Library/Input Methods/ProjectKey.app"
[ -d "$APP" ] || { echo "run ./build.sh first" >&2; exit 1; }

# A running copy keeps the old binary loaded. The system does not always
# relaunch a killed input method, so the new copy is started below.
pkill -x ProjectKey || true
mkdir -p "$HOME/Library/Input Methods"
rm -rf "$DEST"
cp -R "$APP" "$DEST"
"$DEST/Contents/MacOS/ProjectKey" --register
open -g "$DEST"
echo "installed $DEST"
