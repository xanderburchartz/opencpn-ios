# Ingebouwde OpenCPN-plugins (dashboard, grib, wmm, chartdldr) statisch in de iOS-app.
#
# Upstream bouwt ze als losse dylibs die PluginLoader met dlopen laadt. Op iOS kan dat niet
# zonder wxWidgets twee keer in het geheugen te krijgen (wx zit statisch in de app). Hier:
#  1. per plugin een statische bibliotheek, met create_pi/destroy_pi hernoemd naar
#     <plugin>_create_pi/<plugin>_destroy_pi (via -D);
#  2. die bibliotheek gedeeltelijk gelinkt (clang -r) tot één objectbestand waarin alleen
#     die twee functies zichtbaar blijven. Alle andere symbolen worden lokaal, zodat eigen
#     kopieën in de plugins (dashboard heeft een eigen nmea0183 en wxJSON) niet botsen met
#     die van OpenCPN of van elkaar;
#  3. het objectbestand gaat in de app; app/ios/static_plugins.cpp zet de functies in een
#     lijst die de (gepatchte) PluginLoader op iOS gebruikt in plaats van dlopen/dlsym.
# SPDX-License-Identifier: GPL-2.0-or-later

set(OCPNIOS_PLUGIN_OBJECTS "")

# Gemeenschappelijke include-mappen, zoals het upstream-doel ocpn::opencpn (CMakeLists ~1065).
# Eerst de GL-shims (GL/gl.h, GL/glew.h, GLES2/gl2.h naar Qt's OpenGL ES), dan
# gl_private.h/glu.h van de Android-headers, zoals de kern.
set(_ocpnios_pi_common_includes
  "${OCPNIOS_ROOT}/app/ios/gl_shim"
  "${CMAKE_SOURCE_DIR}/libs/gl_headers/android"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/gui/include"
  "${CMAKE_SOURCE_DIR}/model/include"
  "${CMAKE_SOURCE_DIR}/resources")

# Vlaggen voor de gedeeltelijke link: zelfde architectuur, SDK en minimale versie als de rest.
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
  set(_ocpnios_minflag "-mios-simulator-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
  set(_ocpnios_minflag "-miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# ocpnios_static_plugin(<naam> SOURCES ... [INCLUDES ...] [DEFINES ...] [LINK ...])
function(ocpnios_static_plugin name)
  cmake_parse_arguments(P "" "" "SOURCES;INCLUDES;DEFINES;LINK" ${ARGN})
  set(lib "_ocpnios_pi_${name}")
  add_library(${lib} STATIC ${P_SOURCES})
  # BEFORE: eigen headers van de plugin (bijv. radar_pi's config.h) vóór OpenCPN's
  # globale build/include.
  target_include_directories(${lib} BEFORE PRIVATE ${P_INCLUDES} ${_ocpnios_pi_common_includes})
  target_compile_definitions(${lib} PRIVATE
    create_pi=${name}_create_pi
    destroy_pi=${name}_destroy_pi
    ${P_DEFINES})
  target_compile_options(${lib} PRIVATE
    -Wno-unused -Wno-overloaded-virtual -Wno-format -Wno-unknown-pragmas
    -Wno-inconsistent-missing-override -Wno-potentially-evaluated-expression)
  if(P_LINK)
    # Alleen voor de include-mappen en definities; de bibliotheken zelf linkt de app.
    target_link_libraries(${lib} PRIVATE ${P_LINK})
  endif()

  set(obj "${CMAKE_BINARY_DIR}/ocpnios-plugins/${name}.o")
  add_custom_command(
    OUTPUT "${obj}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ocpnios-plugins"
    COMMAND "${CMAKE_CXX_COMPILER}" -arch arm64 -isysroot "${CMAKE_OSX_SYSROOT}"
            ${_ocpnios_minflag} -r -nostdlib
            "-Wl,-force_load,$<TARGET_FILE:${lib}>"
            "-Wl,-exported_symbol,_${name}_create_pi"
            "-Wl,-exported_symbol,_${name}_destroy_pi"
            -o "${obj}"
    DEPENDS ${lib}
    COMMENT "plugin ${name}: gedeeltelijk linken, alleen create/destroy zichtbaar"
    VERBATIM)
  set_source_files_properties("${obj}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
  set(OCPNIOS_PLUGIN_OBJECTS ${OCPNIOS_PLUGIN_OBJECTS} "${obj}" PARENT_SCOPE)
  if(P_LINK)
    set(OCPNIOS_PLUGIN_LINK ${OCPNIOS_PLUGIN_LINK} ${P_LINK} PARENT_SCOPE)
  endif()
endfunction()

set(PI "${CMAKE_SOURCE_DIR}/plugins")

# version.h uit cmake/version.h.in van de plugin, met de versie uit zijn CMakeLists.
function(_ocpnios_pi_version name out_dir)
  file(STRINGS "${PI}/${name}/CMakeLists.txt" _l REGEX "^set\\(VERSION_")
  foreach(line ${_l})
    if(line MATCHES "set\\((VERSION_[A-Z]+) \"([^\"]*)\"\\)")
      set(${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    endif()
  endforeach()
  set(PROJECT_NAME ${name})
  configure_file("${PI}/${name}/cmake/version.h.in" "${out_dir}/version.h")
  if(EXISTS "${PI}/${name}/cmake/wxWTranslateCatalog.h.in")
    configure_file("${PI}/${name}/cmake/wxWTranslateCatalog.h.in"
                   "${out_dir}/wxWTranslateCatalog.h")
  endif()
endfunction()

# ---------------------------------------------------------------- dashboard_pi
file(GLOB _dash_src "${PI}/dashboard_pi/src/*.cpp" "${PI}/dashboard_pi/src/wxJSON/*.cpp"
                    "${PI}/dashboard_pi/src/nmea0183/*.cpp")
ocpnios_static_plugin(dashboard_pi
  SOURCES ${_dash_src}
  INCLUDES "${PI}/dashboard_pi/include" "${PI}/dashboard_pi/include/wx"
           "${PI}/dashboard_pi/src" "${PI}/dashboard_pi/src/nmea0183"
  LINK ocpn::N2KParser ocpn::manual ocpn::wxjson)

# ---------------------------------------------------------------- wmm_pi
_ocpnios_pi_version(wmm_pi "${CMAKE_BINARY_DIR}/ocpnios-plugins/wmm_pi")
ocpnios_static_plugin(wmm_pi
  SOURCES
    "${PI}/wmm_pi/src/wmm_pi.cpp" "${PI}/wmm_pi/src/MagneticPlotMap.cpp"
    "${PI}/wmm_pi/src/icons.cpp" "${PI}/wmm_pi/src/GeomagnetismLibrary.c"
    "${PI}/wmm_pi/src/WmmUIDialog.cpp" "${PI}/wmm_pi/src/pi_TexFont.cpp"
    "${PI}/wmm_pi/src/pi_ocpndc.cpp" "${PI}/wmm_pi/src/pi_shaders.cpp"
  INCLUDES "${PI}/wmm_pi/src" "${CMAKE_BINARY_DIR}/ocpnios-plugins/wmm_pi"
  LINK ocpn::N2KParser ocpn::manual ocpn::wxjson GLU_static)  # 5.14: wmm_pi gebruikt N2kMsg.h
# pi_ocpndc.cpp gebruikt std::list zonder <list> te includen (gaat elders goed via wx).
set_source_files_properties("${PI}/wmm_pi/src/pi_ocpndc.cpp"
  PROPERTIES COMPILE_OPTIONS "-include;list")

# ---------------------------------------------------------------- grib_pi
add_subdirectory("${PI}/grib_pi/libs/jasper" "${CMAKE_BINARY_DIR}/plugins/jasper")
foreach(f grib_pi GribOverlayFactory GribUIDialogBase GrabberWin GribUIDialog CursorData
          GribSettingsDialog GribRequestDialog XyGribPanel XyGribModelDef email GribTable
          CustomGrid icons GribReader GribRecord GribV1Record GribV2Record zuFile IsoLine
          pi_ocpndc pi_TexFont pi_shaders)
  list(APPEND _grib_src "${PI}/grib_pi/src/${f}.cpp")
endforeach()
ocpnios_static_plugin(grib_pi
  SOURCES ${_grib_src}
  INCLUDES "${PI}/grib_pi/src" "${PI}/grib_pi/src/bzip2"
  DEFINES _WCHAR_H_CPLUSPLUS_98_CONFORMANCE_
  LINK JASPER ocpn::wxjson ocpn::manual)

# ---------------------------------------------------------------- chartdldr_pi
# Uitpakken via libarchive (iOS-SDK; headers uit buildandroid, zie ocpn::libarchive).
_ocpnios_pi_version(chartdldr_pi "${CMAKE_BINARY_DIR}/ocpnios-plugins/chartdldr_pi")
ocpnios_static_plugin(chartdldr_pi
  SOURCES
    "${PI}/chartdldr_pi/src/chartdldr_pi.cpp" "${PI}/chartdldr_pi/src/icons.cpp"
    "${PI}/chartdldr_pi/src/chartdldrgui.cpp" "${PI}/chartdldr_pi/src/chartcatalog.cpp"
  INCLUDES "${PI}/chartdldr_pi/src" "${CMAKE_BINARY_DIR}/ocpnios-plugins/chartdldr_pi"
  DEFINES DLDR_USE_LIBARCHIVE _FILE_OFFSET_BITS=64
  LINK ocpn::libarchive ocpn::manual ocpn::wxjson)

# ---------------------------------------------------------------- radar_pi
# Losse bron (github.com/opencpn-radar-pi/radar_pi, GPL-2), in upstream/radar_pi met
# submodule opencpn-libs (nmea0183, wxJSON). Tekent met oude desktop-OpenGL; op iOS via de
# vertaallaag app/ios/glcompat (zie daar en docs/logboek.md, 9 nov 2026).
set(RPI "${OCPNIOS_ROOT}/upstream/radar_pi")
if(EXISTS "${RPI}/Plugin.cmake" AND EXISTS "${RPI}/opencpn-libs/nmea0183/src")
  set(PROJECT_VERSION_MAJOR 5)
  set(PROJECT_VERSION_MINOR 7)
  set(PROJECT_VERSION_PATCH 2)
  set(PROJECT_VERSION_TWEAK 0)
  set(VERSION "5.7.2")
  set(API_VERSION "1.18")
  set(pkg_semver "5.7.2")
  set(PKG_PRERELEASE "")
  set(pkg_vers_build_info "ios")
  configure_file("${RPI}/config.h.in" "${CMAKE_BINARY_DIR}/ocpnios-plugins/radar_pi/config.h")
  # Bronlijst uit Plugin.cmake (set(SRC ...)), alleen .cpp.
  file(READ "${RPI}/Plugin.cmake" _rpi_cmake)
  string(REGEX MATCH "set\\(SRC([^)]*)\\)" _rpi_block "${_rpi_cmake}")
  string(REGEX MATCHALL "src/[A-Za-z0-9_/]+\\.cpp" _rpi_rel "${_rpi_block}")
  set(_rpi_src "")
  foreach(f ${_rpi_rel})
    list(APPEND _rpi_src "${RPI}/${f}")
  endforeach()
  file(GLOB _rpi_nmea "${RPI}/opencpn-libs/nmea0183/src/*.cpp")
  file(GLOB _rpi_json "${RPI}/opencpn-libs/wxJSON/src/*.cpp")
  ocpnios_static_plugin(radar_pi
    SOURCES ${_rpi_src} ${_rpi_nmea} ${_rpi_json}
            "${OCPNIOS_ROOT}/app/ios/glcompat/glcompat.cpp"
    INCLUDES "${CMAKE_BINARY_DIR}/ocpnios-plugins/radar_pi"
             "${OCPNIOS_ROOT}/app/ios/glcompat"
             "${RPI}/include" "${RPI}/include/emulator" "${RPI}/include/garminhd"
             "${RPI}/include/garminxhd" "${RPI}/include/navico" "${RPI}/include/raymarine"
             "${RPI}/opencpn-libs/nmea0183/src" "${RPI}/opencpn-libs/wxJSON/include"
    DEFINES OCPNIOS_GLCOMPAT_WANT_PFN)
  set(OCPNIOS_HAVE_RADAR_PI ON)
else()
  message(STATUS "radar_pi niet gevonden in ${RPI}; plugin overgeslagen")
endif()

# ---------------------------------------------------------------- losse plugins (algemeen)
# Plugins uit eigen repo's (rgleason e.a., GPL), in upstream/<naam> met submodule
# opencpn-libs. ocpnios_ext_plugin(<naam> LIBS <opencpn-libs-mappen> [COMMON <naam>]
#   [DEFINES ...] [EXTRA_SRC ...] [EXCLUDE <regex>] [GLCOMPAT])
# - versie en namen uit de eigen CMakeLists.txt/Plugin.cmake (set(VERSION_MAJOR ...) e.d.);
# - version.h, wxWTranslateCatalog.h en config.h uit hun .in-bestanden;
# - bronnen: src/**/*.cpp|*.c van de plugin en src/**/*.cpp|*.c van elke opgegeven lib;
# - GLCOMPAT: oude desktop-GL via app/ios/glcompat (geforceerd ingesloten).
# OCPNIOS_EXT_PLUGINS houdt naam;map;gewone naam bij voor data, vertalingen en de lijst.
set(OCPNIOS_EXT_PLUGINS "")

# Fixups voor alle losse plugins (gedeelde opencpn-libs):
# - plugin_dc TexFont.h: m_dx/m_dy alleen voor __ANDROID__, maar gebruikt onder
#   USE_ANDROID_GLES2 (die op iOS aan staat).
set(_ocpnios_common_fixups
  "opencpn-libs/plugin_dc/dc_utils/include/TexFont.h@@#ifdef __ANDROID__@@#if defined(__ANDROID__) || defined(__OCPN__IOS__)")

function(ocpnios_ext_plugin name)
  cmake_parse_arguments(E "GLCOMPAT" "COMMON" "LIBS;DEFINES;EXTRA_SRC;EXCLUDE;INCLUDES;FIXUPS" ${ARGN})
  set(dir "${OCPNIOS_ROOT}/upstream/${name}")
  if(NOT EXISTS "${dir}/CMakeLists.txt")
    message(STATUS "${name} niet gevonden in ${dir}; overgeslagen")
    return()
  endif()
  set(out "${CMAKE_BINARY_DIR}/ocpnios-plugins/${name}")
  file(MAKE_DIRECTORY "${out}")

  # Versievariabelen lezen (beide sjablonen).
  set(_txt "")
  foreach(f CMakeLists.txt Plugin.cmake)
    if(EXISTS "${dir}/${f}")
      file(READ "${dir}/${f}" _t)
      string(APPEND _txt "${_t}\n")
    endif()
  endforeach()
  foreach(v VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_TWEAK VERSION_DATE
            COMMON_NAME SHORT_DESCRIPTION LONG_DESCRIPTION PKG_VERSION PKG_NAME)
    # Eerste letterlijke waarde; verwijzingen als ${PROJECT_VERSION_MAJOR} overslaan.
    string(REGEX MATCHALL "[sS][eE][tT][ \t]*\\([ \t]*${v}[ \t]+\"?[^\"\\)\n]*\"?[ \t]*\\)" _all "${_txt}")
    set(${v} "")
    foreach(_m ${_all})
      if(_m MATCHES "${v}[ \t]+\"?([^\"\\)\n]*)\"?")
        set(_val "${CMAKE_MATCH_1}")
        string(STRIP "${_val}" _val)
        if(NOT _val MATCHES "\\$")
          set(${v} "${_val}")
          break()
        endif()
      endif()
    endforeach()
  endforeach()
  if(PKG_VERSION AND NOT VERSION_MAJOR)
    string(REPLACE "." ";" _pv "${PKG_VERSION}")
    list(LENGTH _pv _n)
    list(GET _pv 0 VERSION_MAJOR)
    if(_n GREATER 1)
      list(GET _pv 1 VERSION_MINOR)
    endif()
    if(_n GREATER 2)
      list(GET _pv 2 VERSION_PATCH)
    endif()
    if(_n GREATER 3)
      list(GET _pv 3 VERSION_TWEAK)
    endif()
  endif()
  foreach(v VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_TWEAK)
    if(NOT ${v})
      set(${v} 0)
    endif()
  endforeach()
  set(PACKAGE_NAME ${name})
  set(PACKAGE ${name})
  set(PROJECT_NAME ${name})
  set(PKG_NAME ${name})
  set(VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
  set(PROJECT_VERSION_MAJOR ${VERSION_MAJOR})
  set(PROJECT_VERSION_MINOR ${VERSION_MINOR})
  set(PROJECT_VERSION_PATCH ${VERSION_PATCH})
  set(PROJECT_VERSION_TWEAK ${VERSION_TWEAK})
  set(OCPN_API_VERSION_MAJOR 1)
  set(OCPN_API_VERSION_MINOR 18)
  set(API_VERSION "1.18")
  set(pkg_semver "${VERSION}")
  set(pkg_vers_build_info "ios")
  set(EXTRA_VERSION_INFO "")
  # Naam, samenvatting en beschrijving uit Plugin.cmake van de plugin (nieuwere
  # shipdriver-sjabloon). Zonder dit was PLUGIN_API_NAME leeg en toonde de
  # pluginlijst WeatherRouting zonder naam en met het puzzelstuk (9 nov 2026).
  unset(PLUGIN_API_NAME)
  unset(PKG_SUMMARY)
  unset(PKG_DESCRIPTION)
  if(EXISTS "${dir}/Plugin.cmake")
    file(READ "${dir}/Plugin.cmake" _pc)
    if(_pc MATCHES "set\\(PLUGIN_API_NAME[ \t]+\"?([^\")\n]+)\"?[ \t]*\\)")
      string(STRIP "${CMAKE_MATCH_1}" PLUGIN_API_NAME)
    endif()
    if(_pc MATCHES "set\\(PKG_SUMMARY[ \t]+\"([^\"]*)\"")
      set(PKG_SUMMARY "${CMAKE_MATCH_1}")
    endif()
    if(_pc MATCHES "set\\(PKG_DESCRIPTION[ \t]+\\[=\\[(.*)\\]=\\]")
      string(STRIP "${CMAKE_MATCH_1}" PKG_DESCRIPTION)
    endif()
  endif()
  foreach(tpl version.h wxWTranslateCatalog.h config.h)
    foreach(cand "${dir}/cmake/in-files/${tpl}.in" "${dir}/cmake/${tpl}.in"
                 "${dir}/${tpl}.in" "${dir}/config-templates/${tpl}.in")
      if(EXISTS "${cand}")
        configure_file("${cand}" "${out}/${tpl}")
        break()
      endif()
    endforeach()
  endforeach()

  file(GLOB_RECURSE _src "${dir}/src/*.cpp" "${dir}/src/*.c")
  set(_inc "${out}" "${dir}/include" "${dir}/src")
  file(GLOB _srcsub LIST_DIRECTORIES true "${dir}/src/*")
  foreach(d ${_srcsub})
    if(IS_DIRECTORY "${d}")
      list(APPEND _inc "${d}")
    endif()
  endforeach()
  foreach(lib ${E_LIBS})
    set(ld "${dir}/opencpn-libs/${lib}")
    file(GLOB_RECURSE _ls "${ld}/*.cpp" "${ld}/*.c")
    list(FILTER _ls EXCLUDE REGEX "/(test|tests|example|examples|test_lib_json|jsontestrunner|fuzz)/")
    list(APPEND _src ${_ls})
    list(APPEND _inc "${ld}")
    file(GLOB_RECURSE _li LIST_DIRECTORIES true "${ld}/*")
    foreach(d ${_li})
      if(IS_DIRECTORY "${d}" AND (d MATCHES "/include$" OR d MATCHES "/src$"))
        list(APPEND _inc "${d}")
      endif()
    endforeach()
  endforeach()
  # priorityq-heap.c wordt door priorityq.c ingesloten (GLU-kopie in plugin_dc).
  list(FILTER _src EXCLUDE REGEX "/priorityq-heap\\.c$")
  if(E_EXCLUDE)
    list(FILTER _src EXCLUDE REGEX "${E_EXCLUDE}")
  endif()
  # Fixups zonder de bron te wijzigen: "relpad@@zoek@@vervang" (relatief aan de plugin- of
  # opencpn-libs-map). Kopie in ${out}/fix; headers vóór de rest in het zoekpad, .cpp/.c
  # vervangen in de bronlijst. Ook voor alle plugins gelijk: _ocpnios_common_fixups.
  set(_fix "${out}/fix")
  file(MAKE_DIRECTORY "${_fix}")
  foreach(fx ${_ocpnios_common_fixups} ${E_FIXUPS})
    string(REPLACE "@@" ";" fx "${fx}")
    list(GET fx 0 _rel)
    list(GET fx 1 _from)
    list(GET fx 2 _to)
    # ";" is in CMake het lijstscheidingsteken; in fixups als %SEMI% geschreven.
    string(REPLACE "%SEMI%" ";" _from "${_from}")
    string(REPLACE "%SEMI%" ";" _to "${_to}")
    set(_orig "${dir}/${_rel}")
    if(NOT EXISTS "${_orig}")
      continue()
    endif()
    get_filename_component(_base "${_rel}" NAME)
    set(_copy "${_fix}/${_base}")
    # Per configure opnieuw vanaf het origineel; meerdere fixups op één bestand stapelen.
    list(FIND _fixed "${_copy}" _seen)
    if(_seen GREATER -1)
      file(READ "${_copy}.tmp" _c)
    else()
      file(READ "${_orig}" _c)
      list(APPEND _fixed "${_copy}")
    endif()
    string(REPLACE "${_from}" "${_to}" _c "${_c}")
    file(WRITE "${_copy}.tmp" "${_c}")
    configure_file("${_copy}.tmp" "${_copy}" COPYONLY)
    if(_rel MATCHES "\\.(cpp|c)$")
      list(TRANSFORM _src REPLACE "^${_orig}$" "${_copy}")
      # De kopie staat elders; de map van het origineel voor relatieve includes erbij.
      get_filename_component(_od "${_orig}" DIRECTORY)
      list(APPEND _inc "${_od}")
    endif()
  endforeach()
  list(PREPEND _inc "${_fix}")
  ocpnios_static_plugin(${name}
    SOURCES ${_src} ${E_EXTRA_SRC}
    INCLUDES ${E_INCLUDES} ${_inc}
    DEFINES ${E_DEFINES})
  target_compile_options(_ocpnios_pi_${name} PRIVATE
    "SHELL:-include ${OCPNIOS_ROOT}/app/ios/gl_shim/ocpnios_prefix.h")
  if(E_GLCOMPAT)
    target_sources(_ocpnios_pi_${name} PRIVATE "${OCPNIOS_ROOT}/app/ios/glcompat/glcompat.cpp")
    target_include_directories(_ocpnios_pi_${name} BEFORE PRIVATE "${OCPNIOS_ROOT}/app/ios/glcompat")
    target_compile_options(_ocpnios_pi_${name} PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${OCPNIOS_ROOT}/app/ios/glcompat/glcompat.h>")
  endif()
  set(_common "${E_COMMON}")
  if(NOT _common)
    set(_common "${COMMON_NAME}")
  endif()
  set(OCPNIOS_PLUGIN_OBJECTS ${OCPNIOS_PLUGIN_OBJECTS} PARENT_SCOPE)
  set(OCPNIOS_EXT_PLUGINS ${OCPNIOS_EXT_PLUGINS} "${name}|${dir}|${_common}" PARENT_SCOPE)
endfunction()

# Volgorde van Xander (9 nov 2026): Watchdog, VDR, Polar, Tactics, WeatherRouting, autopilots.
ocpnios_ext_plugin(watchdog_pi GLCOMPAT
  LIBS tinyxml odapi jsonlib wxJSON plugin_dc nmea0183
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57)
ocpnios_ext_plugin(vdr_pi COMMON vdr
  EXCLUDE "/src/plugingl/"
  INCLUDES "${OCPNIOS_ROOT}/upstream/vdr_pi/libs/std_filesystem/include"
           "${OCPNIOS_ROOT}/upstream/vdr_pi/libs/fast-cpp-csv-parser/fast-cpp-csv-parser")
ocpnios_ext_plugin(polar_pi GLCOMPAT
  LIBS wxJSON plugin_dc nmea0183
  EXCLUDE "/src/Options\\.cpp$"
  FIXUPS
    "src/Polar.cpp@@#ifdef __OCPN__ANDROID__\n    wxString stdPath  = std_path.GetUserDataDir()%SEMI%@@#if defined(__OCPN__ANDROID__) || defined(__OCPN__IOS__)\n    wxString stdPath  = std_path.GetUserDataDir()%SEMI%"
    "src/Polar.cpp@@wind_dir = wxRound( ( wind_dir-5 )/5 )%SEMI%@@wind_dir = ( wind_dir-5 )/5%SEMI%"
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57)
ocpnios_ext_plugin(tactics_pi GLCOMPAT
  LIBS tinyxml wxJSON plugin_dc nmea0183
  EXCLUDE "/src/OLD/"
  FIXUPS
    # Bestandsnamen in de bron in kleine letters, in data/ met hoofdletter; iOS is
    # hoofdlettergevoelig.
    # Modaal venster vanuit het tekenen van de kaart crasht op iOS (Qt-assert in de
    # eventloop, 9 nov 2026). Melding naar het log in plaats van een venster.
    "src/tactics_pi.cpp@@        wxMessageDialog dlg(GetOCPNCanvasWindow(), message, _T(\"tactics_pi message\"), wxOK)%SEMI%\n        dlg.ShowModal()%SEMI%@@        wxLogMessage(message)%SEMI%"
    "src/tactics_pi.cpp@@_T(\"tactics.svg\")@@_T(\"Tactics.svg\")"
    "src/tactics_pi.cpp@@_T(\"tactics_toggled.svg\")@@_T(\"Tactics_toggled.svg\")"
    "src/tactics_pi.cpp@@_T(\"tactics_rollover.svg\")@@_T(\"Tactics_rollover.svg\")"
    "src/performance.cpp@@#ifdef __OCPN__ANDROID__\n    wxString stdPath = std_path.GetUserDataDir()%SEMI%@@#if defined(__OCPN__ANDROID__) || defined(__OCPN__IOS__)\n    wxString stdPath = std_path.GetUserDataDir()%SEMI%"
    # Voorkeuren: wxQt verliest de selectie in de lijst met panelen vóór het tonen, alle
    # velden bleven leeg en grijs (zoals bij Dashboard, iPad 10 nov 2026). Na het tonen
    # de eerste regel kiezen.
    "src/tactics_pi.cpp@@    UpdateTacticsButtonsState()%SEMI%\n    UpdateButtonsState()%SEMI%\n    SetMinSize(wxSize(450, -1))%SEMI%@@    UpdateTacticsButtonsState()%SEMI%\n    UpdateButtonsState()%SEMI%\n    CallAfter([this]() { if (m_pListCtrlTacticss->GetItemCount() > 0 && m_pListCtrlTacticss->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) { m_pListCtrlTacticss->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED)%SEMI% UpdateTacticsButtonsState()%SEMI% UpdateButtonsState()%SEMI% } })%SEMI%\n    SetMinSize(wxSize(450, -1))%SEMI%"
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57)
ocpnios_ext_plugin(weather_routing_pi GLCOMPAT COMMON weatherrouting
  LIBS tinyxml odapi jsonlib plugin_dc pugixml bzip2 libtess2
  # Alleen de bronnen uit de eigen CMakeLists (oude kopieën, Windows-geheugenmonitor eruit).
  # Versie 1.15.45.9 (commit 64ca7af, zoals de catalogus); SailboatTransform zit niet in
  # de eigen bronlijst (Plugin.cmake).
  EXCLUDE "/src/SailboatTransform\\.cpp$"
  FIXUPS
    # Venstermaat zoals Android eigen regelt; anders 100x100 (minimum) op iOS.
    "src/WeatherRouting.cpp@@  SetSize(p.x, p.y, m_size.x, m_size.y)%SEMI%@@#ifdef __OCPN__IOS__\n  {\n    wxSize sz = ::wxGetDisplaySize()%SEMI%\n    m_size.x = sz.x - 20%SEMI%\n    m_size.y = sz.y / 2%SEMI%\n    p.x = 10%SEMI%\n    p.y = sz.y / 4%SEMI%\n  }\n#endif\n  SetSize(p.x, p.y, m_size.x, m_size.y)%SEMI%"
    # Inklapbaar paneel kreeg onder wxQt geen hoogte: Posities en Routings bleven een streep
    # (iPhone/iPad, 11 nov 2026). Zoals Android uitzetten.
    "src/WeatherRouting.cpp@@#ifdef __OCPN__ANDROID__\n  m_disable_colpane = true%SEMI%@@#if defined(__OCPN__ANDROID__) || defined(__OCPN__IOS__)\n  m_disable_colpane = true%SEMI%"
    # Lijsten onder elkaar; naast elkaar te smal op de iPhone.
    "src/WeatherRoutingUI.cpp@@  m_splitter1->SplitVertically(m_panel11, m_panel12, -1)%SEMI%@@#ifdef __OCPN__IOS__\n  m_splitter1->SetSashGravity(0.35)%SEMI%\n  m_splitter1->SetMinimumPaneSize(80)%SEMI%\n  m_splitter1->SplitHorizontally(m_panel11, m_panel12, -1)%SEMI%\n  m_splitter1->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { e.Skip()%SEMI% int h = e.GetSize().y%SEMI% if (h > 200 && m_splitter1->GetSashPosition() < h * 3 / 10) m_splitter1->SetSashPosition(h * 2 / 5)%SEMI% })%SEMI%\n#else\n  m_splitter1->SplitVertically(m_panel11, m_panel12, -1)%SEMI%\n#endif"
    # Voorbeeldconfiguraties bevatten Windows-paden (C:\\ProgramData\\...\\boats\\Boat.xml);
    # de plugin plakte het hele pad achter de eigen bootmap: "Vaartuigbestand bestaat niet".
    # Alleen de bestandsnaam gebruiken (11 nov 2026).
    "src/WeatherRouting.cpp@@              wxFileName::GetPathSeparator() + configuration.boatFileName%SEMI%@@              wxFileName::GetPathSeparator() +\n              wxFileName(configuration.boatFileName, wxPATH_WIN).GetFullName()%SEMI%"
    # Routes en isochronen werden alleen getekend zolang het WeatherRouting-venster open
    # stond; op iOS bedekt dat venster de kaart, dus na "Gereed" was er niets te zien
    # (11 nov 2026). iOS: na het eerste openen blijft de laag aan; de wereldbol opent/sluit
    # alleen het venster. Verbergen per routing met "Zichtbaar".
    "src/weather_routing_pi.cpp@@void weather_routing_pi::OnToolbarToolCallback(int id) {@@bool g_ocpnios_wr_overlay = false%SEMI%\nvoid weather_routing_pi::OnToolbarToolCallback(int id) {"
    "src/weather_routing_pi.cpp@@  m_pWeather_Routing->Show(!m_pWeather_Routing->IsShown())%SEMI%@@#ifdef __OCPN__IOS__\n  g_ocpnios_wr_overlay = true%SEMI%\n#endif\n  m_pWeather_Routing->Show(!m_pWeather_Routing->IsShown())%SEMI%"
    "src/weather_routing_pi.cpp@@  if (m_pWeather_Routing && m_pWeather_Routing->IsShown()) {@@  if (m_pWeather_Routing && (m_pWeather_Routing->IsShown() || g_ocpnios_wr_overlay)) {"
    # Tekenen: overlay-projectie zetten zoals radar_pi en de vertex-buffers van OpenCPN
    # losmaken; piDC tekent met client-arrays, met een gebonden VBO werd niets zichtbaar.
    "src/weather_routing_pi.cpp@@    piDC dc%SEMI%\n    dc.SetVP(vp)%SEMI%@@    ocpnios_glcompat_begin_overlay()%SEMI%\n    glBindBuffer(GL_ARRAY_BUFFER, 0)%SEMI%\n    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)%SEMI%\n    piDC dc%SEMI%\n    dc.SetVP(vp)%SEMI%"
    # Importvraag kwam terug als de app werd beëindigd voor de config bewaard was: direct
    # wegschrijven na OK.
    "src/WeatherRouting.cpp@@                   PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR)%SEMI%@@                   PLUGIN_VERSION_MAJOR * 100 + PLUGIN_VERSION_MINOR)%SEMI%\n      pConf->Flush()%SEMI%"
    # Boot-XML: contourbestanden kregen het hele absolute polarpad als naam; in de iOS-
    # container is dat langer dan 255 tekens ("File name too long"), dus contouren werden
    # nooit bewaard en elke keer opnieuw berekend. Ook polarpaden van een vorige container
    # werden niet meer gevonden. Naam en zoekpad nu op het pad onder polars/ (12 nov 2026).
    "src/Boat.cpp@@#include \"Boat.h\"@@#include \"Boat.h\"\n\nstatic wxString ocpnios_polar_rel(const wxString& f) {\n  std::string s(f.ToUTF8())%SEMI%\n  size_t k = s.rfind(\"/polars/\")%SEMI%\n  if (k != std::string::npos) return wxString::FromUTF8(s.substr(k + 8).c_str())%SEMI%\n  return f%SEMI%\n}\nstatic wxString ocpnios_contour_name(const wxString& f) {\n  wxString n = ocpnios_polar_rel(f)%SEMI%\n  n.Replace(wxFileName::GetPathSeparator(), _T(\"_\"))%SEMI%\n  return n%SEMI%\n}"
    "src/Boat.cpp@@                         wxFileName::GetPathSeparator() + polar.FileName%SEMI%@@                         wxFileName::GetPathSeparator() + polar.FileName%SEMI%\n      if (!wxFileName::FileExists(polar.FileName))\n        polar.FileName = weather_routing_pi::StandardPath() + _T(\"polars\") +\n                         wxFileName::GetPathSeparator() +\n                         ocpnios_polar_rel(polar.FileName)%SEMI%"
    "src/Boat.cpp@@        wxFile contours(str)%SEMI%@@        wxFile contours%SEMI%\n        if (wxFileName::FileExists(wxString::FromUTF8(str)))\n          contours.Open(wxString::FromUTF8(str))%SEMI%\n        else\n          generateContours = true%SEMI%"
    "src/Boat.cpp@@        wxFile standaloneContours(standaloneStr)%SEMI%@@        wxFile standaloneContours%SEMI%\n        if (wxFileName::FileExists(wxString::FromUTF8(standaloneStr)))\n          standaloneContours.Open(wxString::FromUTF8(standaloneStr))%SEMI%\n        else\n          generateContours = true%SEMI%"
    "src/Boat.cpp@@      wxString ContoursFileName = polar.FileName%SEMI%@@      wxString ContoursFileName = ocpnios_contour_name(polar.FileName)%SEMI%"
    "src/Boat.cpp@@      wxString StandaloneFileName = polar.FileName%SEMI%@@      wxString StandaloneFileName = ocpnios_contour_name(polar.FileName)%SEMI%"
    # Upstream-telfout: 7 rijen x 2 kolommen, maar 16 items; wx 3.3 gaf bij elke start
    # "too many items (15 > 2*7)". Rijen automatisch (12 nov 2026).
    "src/WeatherRoutingUI.cpp@@new wxFlexGridSizer(7, 2, 3, 10)@@new wxFlexGridSizer(0, 2, 3, 10)"
  # _() met samengestelde tekst; wx 3.3 eist anders letterlijke teksten.
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57 wxNO_REQUIRE_LITERAL_MSGIDS)

# Autopilot-plugins (Xander, 9 nov 2026: zenden mag, zie CLAUDE.md). Testen alleen tegen een
# testbron; aan boord pas met Xander erbij.
ocpnios_ext_plugin(raymarine_autopilot_pi GLCOMPAT
  LIBS tinyxml wxJSON plugin_dc jsonlib nmea0183 n2kparser
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57
  # Knoppen met vaste breedte ("-10", "+10", "Set") toonden hun tekst maar half: de
  # iOS-stijl (laag 1) geeft knoppen opvulling en een rand. Daar de eigen maat
  # (tekst + stijl) laten bepalen: wxDefaultSize en geen vaste minimummaat.
  FIXUPS
    "src/autopilotgui.cpp@@Skalefaktor * FromDIP(wxSize(30, 30))@@wxDefaultSize"
    "src/autopilotgui.cpp@@Skalefaktor * FromDIP(wxSize(34, 34))@@wxDefaultSize"
    "src/autopilotgui.cpp@@Skalefaktor * FromDIP(wxSize(38, 18)), 0)@@wxDefaultSize, 0)"
    "src/autopilotgui.cpp@@buttonSet->SetMinSize(Skalefaktor * FromDIP(wxSize(38, 18)))%SEMI%@@/* opencpn-ios: geen vaste minimummaat */"
    # Waardekeuze naast "Set" toonde alleen "5" half.
    "src/autopilotgui.cpp@@Skalefaktor * FromDIP(wxSize(28, 18))@@wxDefaultSize"
    "src/autopilotgui.cpp@@ParameterValue->SetMinSize(FromDIP(wxSize(Skalefaktor * 28, -1)))%SEMI%@@/* opencpn-ios: geen vaste minimummaat */"
    # Letters van 8 pt en knoppen van ~28 pt hoog waren op de iPad te klein om te lezen en
    # te raken (iOS: minstens 44 pt). Alles in dit venster 1,8 keer zo groot (10 nov 2026).
    "src/autopilotgui.cpp@@: wxDialog(parent, id, title, pos, size, style, name)\n{@@: wxDialog(parent, id, title, pos, size, style, name)\n{\n\tSkalefaktor *= 1.8%SEMI% /* opencpn-ios */")
ocpnios_ext_plugin(pypilot_pi GLCOMPAT
  LIBS tinyxml jsoncpp wxJSON plugin_dc
  EXCLUDE "/src/CalibrationPlot-sean\\.cpp$"
  # wxServDisc (Bonjour-zoeken) zit in de OpenCPN-kern (libs/wxservdisc).
  INCLUDES "${CMAKE_SOURCE_DIR}/libs/wxservdisc"
  FIXUPS
    # wx 3.3: wxTimerEvent zonder timer bestaat niet meer.
    "src/pypilotDialog.cpp@@wxTimerEvent e%SEMI%@@wxTimerEvent e(m_ManualTimer)%SEMI%"
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57)
ocpnios_ext_plugin(autopilot_route_pi GLCOMPAT
  LIBS tinyxml jsoncpp wxJSON plugin_dc nmea0183
  DEFINES TIXML_USE_STL PLUGIN_USE_SVG USE_S57)
# Eigen Navico-autopilotplugin (B&G/Simrad NAC via Simnet-PGN's over de N2K-verbinding van
# OpenCPN; aparte repo, opgehaald met fetch-upstream.sh). Alleen de plugin-API en wx.
# COMMON is nodig: zonder gewone naam geldt een ingebouwde plugin als wees zonder catalogus
# en verdwijnt hij uit de pluginlijst (iPhone, 10 nov 2026). Waarde = GetCommonName() in
# kleine letters (PLUGIN_API_NAME "NavicoAutopilot").
ocpnios_ext_plugin(navico_autopilot_pi COMMON navicoautopilot
  DEFINES PLUGIN_USE_SVG)

# o-charts_pi met stub voor de ontsleuteling (eigen bestand, zie daar).
include("${OCPNIOS_ROOT}/app/cmake/ios-ocharts.cmake")
