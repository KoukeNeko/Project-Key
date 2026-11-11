#!/bin/sh
# Wraps build/ProjectKey.app and the settings app in build/ProjectKey-<version>-macos.pkg. It installs for the
# current user, into ~/Library/Input Methods and ~/Applications as install.sh does, so no admin password is needed
# and the in-app updater and the Homebrew cask replace the same copy. Run ./build.sh first.
set -eu
cd "$(dirname "$0")"
APP=build/ProjectKey.app
SETTINGS="build/Project Key 設定.app"
[ -d "$APP" ] && [ -d "$SETTINGS" ] || { echo "run ./build.sh first" >&2; exit 1; }
VERSION="$(plutil -extract CFBundleShortVersionString raw Resources/Info.plist)"
ID=org.projectkey.inputmethod.pkg
WORK=build/pkg
rm -rf "$WORK"
mkdir -p "$WORK/root/Library/Input Methods" "$WORK/root/Applications"
cp -R "$APP" "$WORK/root/Library/Input Methods/"
cp -R "$SETTINGS" "$WORK/root/Applications/"

# Not relocatable: the installer would otherwise update a copy of the bundle it finds elsewhere.
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist" >/dev/null
# Some pkgbuild versions list the key already, others do not.
for i in 0 1; do
  /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist" 2>/dev/null ||
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/components.plist"
done
pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" --scripts pkg \
  --identifier "$ID" --version "$VERSION" --install-location / "$WORK/component.pkg" >/dev/null

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Project Key</title>
  <options customize="never" hostArchitectures="arm64"/>
  <domains enable_localSystem="false" enable_currentUserHome="true"/>
  <choices-outline><line choice="default"/></choices-outline>
  <choice id="default"><pkg-ref id="$ID"/></choice>
  <pkg-ref id="$ID" version="$VERSION">component.pkg</pkg-ref>
</installer-gui-script>
XML
OUT="build/ProjectKey-$VERSION-macos.pkg"
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK" "$OUT" >/dev/null
echo "built $OUT"
