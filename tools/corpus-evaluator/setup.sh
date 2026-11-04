#!/bin/sh
# Builds the engine adapters and prepares RIME data under .cache/. Needs Homebrew.
set -eu
cd "$(dirname "$0")"
brew list librime >/dev/null 2>&1 || brew install librime
brew list libchewing >/dev/null 2>&1 || brew install libchewing
BREW=$(brew --prefix)

mkdir -p .cache/bin
cc -O2 adapters/rime.c -I"$BREW/opt/librime/include" -L"$BREW/opt/librime/lib" -lrime -o .cache/bin/rime
cc -O2 adapters/chewing.c -I"$BREW/opt/libchewing/include" -L"$BREW/opt/libchewing/lib" -lchewing -o .cache/bin/chewing

RIME=.cache/rime
if [ ! -f "$RIME/user/build/bopomofo_tw.prism.bin" ]; then
  mkdir -p "$RIME/src" "$RIME/shared/opencc" "$RIME/user"
  for repo in rime-prelude rime-essay rime-bopomofo rime-terra-pinyin; do
    [ -d "$RIME/src/$repo" ] || git clone -q --depth 1 "https://github.com/rime/$repo" "$RIME/src/$repo"
  done
  cp "$RIME"/src/*/*.yaml "$RIME/shared/"
  cp "$RIME"/src/*/*.txt "$RIME/shared/" 2>/dev/null || true
  cp "$BREW"/share/opencc/*.* "$RIME/shared/opencc/"
  printf 'patch:\n  schema_list:\n    - schema: bopomofo_tw\n' > "$RIME/shared/default.custom.yaml"
  "$BREW/opt/librime/bin/rime_deployer" --build "$RIME/user" "$RIME/shared" "$RIME/user/build"
fi
MCB_COMMIT=257da536ddaf590aae0e74e6adb9b415988344f6  # same commit annotate.py reads readings from
MCB=.cache/mcbopomofo
if [ ! -d "$MCB/.git" ]; then
  git init -q "$MCB"
  git -C "$MCB" remote add origin https://github.com/openvanilla/McBopomofo
  git -C "$MCB" fetch -q --depth 1 origin "$MCB_COMMIT"
  git -C "$MCB" checkout -q FETCH_HEAD
fi
[ -f "$MCB/Source/Data/data.txt" ] || make -C "$MCB/Source/Data" data.txt
cmake -S "$MCB/Source/Engine" -B "$MCB/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$MCB/build" --target McBopomofoLMLib MandarinLib gramambular2_lib >/dev/null
c++ -std=c++20 -O2 adapters/mcbopomofo.cpp -I"$MCB/Source/Engine" \
  "$MCB/build/libMcBopomofoLMLib.a" "$MCB/build/Mandarin/libMandarinLib.a" "$MCB/build/gramambular2/libgramambular2_lib.a" \
  -o .cache/bin/mcbopomofo

echo "ready: .cache/bin/rime .cache/bin/chewing .cache/bin/mcbopomofo"
