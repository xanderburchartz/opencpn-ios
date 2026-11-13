# Fase 1, stap 4: wxWidgets (wxQt) minimal-app voor iOS.
# Bouwen: scripts/build-spike.sh wx-minimal [sim|device]
# Verwacht wxWidgets geïnstalleerd door scripts/build-wxqt-ios.sh (wxWidgets_DIR).
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(wxminimal LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets PrintSupport Test)
qt_standard_project_setup()

find_package(wxWidgets 3.3 REQUIRED CONFIG)

qt_add_executable(wxminimal MACOSX_BUNDLE main.cpp)
target_link_libraries(wxminimal PRIVATE wx::wxcore wx::wxbase
  Qt6::Widgets Qt6::OpenGLWidgets Qt6::PrintSupport Qt6::Test)

set(OCPNIOS_BUNDLE_PREFIX "local.opencpnios" CACHE STRING "Voorvoegsel voor bundel-ID's")
set_target_properties(wxminimal PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.wxminimal"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.wxminimal"
  MACOSX_BUNDLE_BUNDLE_NAME "wxMinimal"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)
