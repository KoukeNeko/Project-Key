#!/bin/sh
# Runs install.sh's edit of an existing Fcitx5 profile on sample profiles: Project Key is added
# after the group's input methods and becomes its default, and nothing else changes.
set -eu
cd "$(dirname "$0")"
# The edit itself, taken from install.sh (which runs alone, so it cannot call a file of its own).
edit=$(sed -n '/items=\$(grep -c/,/>"\$profile.new"/p' install.sh)
[ -n "$edit" ] || { echo "test-profile.sh: the profile edit is not in install.sh" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
profile=$work/profile

cat >"$profile" <<'PROFILE'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=pinyin

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=pinyin
Layout=

[Groups/1]
Name=Other
Default Layout=us
DefaultIM=anthy

[Groups/1/Items/0]
Name=anthy
Layout=

[GroupOrder]
0=Default
1=Other
PROFILE
eval "$edit"
result=$(cat "$profile.new")
check() { printf '%s\n' "$result" | grep -qxF "$1" || { echo "test-profile.sh: missing line: $1" >&2; exit 1; }; }
check '[Groups/0/Items/2]'
check 'Name=projectkey'
check 'DefaultIM=anthy'  # the other group keeps its own
[ "$(printf '%s\n' "$result" | grep -c '^DefaultIM=projectkey$')" = 1 ] || { echo "test-profile.sh: DefaultIM of Groups/0 not Project Key" >&2; exit 1; }
printf '%s\n' "$result" | awk '/^\[/ { section = $0 } section == "[Groups/0]" && /^DefaultIM=/ { found = $0 } END { exit found == "DefaultIM=projectkey" ? 0 : 1 }' ||
  { echo "test-profile.sh: DefaultIM=projectkey is not in [Groups/0]" >&2; exit 1; }
[ "$(printf '%s\n' "$result" | grep -c '^Name=pinyin$')" = 1 ] || { echo "test-profile.sh: an input method was lost" >&2; exit 1; }
echo "test-profile.sh passed"
