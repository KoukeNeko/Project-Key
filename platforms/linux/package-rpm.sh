#!/bin/sh
# Builds fcitx5-projectkey-<version>-1.<dist>.<arch>.rpm into platforms/linux/dist/ from this checkout.
# Needs rpm-build and the BuildRequires in rpm/fcitx5-projectkey.spec (Fedora); downloads
# McBopomofo and the trained models while building.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
version=$(sed -n 's/^project(.* VERSION \([0-9.]*\).*/\1/p' "$here/CMakeLists.txt")
work=$(mktemp -d "${TMPDIR:-/var/tmp}/pk.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/pk"
tar -C "$root" --exclude=./.git --exclude=./platforms/linux/build --exclude=./platforms/linux/dist -cf - . | tar -C "$work/pk" -xf -
cd "$work/pk"
tools/bigram-trainer/fetch-models.sh
mcb=platforms/linux/build/mcbopomofo
tools/fetch-mcbopomofo.sh "$mcb"
cd "$work"
mkdir -p rpmbuild/SOURCES
tar czf rpmbuild/SOURCES/pk.tar.gz pk
data_version="$(tr - . < "$work/pk/tools/bigram-trainer/.cache/models-version.txt")+mcb$(cut -c1-7 "$work/pk/MCBOPOMOFO_COMMIT")"
rpmbuild -bb --define "_topdir $work/rpmbuild" --define "pk_version $version" --define "data_version $data_version" "$here/rpm/fcitx5-projectkey.spec"
mkdir -p "$here/dist"
cp rpmbuild/RPMS/*/*.rpm "$here/dist/"
ls "$here/dist"
