#!/bin/sh
# Installs Project Key on Linux, set up and ready to type with:
#   curl -fsSL https://raw.githubusercontent.com/KoukeNeko/Project-Key/main/platforms/linux/install.sh | sh
# 1. The program, as a package the package manager can remove again: from the apt or dnf
#    repository where one is built for the system (Debian 13, Ubuntu 24.04 and Fedora 43 on
#    x86_64); anywhere else built here from the latest release's source (apt, dnf or pacman).
# 2. What a package cannot do for the user: Fcitx5 as the input method and started at login,
#    Project Key in Fcitx5's input methods, and on GNOME the extension turned on.
# Run it as yourself, not with sudo: it asks for the password where it needs one. Running it
# again updates Project Key and repairs the setup.
# PROJECTKEY_SOURCE=<checkout> builds from that checkout instead of the latest release.
set -eu

REPO=KoukeNeko/Project-Key
PAGES=https://koukeneko.github.io/Project-Key
UUID=project-key@koukeneko.github.io

if [ -t 1 ]; then bold=$(printf '\033[1m'); plain=$(printf '\033[0m'); else bold= plain=; fi
step() { printf '%s==> %s%s\n' "$bold" "$*" "$plain"; }
note() { printf '    %s\n' "$*"; }
die() { printf '錯誤：%s\n' "$*" >&2; exit 1; }

[ "$(id -u)" != 0 ] || die "請以自己的帳號執行，不要加 sudo：需要時會詢問密碼。"
[ "$(uname -s)" = Linux ] || die "這個指令只適用於 Linux；macOS 與 Windows 的安裝方式見 https://github.com/$REPO#安裝"
command -v sudo >/dev/null || die "需要 sudo 才能安裝套件。"
command -v curl >/dev/null || die "需要 curl。"
[ -r /etc/os-release ] || die "讀不到 /etc/os-release，無法判斷發行版。"
# shellcheck disable=SC1091
. /etc/os-release
arch=$(uname -m)
work=$(mktemp -d "${TMPDIR:-/tmp}/projectkey.XXXXXX")
chmod 755 "$work"  # apt reads the downloaded package as its own user
trap 'rm -rf "$work"' EXIT

# Asks once for the password, before the long steps, rather than in the middle of them.
step "安裝 Project Key（需要 sudo 密碼）"
sudo -v

# ---- 1. The program ----

# The latest release's description from GitHub's API, asked for once.
latest_release() {
  [ -s "$work/release.json" ] || curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" >"$work/release.json"
  cat "$work/release.json"
}

# The data package (dictionary and models, over 100 MB) is attached to each release rather than
# kept in the repository, signed with the repository's key: downloaded and checked here. Without
# its signature on the release, the package is built from source instead of installed unchecked.
signed_data_package() {  # $1: deb or rpm
  latest_release | grep -q "projectkey-data[^\"]*\\.$1\\.asc\""
}

fetch_data_package() {  # $1: the key file, $2: deb or rpm
  url=$(latest_release | grep -o "https://[^\"]*projectkey-data[^\"]*\\.$2\"" | tr -d '"' | head -1)
  [ -n "$url" ] || die "最新的發行版本沒有資料套件。"
  file="$work/$(basename "$url")"
  curl -fsSL --retry 3 -o "$file" "$url"
  curl -fsSL --retry 3 -o "$file.asc" "$url.asc"
  gnupg=$(mktemp -d "$work/gnupg.XXXXXX")
  GNUPGHOME=$gnupg gpg --batch --quiet --import "$1" 2>/dev/null
  GNUPGHOME=$gnupg gpg --batch --quiet --verify "$file.asc" "$file" 2>/dev/null || die "資料套件的簽章不符，沒有安裝。"
  data_package=$file
}

install_from_apt_repository() {
  step "加入 Project Key 的 apt 套件庫（$suite）"
  command -v gpg >/dev/null || sudo apt-get install -y gpg
  sudo mkdir -p /etc/apt/keyrings
  curl -fsSL "$PAGES/apt/key.asc" | sudo tee /etc/apt/keyrings/projectkey.asc >/dev/null
  echo "deb [signed-by=/etc/apt/keyrings/projectkey.asc] $PAGES/apt $suite main" |
    sudo tee /etc/apt/sources.list.d/projectkey.list >/dev/null
  sudo apt-get update
  step "下載並驗證詞庫與選字模型（約 100 MB）"
  fetch_data_package /etc/apt/keyrings/projectkey.asc deb
  step "安裝套件"
  sudo apt-get install -y "$data_package" fcitx5-projectkey
}

install_from_dnf_repository() {
  step "加入 Project Key 的 dnf 套件庫"
  sudo tee /etc/yum.repos.d/projectkey.repo >/dev/null <<EOF
[projectkey]
name=Project Key
baseurl=$PAGES/rpm
enabled=1
gpgcheck=0
repo_gpgcheck=1
gpgkey=$PAGES/rpm/key.asc
EOF
  command -v gpg >/dev/null || sudo dnf install -y gnupg2
  curl -fsSL "$PAGES/rpm/key.asc" -o "$work/key.asc"
  step "下載並驗證詞庫與選字模型（約 100 MB）"
  fetch_data_package "$work/key.asc" rpm
  step "安裝套件"
  sudo dnf install -y "$data_package" fcitx5-projectkey
}

# The source to build: the latest release, or PROJECTKEY_SOURCE.
fetch_source() {
  src=$work/source
  if [ -n "${PROJECTKEY_SOURCE:-}" ]; then
    step "從 $PROJECTKEY_SOURCE 建置"
    # A copy, so the build leaves the checkout as it was.
    mkdir "$src"
    tar -C "$PROJECTKEY_SOURCE" --exclude=./.git --exclude=./platforms/linux/build --exclude=./platforms/linux/dist \
      --exclude=./platforms/linux/arch/src --exclude=./platforms/linux/arch/pkg -cf - . | tar -C "$src" -xf -
    return
  fi
  tag=$(latest_release | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -1)
  [ -n "$tag" ] || die "找不到最新的發行版本。"
  step "下載 Project Key $tag 的原始碼"
  mkdir "$src"
  curl -fsSL --retry 3 "https://github.com/$REPO/archive/refs/tags/$tag.tar.gz" | tar -xz -C "$src" --strip-components=1
}

# The build-dependency lists are release.yml's, which builds the published packages the same way.
build_deb() {
  step "安裝建置所需的套件"
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends ca-certificates git curl make build-essential dpkg-dev debhelper \
    fakeroot cmake ninja-build g++ python3 pkg-config libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
    fcitx5-modules-dev libenchant-2-dev libxcb1-dev libcairo2-dev libpango1.0-dev libwayland-dev qt6-base-dev \
    libadwaita-1-dev libsoup-3.0-dev libjson-glib-dev
  fetch_source
  step "建置套件（下載詞庫與模型，需要數分鐘）"
  sh "$src/platforms/linux/package-deb.sh" >"$work/build.log" 2>&1 || { tail -30 "$work/build.log" >&2; die "建置失敗。"; }
  step "安裝套件"
  sudo apt-get install -y --reinstall "$src"/platforms/linux/dist/*.deb
}

build_rpm() {
  step "安裝建置所需的套件"
  sudo dnf install -y rpm-build make tar gzip findutils git curl cmake ninja-build gcc-c++ python3 pkgconf-pkg-config \
    fcitx5-devel enchant2-devel libxcb-devel cairo-devel pango-devel wayland-devel qt6-qtbase-devel libadwaita-devel \
    libsoup3-devel json-glib-devel
  fetch_source
  step "建置套件（下載詞庫與模型，需要數分鐘）"
  sh "$src/platforms/linux/package-rpm.sh" >"$work/build.log" 2>&1 || { tail -30 "$work/build.log" >&2; die "建置失敗。"; }
  step "安裝套件"
  sudo dnf install -y "$src"/platforms/linux/dist/*.rpm
}

build_arch() {
  step "安裝建置所需的套件"
  sudo pacman -S --noconfirm --needed base-devel git cmake ninja python curl fcitx5 enchant hunspell-en_us libxcb cairo \
    pango wayland qt6-base libadwaita libsoup3 json-glib
  fetch_source
  step "建置並安裝套件（下載詞庫與模型，需要數分鐘）"
  (cd "$src/platforms/linux/arch" && makepkg --noconfirm -sif)
}

# The repositories have x86_64 packages for these releases; Ubuntu's derivatives (Pop!_OS, Mint)
# name the Ubuntu release they are built on in UBUNTU_CODENAME.
suite=${UBUNTU_CODENAME:-${VERSION_CODENAME:-}}
repository=false
[ "$arch" = x86_64 ] && [ -z "${PROJECTKEY_SOURCE:-}" ] && repository=true
unsigned() { note "最新發行版本的資料套件缺少簽章，改自原始碼建置。"; }
# Plain if/else throughout: in `a && f || g`, set -e does not apply inside f.
if command -v apt-get >/dev/null; then
  if [ "$repository" = true ] && { [ "$suite" = trixie ] || [ "$suite" = noble ]; }; then
    if signed_data_package deb; then
      install_from_apt_repository
    else
      unsigned
      build_deb
    fi
  else
    build_deb
  fi
elif command -v dnf >/dev/null; then
  if [ "$repository" = true ] && [ "${ID:-}" = fedora ] && [ "${VERSION_ID:-}" = 43 ]; then
    if signed_data_package rpm; then
      install_from_dnf_repository
    else
      unsigned
      build_rpm
    fi
  else
    build_rpm
  fi
elif command -v pacman >/dev/null; then
  build_arch
else
  die "不支援這個發行版（需要 apt、dnf 或 pacman）。可依 https://github.com/$REPO/tree/main/platforms/linux 自行建置。"
fi

# ---- 2. The user's setup ----

data=${XDG_DATA_HOME:-$HOME/.local/share}
config=${XDG_CONFIG_HOME:-$HOME/.config}
desktop=${XDG_CURRENT_DESKTOP:-$(systemctl --user show-environment 2>/dev/null | sed -n 's/^XDG_CURRENT_DESKTOP=//p')}
graphical=false
[ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ] && graphical=true

# Homebrew's projectkey-link puts links in the user's data folder, which Fcitx5 and GNOME read
# before /usr: they would keep loading that copy instead of this one.
for file in fcitx5/addon/projectkey.conf fcitx5/inputmethod/projectkey.conf fcitx5/themes/projectkey \
            gnome-shell/extensions/$UUID applications/projectkey-settings.desktop \
            icons/hicolor/scalable/apps/fcitx-projectkey.svg icons/hicolor/scalable/apps/fcitx-projectkey-symbolic.svg; do
  if [ -L "$data/$file" ]; then
    rm -f "$data/$file"
    linked=true
  fi
done
if [ "${linked:-false}" = true ]; then
  note "已移除 Homebrew 版建立於 $data 的連結；可執行 brew uninstall fcitx5-projectkey 移除 Homebrew 版。"
fi

step "將 Fcitx5 設為輸入法框架"
if command -v im-config >/dev/null; then
  # Debian and Ubuntu: ~/.xinputrc, read as the next session starts. im-config 1.x writes it with
  # -w; older ones (Ubuntu 24.04) with -n, which refuses to run without a display (over SSH)
  # although it does not use one.
  if im-config -h 2>&1 | grep -q -- '-w '; then
    im-config -w fcitx5 || note "im-config 沒有完成；可於登入後執行 im-config -w fcitx5。"
  else
    DISPLAY=${DISPLAY:-:0} im-config -n fcitx5 || note "im-config 沒有完成；可於登入後執行 im-config -n fcitx5。"
  fi
elif command -v imsettings-switch >/dev/null; then
  imsettings-switch -q fcitx5 || true  # Fedora
fi
kwin_starts_it=false
case ":$desktop:" in
  *:KDE:*)
    # Plasma on Wayland starts Fcitx5 itself once it is the virtual keyboard (input-method-v1).
    kwrite=$(command -v kwriteconfig6 || command -v kwriteconfig5 || true)
    if [ -n "$kwrite" ]; then
      if "$kwrite" --file kwinrc --group Wayland --key InputMethod /usr/share/applications/org.fcitx.Fcitx5.desktop &&
         [ "${XDG_SESSION_TYPE:-}" = wayland ]; then
        kwin_starts_it=true
      fi
    fi
    ;;
esac
# Started at login: some distributions' Fcitx5 has no autostart entry of its own (Ubuntu 26.10's
# has none), and without one it starts only when an app first calls it over D-Bus. Not where
# KWin starts it: a second Fcitx5 would only compete with that one.
if [ "$kwin_starts_it" = false ] && ! ls /etc/xdg/autostart/*[Ff]citx* >/dev/null 2>&1 &&
   [ ! -e "$config/autostart/org.fcitx.Fcitx5.desktop" ]; then
  mkdir -p "$config/autostart"
  cp /usr/share/applications/org.fcitx.Fcitx5.desktop "$config/autostart/"
fi

step "將 Project Key 加入 Fcitx5 的輸入法"
profile=$config/fcitx5/profile
was_running=false
pgrep -x fcitx5 >/dev/null && was_running=true
if [ -f "$profile" ] && grep -qx 'Name=projectkey' "$profile"; then
  note "已加入。"
else
  # Fcitx5 writes the profile as it exits, so it is stopped before the profile changes.
  if [ "$was_running" = true ]; then
    fcitx5-remote -e 2>/dev/null || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -x fcitx5 >/dev/null || break; sleep 0.5; done
    pkill -x fcitx5 2>/dev/null || true
  fi
  mkdir -p "$config/fcitx5"
  if [ -f "$profile" ]; then
    # After the group's other input methods, and the one Ctrl+Space switches to.
    items=$(grep -c '^\[Groups/0/Items/[0-9]*\]$' "$profile" || true)
    awk -v items="$items" '
      /^\[/ { section = $0 }
      section == "[Groups/0]" && /^DefaultIM=/ { print "DefaultIM=projectkey"; next }
      { print }
      END { printf "\n[Groups/0/Items/%d]\nName=projectkey\nLayout=\n", items }
    ' "$profile" >"$profile.new"
    mv "$profile.new" "$profile"
  else
    # The keyboard layout GNOME has first, else US; Fcitx5 names it keyboard-<layout>[-<variant>].
    layout=$(gsettings get org.gnome.desktop.input-sources sources 2>/dev/null |
      sed -n "s/^[^(]*('xkb', '\\([^']*\\)').*/\\1/p" | tr + -)
    layout=${layout:-us}
    cat >"$profile" <<EOF
[Groups/0]
Name=Default
Default Layout=$layout
DefaultIM=projectkey

[Groups/0/Items/0]
Name=keyboard-$layout
Layout=

[Groups/0/Items/1]
Name=projectkey
Layout=

[GroupOrder]
0=Default
EOF
  fi
fi
# Started anew in the desktop, so it loads this version; outside one (over SSH), at the next login.
if [ "$graphical" = true ]; then
  fcitx5 -r -d >/dev/null 2>&1 || true
fi

relogin=false
gnome_shell=false
gsettings list-keys org.gnome.shell >/dev/null 2>&1 && gnome_shell=true
case ":$desktop:$gnome_shell" in
  *:GNOME:*true)
    step "開啟 GNOME Shell 擴充功能「Project Key」"
    # GNOME loads extensions as the session starts: one installed now is unknown to the running
    # Shell, so gnome-extensions cannot turn it on. In the setting the next login reads, it is on.
    enabled=$(gsettings get org.gnome.shell enabled-extensions 2>/dev/null || echo '@as []')
    case "$enabled" in
      *"'$UUID'"*) ;;
      *'[]'*) gsettings set org.gnome.shell enabled-extensions "['$UUID']" ;;
      *) gsettings set org.gnome.shell enabled-extensions "$(printf '%s' "$enabled" | sed "s/]\$/, '$UUID']/")" ;;
    esac
    if [ "$(gsettings get org.gnome.shell disable-user-extensions 2>/dev/null)" = true ]; then
      note "注意：GNOME 的擴充功能目前全部停用；在「擴充功能」App 開啟後才會使用 Project Key 的候選字窗。"
    fi
    relogin=true
    ;;
esac

step "完成"
if [ "$relogin" = true ]; then
  note "請登出再登入一次，讓 GNOME 載入擴充功能（第一次安裝與每次更新後都需要）。"
fi
note "在輸入欄位按 Ctrl+Space 切換到 Project Key（ㄅ）。"
note "設定：應用程式選單的「Project Key 設定」，或執行 projectkey-settings。"
if [ "$graphical" = true ] && command -v projectkey-settings >/dev/null; then
  # The welcome window: shows the input method added and lets the user try typing.
  nohup projectkey-settings --welcome >/dev/null 2>&1 &
fi
