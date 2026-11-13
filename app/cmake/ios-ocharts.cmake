# o-charts_pi (GPL-2.0, github.com/bdbcat/o-charts_pi) statisch in de iOS-app.
# Ingesloten vanuit ios-plugins.cmake (zelfde scope; gebruikt ocpnios_static_plugin).
#
# De plugin is open; de ontsleuteling zit bij o-charts in een gesloten programma
# (oexserverd + libsgl) dat op iOS niet kan draaien. Hier:
#   - de plugin zelf, met patches/o-charts_pi/0001-ios.patch (vervoer naar de
#     ontsleuteling binnen de app, geen winkelverkeer zonder ontsleuteling);
#   - app/ios/ocharts: de interface (ocharts_ios_backend.h) en de lijm aan de plugin-kant;
#   - de implementatie van die interface: OCPNIOS_OCHARTS_BACKEND (een statische
#     bibliotheek of xcframework van o-charts), of anders de stub die "niet beschikbaar"
#     meldt. Zie docs/o-charts-ios.md.
# SPDX-License-Identifier: GPL-2.0-or-later

option(OCPNIOS_WITH_OCHARTS "o-charts_pi inbouwen (stub zonder OCPNIOS_OCHARTS_BACKEND)" ON)
set(OCPNIOS_OCHARTS_BACKEND "" CACHE FILEPATH
    "Ontsleutelingsbibliotheek van o-charts (.a of .xcframework); leeg = stub")

set(OCD "${OCPNIOS_ROOT}/upstream/o-charts_pi")
set(OCIOS "${OCPNIOS_ROOT}/app/ios/ocharts")

if(NOT OCPNIOS_WITH_OCHARTS)
  message(STATUS "o-charts_pi: uitgezet (OCPNIOS_WITH_OCHARTS=OFF)")
elseif(NOT EXISTS "${OCD}/Plugin.cmake")
  message(STATUS "o-charts_pi niet gevonden in ${OCD}; overgeslagen")
elseif(NOT EXISTS "${OCD}/src/o-charts_pi.cpp" OR
       NOT EXISTS "${OCD}/opencpn-libs/README.md")
  message(STATUS "o-charts_pi onvolledig (submodule?); overgeslagen")
else()
  # Zonder de iOS-patch bouwt de plugin wel, maar zoekt hij oexserverd op schijf.
  file(READ "${OCD}/src/o-charts_pi.cpp" _oc_main)
  if(NOT _oc_main MATCHES "OCIOS_SENCUTIL_SENTINEL")
    message(FATAL_ERROR "o-charts_pi: patch ontbreekt; draai scripts/apply-patches.sh o-charts_pi")
  endif()

  # Versie en config.h zoals Plugin.cmake/CMakeLists.txt van de plugin.
  file(STRINGS "${OCD}/VERSION.cmake" _oc_v REGEX "OCPN_VERSION")
  string(REGEX MATCH "\"([0-9.]+)\"" _m "${_oc_v}")
  set(_oc_ver "${CMAKE_MATCH_1}")
  string(REPLACE "." ";" _oc_vl "${_oc_ver}")
  list(GET _oc_vl 0 PROJECT_VERSION_MAJOR)
  list(GET _oc_vl 1 PROJECT_VERSION_MINOR)
  list(GET _oc_vl 2 PROJECT_VERSION_PATCH)
  list(GET _oc_vl 3 PROJECT_VERSION_TWEAK)
  set(API_VERSION "1.17")
  set(PKG_PRERELEASE "")
  set(pkg_vers_build_info "ios")
  set(PKG_NAME o-charts_pi)
  set(PACKAGE_NAME o-charts_pi)
  set(_oc_out "${CMAKE_BINARY_DIR}/ocpnios-plugins/o-charts_pi")
  configure_file("${OCD}/config.h.in" "${_oc_out}/config.h")

  set(_oc_src "")
  foreach(f chart cutil eSENCChart fpr georef InstallDirs ochartShop o-charts_pi OCPNRegion
            oernc_inStream Osenc piScreenLog s57RegistrarMgr uKey validator viewport)
    list(APPEND _oc_src "${OCD}/src/${f}.cpp")
  endforeach()
  list(APPEND _oc_src "${OCD}/src/sha256.c" "${OCD}/libs/gdal/src/s57classregistrar.cpp")
  # tpm/tpmUtil.cpp: alleen Linux (TPM-chip); op iOS leeg, niet nodig.
  # Eigen bibliotheken van de plugin (libs/), zoals Plugin.cmake add_plugin_libraries.
  # tpm2-tss, glew en libusb-compat (dongle) zijn er op iOS niet.
  foreach(l cpl dsa iso8211 geoprim s52plib)
    file(GLOB _ls "${OCD}/libs/${l}/src/*.cpp" "${OCD}/libs/${l}/src/*.c")
    list(APPEND _oc_src ${_ls})
  endforeach()
  # Upstream linkt geoprim als archief; dan wint cutil.cpp en worden line_clip/poly_math
  # niet geladen. Hier alles in één object: die twee dubbele bestanden weglaten.
  list(FILTER _oc_src EXCLUDE REGEX "/libs/geoprim/src/(line_clip|poly_math)\\.cpp$")
  file(GLOB _ls "${OCD}/libs/wxJSON/src/*.cpp" "${OCD}/libs/tinyxml/src/*.cpp"
                "${OCD}/libs/wxcurl/src/*.cpp")
  list(APPEND _oc_src ${_ls} "${OCD}/libs/pugixml/pugixml.cpp")
  # iOS-deel: lijm, vervoer. De stub hoort NIET in de plugin (wordt vervangen door de
  # bibliotheek van o-charts) en gaat via OCPNIOS_PLUGIN_LINK naar de app.
  list(APPEND _oc_src "${OCIOS}/ocharts_ios_glue.cpp" "${OCIOS}/ocharts_ios_pipe.cpp")

  # Bibliotheek voor de ontsleuteling: de echte of de stub.
  if(OCPNIOS_OCHARTS_BACKEND)
    message(STATUS "o-charts_pi: ontsleuteling uit ${OCPNIOS_OCHARTS_BACKEND}")
    set(_oc_backend "${OCPNIOS_OCHARTS_BACKEND}")
  else()
    message(STATUS "o-charts_pi: stub voor de ontsleuteling (niet beschikbaar op iOS)")
    add_library(ocpnios_ocharts_stub STATIC "${OCIOS}/ocharts_backend_stub.cpp")
    target_include_directories(ocpnios_ocharts_stub PRIVATE "${OCIOS}")
    set(_oc_backend ocpnios_ocharts_stub)
  endif()

  # Symboolnaam o_charts_pi (create/destroy); app/CMakeLists.txt maakt die met
  # MAKE_C_IDENTIFIER uit de pluginnaam o-charts_pi.
  ocpnios_static_plugin(o_charts_pi
    SOURCES ${_oc_src}
    # api-17: de plugin-API waartegen o-charts_pi gebouwd is (ook op de desktop); de
    # header van de kern (5.14) mist o.a. ~PI_S57Obj().
    INCLUDES "${OCD}/opencpn-libs/api-17" "${_oc_out}" "${OCIOS}" "${OCD}/src" "${OCD}/libs/gdal/src"
             "${OCD}/libs/cpl/src" "${OCD}/libs/dsa/src" "${OCD}/libs/iso8211/src"
             "${OCD}/libs/geoprim/src" "${OCD}/libs/s52plib/src" "${OCD}/libs/pugixml"
             "${OCD}/libs/wxJSON/include" "${OCD}/libs/tinyxml/include"
             "${OCD}/libs/wxcurl/src" "${OCD}/libs/wxcurl/include"
    DEFINES ocpnUSE_GLSL TIXML_USE_STL __OCPN_USE_CURL__
    LINK CURL::libcurl)
  target_compile_options(_ocpnios_pi_o_charts_pi PRIVATE
    "SHELL:-include ${OCPNIOS_ROOT}/app/ios/gl_shim/ocpnios_prefix.h"
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${OCIOS}/ocharts_ios_prefix.h>")
  set(OCPNIOS_PLUGIN_LINK ${OCPNIOS_PLUGIN_LINK} ${_oc_backend})
  # Data (S-52-aanvullingen, licentie) en vertalingen via de lijst van losse plugins
  # (ios-bundle-data.cmake); gewone naam = GetCommonName() in kleine letters.
  set(OCPNIOS_EXT_PLUGINS ${OCPNIOS_EXT_PLUGINS} "o-charts_pi|${OCD}|o-charts")
  set(OCPNIOS_HAVE_OCHARTS_PI ON)
endif()
