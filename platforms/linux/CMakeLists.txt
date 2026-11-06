# The Fcitx5 input method: an addon that drives the shared engine
# (core/engine) and shows its candidates in its own X11 window (Sources/Panel.cpp).
# build.sh fetches McBopomofo (MIT) into .build/mcbopomofo and points MCBOPOMOFO here.
cmake_minimum_required(VERSION 3.16)
project(fcitx5-projectkey VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENCHANT REQUIRED IMPORTED_TARGET enchant-2)
pkg_check_modules(PANEL REQUIRED IMPORTED_TARGET xcb cairo cairo-xcb pangocairo)
include(GNUInstallDirs)

set(MCBOPOMOFO "" CACHE PATH "McBopomofo checkout")
set(CORE "${CMAKE_CURRENT_SOURCE_DIR}/../../core/engine")
add_subdirectory("${MCBOPOMOFO}/Source/Engine" mcbopomofo EXCLUDE_FROM_ALL)

# The engine shared with macOS, Windows and iOS.
add_library(engine STATIC "${CORE}/Composer.cpp" "${CORE}/Bigram.cpp" "${CORE}/Controller.cpp")
target_include_directories(engine PUBLIC "${CORE}" "${MCBOPOMOFO}/Source/Engine")
target_link_libraries(engine PUBLIC McBopomofoLMLib MandarinLib gramambular2_lib)

foreach(test ComposerTest ControllerTest)
  add_executable(${test} "${CORE}/Tests/${test}.cpp")
  target_link_libraries(${test} engine)
endforeach()

set(DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/fcitx5/projectkey")
add_library(projectkey MODULE Sources/ProjectKey.cpp Sources/Panel.cpp)
target_link_libraries(projectkey PRIVATE engine Fcitx5::Core Fcitx5::Utils PkgConfig::ENCHANT PkgConfig::PANEL)
target_compile_definitions(projectkey PRIVATE PROJECTKEY_DATA_DIR="${DATA_DIR}" FCITX_GETTEXT_DOMAIN="fcitx5-projectkey")
set_target_properties(projectkey PROPERTIES PREFIX "")

install(TARGETS projectkey DESTINATION "${FCITX_INSTALL_LIBDIR}/fcitx5")
configure_file(projectkey-addon.conf.in projectkey-addon.conf @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/projectkey-addon.conf" RENAME projectkey.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES projectkey.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
install(FILES "${MCBOPOMOFO}/Source/Data/data.txt" DESTINATION "${DATA_DIR}")
install(FILES "${MCBOPOMOFO}/LICENSE.txt" RENAME McBopomofo-LICENSE.txt DESTINATION "${DATA_DIR}")
# From tools/bigram-trainer when it was run: the dictionary with the words McBopomofo
# lacks (it replaces McBopomofo's) and the context ranking model.
set(TRAINED "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/bigram-trainer/.cache")
foreach(file data.txt bigram.bin)
  if(EXISTS "${TRAINED}/${file}")
    install(FILES "${TRAINED}/${file}" DESTINATION "${DATA_DIR}")
  endif()
endforeach()
