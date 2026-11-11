#!/bin/sh
# McBopomofo at MCBOPOMOFO_COMMIT in the folder $1, fetched again whenever the commit moves, and
# its dictionary, Source/Data/data.txt. Every build and package script on macOS, iOS and Linux
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
  rm -f "$dir/Source/Data/data.txt"  # built again from the new commit
fi
[ -f "$dir/Source/Data/data.txt" ] || make -C "$dir/Source/Data" data.txt
