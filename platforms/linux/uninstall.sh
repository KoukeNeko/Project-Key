#!/bin/sh
# Removes Project Key as install.sh installed it:
#   curl -fsSL https://raw.githubusercontent.com/KoukeNeko/Project-Key/main/platforms/linux/uninstall.sh | sh
# The packages, the apt or dnf repository, and the GNOME extension's place in the enabled list go.
# Fcitx5 drops Project Key from its input methods by itself once the addon is gone. Kept: the user's
# words and learned choices, and Fcitx5 as the input method framework, which other input methods
# may use; the end says how to remove them.
set -eu

UUID=project-key@koukeneko.github.io

if [ -t 1 ]; then bold=$(printf '\033[1m'); plain=$(printf '\033[0m'); else bold= plain=; fi
step() { printf '%s==> %s%s\n' "$bold" "$*" "$plain"; }
note() { printf '    %s\n' "$*"; }
die() { printf '錯誤：%s\n' "$*" >&2; exit 1; }

[ "$(id -u)" != 0 ] || die "請以自己的帳號執行，不要加 sudo：需要時會詢問密碼。"

# The packages install.sh installs: from the repositories or built here, under the same names.
packages=
if command -v dpkg-query >/dev/null; then
  packages=$(dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' fcitx5-projectkey fcitx5-projectkey-data 2>/dev/null |
    awk '$1 == "ii" { print $2 }' | tr '\n' ' ')
  remove="sudo apt-get remove -y"
elif command -v rpm >/dev/null && command -v dnf >/dev/null; then
  packages=$(rpm -q --qf '%{NAME}\n' fcitx5-projectkey fcitx5-projectkey-data 2>/dev/null | grep -v 'not installed' | tr '\n' ' ' || true)
  remove="sudo dnf remove -y"
elif command -v pacman >/dev/null; then
  packages=$(pacman -Qq fcitx5-projectkey 2>/dev/null || true)
  remove="sudo pacman -R --noconfirm"
fi
repositories=
for file in /etc/apt/sources.list.d/projectkey.list /etc/apt/keyrings/projectkey.asc /etc/yum.repos.d/projectkey.repo; do
  [ -e "$file" ] && repositories="$repositories $file"
done

if [ -n "${packages% }" ] || [ -n "$repositories" ]; then
  step "移除 Project Key（需要 sudo 密碼）"
  sudo -v
  if [ -n "${packages% }" ]; then
    # shellcheck disable=SC2086  # the packages are separate words
    $remove $packages
  fi
  if [ -n "$repositories" ]; then
    note "移除套件庫設定：$repositories"
    # shellcheck disable=SC2086
    sudo rm -f $repositories
  fi
else
  note "沒有找到 Project Key 的套件。"
fi

extension_removed=false
if gsettings list-keys org.gnome.shell >/dev/null 2>&1; then
  enabled=$(gsettings get org.gnome.shell enabled-extensions)
  case "$enabled" in
    *"'$UUID'"*)
      step "從 GNOME 開啟的擴充功能中移除「Project Key」"
      uuid=$(printf '%s' "$UUID" | sed 's/[.]/\\./g')
      gsettings set org.gnome.shell enabled-extensions \
        "$(printf '%s' "$enabled" | sed -e "s/'$uuid', //" -e "s/, '$uuid'//" -e "s/'$uuid'//")"
      extension_removed=true
      ;;
  esac
fi

# Restarted without the addon, Fcitx5 drops Project Key from its input methods and saves that.
if pgrep -x fcitx5 >/dev/null && [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then
  step "重新啟動 Fcitx5"
  fcitx5 -r -d >/dev/null 2>&1 || true
fi

step "完成"
if [ "$extension_removed" = true ]; then
  note "GNOME 於登出再登入後才會卸下擴充功能。"
fi
data=${XDG_DATA_HOME:-$HOME/.local/share}/projectkey
config=${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf/projectkey.conf
if [ -e "$data" ] || [ -e "$config" ]; then
  note "保留了自訂詞、排除詞、選字紀錄與設定；不再需要時執行："
  note "  rm -rf '$data' '$config'"
fi
note "Fcitx5 仍是輸入法框架。要改回 IBus：im-config -w ibus（較舊的 im-config 為 -n ibus），"
note "並刪除 ~/.config/autostart/org.fcitx.Fcitx5.desktop（若有）。"
