#!/bin/sh
# Adds the packages in IN to the signed apt and dnf repositories in SITE (a checkout of the
# gh-pages branch, served at https://koukeneko.github.io/Project-Key/), under apt/ and rpm/.
#   publish-repos.sh IN SITE
# IN holds deb-trixie/ and deb-noble/ (package-deb.sh run on Debian 13 and Ubuntu 24.04) and
# rpm/ (package-rpm.sh on Fedora). GPG_KEY is the id of the signing key in the keyring.
# Needs reprepro, createrepo_c and gpg. Only the newest program package of each is kept. The data
# package (the models, over 100 MB) is not in the repositories, as GitHub refuses a file that
# large in a branch: release.yml attaches it to the release, and the README installs it from there.
set -eu
in=$1
site=$(cd "$2" && pwd)
apt=$site/apt
mkdir -p "$apt/conf" "$site/rpm"

: > "$apt/conf/distributions"
for suite in trixie noble; do
  cat >> "$apt/conf/distributions" <<EOF
Origin: Project Key
Label: Project Key
Codename: $suite
Architectures: amd64
Components: main
SignWith: $GPG_KEY

EOF
done

for suite in trixie noble; do
  reprepro -b "$apt" remove "$suite" fcitx5-projectkey
  reprepro -b "$apt" includedeb "$suite" "$in/deb-$suite"/fcitx5-projectkey_*.deb
done
reprepro -b "$apt" deleteunreferenced
gpg --armor --export "$GPG_KEY" > "$apt/key.asc"

rpm=$site/rpm
find "$rpm" -name 'fcitx5-projectkey-*.rpm' -delete
cp "$in"/rpm/fcitx5-projectkey-[0-9]*.rpm "$rpm"
createrepo_c --update "$rpm"
gpg --batch --yes --armor --detach-sign -u "$GPG_KEY" -o "$rpm/repodata/repomd.xml.asc" "$rpm/repodata/repomd.xml"
gpg --armor --export "$GPG_KEY" > "$rpm/key.asc"
