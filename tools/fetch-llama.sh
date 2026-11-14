#!/bin/sh
# llama.cpp (MIT) at the release LLAMA_RELEASE names, in the folder $1, fetched again whenever the
# release moves: the advanced model (core/engine/AdvancedModel.h) runs on it. The package scripts,
# the PKGBUILD and the workflows pass the folder as PROJECTKEY_LLAMA.
set -eu
dir=$1
release=$(tr -d '[:space:]' < "$(dirname "$0")/../LLAMA_RELEASE")
if [ ! -d "$dir/.git" ]; then
  git init -q "$dir"
  git -C "$dir" remote add origin https://github.com/ggml-org/llama.cpp
fi
if [ "$(git -C "$dir" describe --tags --exact-match 2>/dev/null || true)" != "$release" ]; then
  git -C "$dir" fetch -q --depth 1 origin tag "$release"
  git -C "$dir" checkout -q "$release"
fi
