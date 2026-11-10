#!/bin/sh
# Builds fcitx5-projectkey_<version>_<arch>.deb into platforms/linux/dist/ from this checkout.
# Needs the Build-Depends in debian/control and dpkg-dev, debhelper; downloads McBopomofo and
# the trained models while building.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
version=$(sed -n 's/^project(.* VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/pk"
tar -C "$root" --exclude=./.git --exclude=./platforms/linux/build --exclude=./platforms/linux/dist -cf - . | tar -C "$work/pk" -xf -
cd "$work/pk"
tools/bigram-trainer/fetch-models.sh
mcb=platforms/linux/build/mcbopomofo
git init -q "$mcb"
git -C "$mcb" remote add origin https://github.com/openvanilla/McBopomofo
git -C "$mcb" fetch -q --depth 1 origin "$(tr -d '[:space:]' < MCBOPOMOFO_COMMIT)"
git -C "$mcb" checkout -q FETCH_HEAD
make -C "$mcb/Source/Data" data.txt
cp -r platforms/linux/debian debian
chmod +x debian/rules
printf 'fcitx5-projectkey (%s-1) unstable; urgency=medium\n\n  * Release %s.\n\n -- KoukeNeko <noreply@users.noreply.github.com>  %s\n' "$version" "$version" "$(date -R)" > debian/changelog
dpkg-buildpackage -b -us -uc
mkdir -p "$here/dist"
cp "$work"/fcitx5-projectkey_*.deb "$here/dist/"
ls "$here/dist"
