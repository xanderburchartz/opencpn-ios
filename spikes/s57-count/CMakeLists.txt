# Fase 2: S-57 ENC inlezen met de OpenCPN-kern op iOS.
# Bouwen: scripts/build-spike.sh s57-count [sim|device]
# Een ENC-cel (.000) uit build/testdata/enc wordt in de bundel gezet (map enc/).
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(s57count LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(wxWidgets 3.3 REQUIRED CONFIG)

get_filename_component(OCPNIOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${OCPNIOS_ROOT}/cmake/ocpn-core.cmake")

# Resources in de bundel: S-57-tabellen en eventuele testcellen.
set(_res)
foreach(f ${OCPN_S57DATA_FILES})
  set_source_files_properties("${f}" PROPERTIES MACOSX_PACKAGE_LOCATION "s57data")
  list(APPEND _res "${f}")
endforeach()
file(GLOB _enc "${OCPNIOS_ROOT}/build/testdata/enc/*.0[0-9][0-9]")
foreach(f ${_enc})
  set_source_files_properties("${f}" PROPERTIES MACOSX_PACKAGE_LOCATION "enc")
  list(APPEND _res "${f}")
endforeach()

qt_add_executable(s57count MACOSX_BUNDLE main.cpp ${_res})
target_link_libraries(s57count PRIVATE
  ocpn::s57-charts ocpn::iso8211 ocpn::gdal ocpn::nmea0183 ocpn::geoprim wx::wxbase Qt6::Core)
target_include_directories(s57count PRIVATE
  "${OCPN_SRC}/libs/s57-charts/include" "${OCPN_SRC}/libs/nmea0183/src")

set(OCPNIOS_BUNDLE_PREFIX "local.opencpnios" CACHE STRING "Voorvoegsel voor bundel-ID's")
set_target_properties(s57count PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.s57count"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.s57count"
  MACOSX_BUNDLE_BUNDLE_NAME "S57Count"
)
