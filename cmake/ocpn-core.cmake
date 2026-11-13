# Bouwt de niet-grafische OpenCPN-bibliotheken uit upstream/OpenCPN voor iOS (fase 2):
#   ocpn::gdal         uitgeklede GDAL/OGR (cpl_*, ogr_*)
#   ocpn::iso8211      ISO 8211-lezer (basisformaat van S-57)
#   ocpn::s57-charts   OGR-S57-driver: S57Reader, OGRS57DataSource, klassenregistrar
#   ocpn::nmea0183     NMEA 0183-ontleding
#   ocpn::geoprim      geografische primitieven (LLRegion, bbox, ...)
#
# Gebruik in een project dat al Qt6 en wxWidgets (wx::wxbase) gevonden heeft:
#   include(${OCPNIOS_ROOT}/cmake/ocpn-core.cmake)
# SPDX-License-Identifier: GPL-2.0-or-later

if(NOT OCPNIOS_ROOT)
  get_filename_component(OCPNIOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
set(OCPN_SRC "${OCPNIOS_ROOT}/upstream/OpenCPN")
if(NOT EXISTS "${OCPN_SRC}/libs/gdal/CMakeLists.txt")
  message(FATAL_ERROR "upstream/OpenCPN ontbreekt; draai scripts/fetch-upstream.sh")
endif()

# De OpenCPN-bibliotheken verwachten deze variabelen (anders roepen ze wx-config aan).
get_target_property(_wx_inc wx::wxbase INTERFACE_INCLUDE_DIRECTORIES)
set(wxWidgets_INCLUDE_DIRS "${_wx_inc}")
set(wxWidgets_LIBRARIES wx::wxbase)
list(APPEND CMAKE_MODULE_PATH "${OCPN_SRC}/cmake")

# iso8211 linkt (PRIVATE) tegen ocpn::model maar gebruikt het niet; leeg doel als stand-in.
if(NOT TARGET ocpn::model)
  add_library(ocpnios_model_stub INTERFACE)
  add_library(ocpn::model ALIAS ocpnios_model_stub)
endif()

set(_b "${CMAKE_BINARY_DIR}/ocpn-core")
add_subdirectory("${OCPN_SRC}/libs/gdal" "${_b}/gdal")
add_subdirectory("${OCPN_SRC}/libs/iso8211" "${_b}/iso8211")
add_subdirectory("${OCPN_SRC}/libs/s57-charts" "${_b}/s57-charts")
add_subdirectory("${OCPN_SRC}/libs/nmea0183" "${_b}/nmea0183")
add_subdirectory("${OCPN_SRC}/libs/gl_headers" "${_b}/gl_headers")
add_subdirectory("${OCPN_SRC}/libs/glu" "${_b}/glu")
add_subdirectory("${OCPN_SRC}/libs/geoprim" "${_b}/geoprim")

# iOS gebruikt dezelfde losse GL-typen en GLU-declaraties als Android, plus OpenCPN's
# eigen GLU-tessellator (libs/glu). Platformsymbool voor iOS-takken in OpenCPN-patches.
target_include_directories(_GL_HEADERS INTERFACE "${OCPN_SRC}/libs/gl_headers/android")
target_link_libraries(GEOPRIM PRIVATE GLU_static Qt6::Gui)  # LLRegion.cpp: <qopengl.h> (OpenCPN 5.14)
foreach(_t GDAL ISO8211 _S57_CHARTS NMEA0183 GEOPRIM GLU_static)
  target_compile_definitions(${_t} PRIVATE __OCPN__IOS__)
endforeach()

# s57registrar_mgr.cpp gebruikt wxBase.
target_link_libraries(_S57_CHARTS wx::wxbase)  # zelfde (oude) signatuur als upstream

# Datatabellen die de S-57-klassenregistrar nodig heeft (worden in de app-bundel gezet).
set(OCPN_S57DATA_FILES
  "${OCPN_SRC}/data/s57data/s57objectclasses.csv"
  "${OCPN_SRC}/data/s57data/s57attributes.csv"
  "${OCPN_SRC}/data/s57data/s57expectedinput.csv"
)
