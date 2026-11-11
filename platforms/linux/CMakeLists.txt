# The Fcitx5 input method: an addon that drives the shared engine
# (core/engine) and shows its candidates in its own window (Sources/Panel.cpp), on
# X11 or as a Wayland input method popup, and the settings windows (Settings/).
# build.sh fetches McBopomofo (MIT) into build/mcbopomofo and points MCBOPOMOFO here.
cmake_minimum_required(VERSION 3.16)
project(fcitx5-projectkey VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(Fcitx5ModuleDBus REQUIRED)
find_package(Fcitx5ModuleWayland REQUIRED)
find_package(Fcitx5ModuleNotifications REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENCHANT REQUIRED IMPORTED_TARGET enchant-2)
pkg_check_modules(PANEL REQUIRED IMPORTED_TARGET xcb cairo cairo-xcb pangocairo wayland-client gio-2.0)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
include(GNUInstallDirs)

# The KDE settings window in the Qt that KDE's own look (the Breeze style and KDE's platform
# theme) is installed for: Qt 6 on Plasma 6, Qt 5 on Plasma 5 (Ubuntu 24.04), where a Qt 6
# window would fall back to Fusion. Distributions keep Qt's plugins in <libdir>/qt5|qt6/plugins, the
# libdir Fcitx5 is in (installed elsewhere, as by Homebrew, the install prefix's is not it).
set(PROJECTKEY_QT "" CACHE STRING "Qt for the KDE settings window: 5 or 6 (default: the one KDE's look is installed for)")
if(NOT PROJECTKEY_QT)
  set(PROJECTKEY_QT 6)
  get_filename_component(THEMES "${Fcitx5Core_DIR}/../.." ABSOLUTE)
  if(EXISTS "${THEMES}/qt5/plugins/platformthemes/KDEPlasmaPlatformTheme.so" AND
     NOT EXISTS "${THEMES}/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so")
    set(PROJECTKEY_QT 5)
  endif()
endif()
find_package(Qt${PROJECTKEY_QT} REQUIRED COMPONENTS Widgets DBus Network)
message(STATUS "KDE settings window: Qt ${PROJECTKEY_QT}")

set(MCBOPOMOFO "" CACHE PATH "McBopomofo checkout")
set(CORE "${CMAKE_CURRENT_SOURCE_DIR}/../../core/engine")
add_subdirectory("${MCBOPOMOFO}/Source/Engine" mcbopomofo EXCLUDE_FROM_ALL)
# Workaround on McBopomofoLMLib over libcxx
# See: https://github.com/openvanilla/McBopomofo/pull/932
target_compile_options(McBopomofoLMLib PRIVATE "SHELL:-include iterator")

# The engine shared with macOS, Windows and iOS.
add_library(engine STATIC "${CORE}/Composer.cpp" "${CORE}/Bigram.cpp" "${CORE}/CharModel.cpp" "${CORE}/SentenceModel.cpp" "${CORE}/PersonalModel.cpp" "${CORE}/Controller.cpp")
target_include_directories(engine PUBLIC "${CORE}" "${MCBOPOMOFO}/Source/Engine")
find_package(Threads REQUIRED)  # SentenceModel's matrix products
target_link_libraries(engine PUBLIC McBopomofoLMLib MandarinLib gramambular2_lib Threads::Threads)

foreach(test ComposerTest ControllerTest)
  add_executable(${test} "${CORE}/Tests/${test}.cpp")
  target_link_libraries(${test} engine)
endforeach()

set(DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/fcitx5/projectkey")
# The input method popups the candidate window goes into on Wayland (wlroots' protocol, as Fcitx5 uses).
set(IM_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/protocols/input-method-unstable-v2.xml")
add_custom_command(
  OUTPUT input-method-unstable-v2-client-protocol.h input-method-unstable-v2-protocol.c
  COMMAND "${WAYLAND_SCANNER}" client-header "${IM_PROTOCOL}" input-method-unstable-v2-client-protocol.h
  COMMAND "${WAYLAND_SCANNER}" private-code "${IM_PROTOCOL}" input-method-unstable-v2-protocol.c
  DEPENDS "${IM_PROTOCOL}")
# The blur behind them, where the compositor offers it (wayland-protocols' staging, newer than the distributions').
set(EFFECT_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-background-effect-v1.xml")
add_custom_command(
  OUTPUT ext-background-effect-v1-client-protocol.h ext-background-effect-v1-protocol.c
  COMMAND "${WAYLAND_SCANNER}" client-header "${EFFECT_PROTOCOL}" ext-background-effect-v1-client-protocol.h
  COMMAND "${WAYLAND_SCANNER}" private-code "${EFFECT_PROTOCOL}" ext-background-effect-v1-protocol.c
  DEPENDS "${EFFECT_PROTOCOL}")
add_library(projectkey MODULE Sources/ProjectKey.cpp Sources/Panel.cpp Sources/X11Screen.cpp Sources/GnomeScreen.cpp Sources/WaylandScreen.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/input-method-unstable-v2-protocol.c" "${CMAKE_CURRENT_BINARY_DIR}/ext-background-effect-v1-protocol.c")
target_include_directories(projectkey PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(projectkey PRIVATE engine Fcitx5::Core Fcitx5::Utils Fcitx5::Module::DBus Fcitx5::Module::Wayland Fcitx5::Module::Notifications
  PkgConfig::ENCHANT PkgConfig::PANEL)
target_compile_definitions(projectkey PRIVATE PROJECTKEY_DATA_DIR="${DATA_DIR}" FCITX_GETTEXT_DOMAIN="fcitx5-projectkey"
  PROJECTKEY_SETTINGS="${CMAKE_INSTALL_FULL_BINDIR}/projectkey-settings")
set_target_properties(projectkey PROPERTIES PREFIX "")

# The settings window, opened from the Fcitx5 menu or the app launcher through the
# projectkey-settings script: Qt on KDE, GTK (libadwaita) on GNOME when it is built.
add_executable(projectkey-settings-qt Settings/Qt.cpp)
target_link_libraries(projectkey-settings-qt PRIVATE Qt${PROJECTKEY_QT}::Widgets Qt${PROJECTKEY_QT}::DBus Qt${PROJECTKEY_QT}::Network)
target_include_directories(projectkey-settings-qt PRIVATE "${CORE}")  # Models.h
target_compile_definitions(projectkey-settings-qt PRIVATE PROJECTKEY_VERSION="${PROJECT_VERSION}" PROJECTKEY_DATA_DIR="${DATA_DIR}")
pkg_check_modules(ADWAITA IMPORTED_TARGET libadwaita-1 libsoup-3.0 json-glib-1.0)
if(ADWAITA_FOUND)
  add_executable(projectkey-settings-gtk Settings/Gtk.cpp)
  target_link_libraries(projectkey-settings-gtk PRIVATE PkgConfig::ADWAITA)
  target_include_directories(projectkey-settings-gtk PRIVATE "${CORE}")  # Models.h
  target_compile_definitions(projectkey-settings-gtk PRIVATE PROJECTKEY_VERSION="${PROJECT_VERSION}" PROJECTKEY_DATA_DIR="${DATA_DIR}")
  install(TARGETS projectkey-settings-gtk DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
  message(STATUS "libadwaita, libsoup 3 or json-glib missing: building only the Qt settings window")
endif()

install(TARGETS projectkey DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5")
install(TARGETS projectkey-settings-qt DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(PROGRAMS projectkey-settings DESTINATION "${CMAKE_INSTALL_BINDIR}")
# By full path: a session's PATH may not have the install prefix's bin (Homebrew's).
configure_file(projectkey-settings.desktop.in projectkey-settings.desktop @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/projectkey-settings.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
# The icon (tools/icon/make-icons.ps1), for the input method and the settings launcher. Desktops read
# icons through hicolor's cache, so refresh it as a package's trigger would.
install(FILES fcitx-projectkey.svg fcitx-projectkey-symbolic.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
# The GNOME Shell extension that shows Project Key's windows on GNOME (Sources/GnomeScreen.cpp),
# where Mutter lets no input method place them; it is turned on in GNOME's Extensions app.
install(DIRECTORY gnome-shell/project-key@koukeneko.github.io DESTINATION "${CMAKE_INSTALL_DATADIR}/gnome-shell/extensions")
# The Fcitx5 theme in the candidate window's look, for Fcitx5's own panel (Wayland, other input methods).
install(DIRECTORY theme/ DESTINATION "${FCITX_INSTALL_PKGDATADIR}/themes/projectkey")
install(CODE "execute_process(COMMAND gtk-update-icon-cache -q -t -f \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\" ERROR_QUIET)")
# Fcitx5 finds an addon's library in its own addon directory; installed under another prefix
# (Homebrew's), the conf names it by full path.
if(FCITX_INSTALL_USE_FCITX_SYS_PATHS)
  set(LIBRARY projectkey)
else()
  set(LIBRARY "${FCITX_INSTALL_ADDONDIR}/projectkey")
endif()
configure_file(projectkey-addon.conf.in projectkey-addon.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/projectkey-addon.conf" RENAME projectkey.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES projectkey.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
install(FILES "${MCBOPOMOFO}/Source/Data/data.txt" "${MCBOPOMOFO}/Source/Data/associated-phrases-v2.txt" DESTINATION "${DATA_DIR}")
install(FILES "${MCBOPOMOFO}/LICENSE.txt" RENAME McBopomofo-LICENSE.txt DESTINATION "${DATA_DIR}")
# The emoji, and the license of their annotations (tools/emoji/build-emoji.py).
install(FILES ../../core/data/emoji.txt ../../core/data/EMOJI-NOTICE.txt DESTINATION "${DATA_DIR}")
# From tools/bigram-trainer when it was run: the dictionary with the words McBopomofo
# lacks (it replaces McBopomofo's) and the context ranking models.
# REQUIRE_MODELS stops the build without them, as releases need them; models-version.txt is
# what the settings windows show.
set(TRAINED "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/bigram-trainer/.cache")
option(REQUIRE_MODELS "Fail without the trained models (tools/bigram-trainer/fetch-models.sh)" OFF)
foreach(file data.txt bigram.bin chars.bin models-version.txt)
  if(REQUIRE_MODELS AND NOT EXISTS "${TRAINED}/${file}")
    message(FATAL_ERROR "no ${TRAINED}/${file}: run tools/bigram-trainer/fetch-models.sh")
  endif()
  if(EXISTS "${TRAINED}/${file}")
    install(FILES "${TRAINED}/${file}" DESTINATION "${DATA_DIR}")
  endif()
endforeach()
if(EXISTS "${TRAINED}/sentence.bin")  # optional: tools/lm-trainer
  install(FILES "${TRAINED}/sentence.bin" DESTINATION "${DATA_DIR}")
endif()
# Their licenses and sources go with them.
if(EXISTS "${TRAINED}/data.txt" OR EXISTS "${TRAINED}/bigram.bin")
  install(FILES ../../tools/bigram-trainer/MODELS-NOTICE.txt ../../tools/bigram-trainer/LICENSE-LGPL-2.1.txt DESTINATION "${DATA_DIR}")
endif()
