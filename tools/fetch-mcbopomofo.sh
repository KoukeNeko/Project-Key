#!/bin/sh
# McBopomofo at MCBOPOMOFO_COMMIT in the folder $1, fetched again whenever the commit moves, its
# dictionary, Source/Data/data.txt, and the associated phrases derived from it. Every build and package script on macOS, iOS and Linux
# runs this; platforms/windows/build.ps1 does the same in PowerShell.
set -eu
dir=$1
commit=$(tr -d '[:space:]' < "$(dirname "$0")/../MCBOPOMOFO_COMMIT")
if [ ! -d "$dir/.git" ]; then
  git init -q "$dir"
  git -C "$dir" remote add origin https://github.com/openvanilla/McBopomofo
fi
if [ "$(git -C "$dir" rev-parse -q --verify HEAD || true)" != "$commit" ]; then
  git -C "$dir" fetch -q --depth 1 origin "$commit"
  git -C "$dir" checkout -q FETCH_HEAD
  rm -f "$dir/Source/Data/data.txt" "$dir/Source/Data/associated-phrases-v2.txt"  # built again from the new commit
fi
[ -f "$dir/Source/Data/associated-phrases-v2.txt" ] || make -C "$dir/Source/Data" data.txt associated-phrases-v2.txt
