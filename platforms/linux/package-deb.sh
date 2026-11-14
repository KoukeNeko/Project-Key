#!/bin/sh
# Builds fcitx5-projectkey_<version>_<arch>.deb into platforms/linux/dist/ from this checkout.
# Needs the Build-Depends in debian/control and dpkg-dev, debhelper; downloads McBopomofo,
# llama.cpp and the trained models while building.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
version=$(sed -n 's/^project(.* VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")
# The codename keeps the files of the distributions apart in one repository pool.
codename=$(. /etc/os-release && echo "$VERSION_CODENAME")
work=$(mktemp -d "${TMPDIR:-/var/tmp}/pk.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/pk"
tar -C "$root" --exclude=./.git --exclude=./platforms/linux/build --exclude=./platforms/linux/dist -cf - . | tar -C "$work/pk" -xf -
cd "$work/pk"
tools/bigram-trainer/fetch-models.sh
mcb=platforms/linux/build/mcbopomofo
tools/fetch-mcbopomofo.sh "$mcb"
# The advanced model's llama.cpp, which debian/rules builds in.
tools/fetch-llama.sh platforms/linux/build/llama.cpp
export PROJECTKEY_LLAMA="$PWD/platforms/linux/build/llama.cpp"
cp -r platforms/linux/debian debian
chmod +x debian/rules
printf 'fcitx5-projectkey (%s-1~%s) unstable; urgency=medium\n\n  * Release %s.\n\n -- KoukeNeko <noreply@users.noreply.github.com>  %s\n' "$version" "$codename" "$version" "$(date -R)" > debian/changelog
dpkg-buildpackage -b -us -uc
mkdir -p "$here/dist"
cp "$work"/fcitx5-projectkey*.deb "$here/dist/"
ls "$here/dist"
