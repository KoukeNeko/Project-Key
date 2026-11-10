Name:           fcitx5-projectkey
Version:        %{pk_version}
Release:        1%{?dist}
Summary:        Zhuyin input method for Fcitx5
License:        MIT
URL:            https://github.com/KoukeNeko/Project-Key
# Made by package-rpm.sh, which also puts McBopomofo and the models in it.
Source0:        pk.tar.gz

BuildRequires:  cmake, ninja-build, gcc-c++, python3, git, curl, pkgconf-pkg-config
BuildRequires:  fcitx5-devel, enchant2-devel, libxcb-devel, cairo-devel, pango-devel, wayland-devel
BuildRequires:  qt6-qtbase-devel, libadwaita-devel, libsoup3-devel, json-glib-devel
Requires:       fcitx5, hunspell-en-US, fcitx5-projectkey-data = %{data_version}-1
Recommends:     google-noto-sans-cjk-fonts

%global debug_package %{nil}

%description
Project Key, a Zhuyin (Bopomofo) input method with sentence prediction,
for Fcitx5 on GNOME, KDE and other desktops.

# The dictionary and models change far less often than the program: a package of their own,
# versioned by the models' date and the McBopomofo commit (package-rpm.sh).
%package data
Summary:        Dictionary and context models for Project Key
Version:        %{data_version}
Release:        1
License:        CC-BY-SA-4.0 AND LGPL-2.1-or-later
BuildArch:      noarch

%description data
The McBopomofo dictionary and the trained sentence models.

%prep
%setup -q -n pk

%build
cmake -S platforms/linux -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DMCBOPOMOFO=$PWD/platforms/linux/build/mcbopomofo -DREQUIRE_MODELS=ON
cmake --build build

%check
for t in ComposerTest ControllerTest; do build/$t platforms/linux/build/mcbopomofo/Source/Data/data.txt; done

%install
DESTDIR=%{buildroot} cmake --install build
rm -f %{buildroot}/usr/share/icons/hicolor/icon-theme.cache

%files
/usr/bin/projectkey-settings*
/usr/lib64/fcitx5/projectkey.so
/usr/share/applications/projectkey-settings.desktop
/usr/share/fcitx5/addon/projectkey.conf
/usr/share/fcitx5/inputmethod/projectkey.conf
/usr/share/fcitx5/themes/projectkey
/usr/share/gnome-shell/extensions/project-key@koukeneko.github.io
/usr/share/icons/hicolor/scalable/apps/fcitx-projectkey*.svg

%files data
/usr/share/fcitx5/projectkey
