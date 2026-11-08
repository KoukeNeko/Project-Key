#!/bin/sh
# Installs build/ProjectKey.app for the current user and registers it, and the settings app in
# ~/Applications. Adding it as an input source is left to System Settings > Keyboard > Input Sources.
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

# The settings app, for Launchpad and Spotlight. It has its own bundle identifier, so the system
# never takes it for a second copy of the input method.
SETTINGS="build/Project Key 設定.app"
SETTINGS_DEST="$HOME/Applications/Project Key 設定.app"
pkill -x ProjectKeySettings || true
mkdir -p "$HOME/Applications"
rm -rf "$SETTINGS_DEST"
cp -R "$SETTINGS" "$SETTINGS_DEST"
echo "installed $SETTINGS_DEST"
