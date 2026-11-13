# Fase 1, stap 1: Qt-rooktest voor iOS.
# Bouwen: scripts/build-spike.sh qt-hello [sim|device]
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(qthello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(qthello MACOSX_BUNDLE main.cpp)
target_link_libraries(qthello PRIVATE Qt6::Widgets)

set(OCPNIOS_BUNDLE_PREFIX "local.opencpnios" CACHE STRING "Voorvoegsel voor bundel-ID's")
set_target_properties(qthello PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.qthello"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.qthello"
  MACOSX_BUNDLE_BUNDLE_NAME "QtHello"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)
