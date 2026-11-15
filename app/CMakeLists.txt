# OpenCPN voor iOS (fase 3): eigen top-level CMake.
#
# Dit bestand wordt door scripts/build-ocpn-ios.sh in een symlinkmap gezet die verder naar
# upstream/OpenCPN wijst. Daardoor is CMAKE_SOURCE_DIR de OpenCPN-bron en kloppen de
# ${CMAKE_SOURCE_DIR}-paden in de OpenCPN-bibliotheken, zonder de upstream-CMake te patchen.
# Volgt de Android-opzet (wxQt, OpenGL ES 2), maar zonder Android-code (androidUTIL/JNI).
# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(opencpn_ios LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CMakeDependentOption)  # nodig voor libs/o_sound
find_package(PkgConfig)        # model/CMakeLists gebruikt pkg_search_module
# Overgenomen uit upstream CMakeLists.txt (gebruikt door model/CMakeLists.txt).
set(OCPN_USE_BUNDLED_LIBS "ON")
macro (USE_BUNDLED_LIB _result library)
  if ("${OCPN_USE_BUNDLED_LIBS}" STREQUAL "")
    set(OCPN_USE_BUNDLED_LIBS "ON")
  endif ()
  if (${OCPN_USE_BUNDLED_LIBS} STREQUAL "OFF")
    set(${_result} "OFF")
  elseif (${OCPN_USE_BUNDLED_LIBS} STREQUAL "ON")
    set(${_result} "ON")
  elseif (",${OCPN_USE_BUNDLED_LIBS}," MATCHES ",${library},")
    set(${_result} "ON")
  else ()
    set(${_result} "OFF")
  endif ()
endmacro ()


if(NOT OCPNIOS_ROOT)
  message(FATAL_ERROR "OCPNIOS_ROOT niet gezet (gebruik scripts/build-ocpn-ios.sh)")
endif()
if(NOT OCPNIOS_DEPS)
  message(FATAL_ERROR "OCPNIOS_DEPS niet gezet (map met libcurl, zie build-deps-ios.sh)")
endif()

include("${CMAKE_SOURCE_DIR}/VERSION.cmake")
set(PACKAGE_NAME opencpn)
set(PACKAGE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# ---------------------------------------------------------------------------
# Qt en wxWidgets
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets PrintSupport Test)
qt_standard_project_setup()
# OpenCPN gebruikt geen Qt-moc/uic; AUTOUIC zou "ui_utils.h" (libs/gui) aanzien voor een .ui.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
find_package(wxWidgets 3.3 REQUIRED CONFIG)
get_target_property(_wx_inc wx::wxbase INTERFACE_INCLUDE_DIRECTORIES)
set(wxWidgets_INCLUDE_DIRS "${_wx_inc}")
set(wxWidgets_LIBRARIES
  wx::wxcore wx::wxadv wx::wxaui wx::wxgl wx::wxhtml wx::wxxml wx::wxnet wx::wxqa wx::wxbase)
set(wxWidgets_VERSION_STRING "3.3.4")
# libs/texcmp doet include(${wxWidgets_USE_FILE}); doelen regelen dat hier al.
set(wxWidgets_USE_FILE "${OCPNIOS_ROOT}/app/cmake/empty.cmake")

# ---------------------------------------------------------------------------
# Keuzes voor iOS (zie docs/logboek.md, fase 3)
set(OCPN_USE_CURL ON)
set(OCPN_USE_EXTERN_CURL ON)
set(OCPN_USE_GARMINHOST ON)   # upstream gaat ervan uit dat dit aan staat (comm_n0183_output.cpp)
set(USE_GARMINHOST 1)         # -> config.h
set(OCPN_USE_NEWSERIAL OFF)
set(OCPN_USE_LZMA OFF)
set(OCPN_USE_GL ON)
set(ocpnUSE_GLES ON)
set(OPENGL_FOUND ON)
set(OPENGLES_FOUND ON)
set(USE_GLES2 ON)
set(USE_TLS OFF CACHE BOOL "IXWebSocket zonder TLS (geen OpenSSL op iOS)" FORCE)
# Geen OpenSSL voor iOS; voorkomt dat CMake die van Homebrew (macOS) vindt.
set(CMAKE_DISABLE_FIND_PACKAGE_OpenSSL TRUE)
set(HAVE_UNISTD_H ON)
set(HAVE_SYS_STAT_H ON)
set(HAVE_SYS_TYPES_H ON)
set(HAVE_DIRENT_H ON)
set(HAVE_LIBGEN_H ON)
set(HAVE_READLINK ON)
set(PKG_TARGET "ios")
set(PKG_TARGET_VERSION "18")

add_compile_definitions(
  __OCPN__IOS__
  ocpnUSE_GL
  ocpnUSE_GLES
  USE_ANDROID_GLES2   # naam uit OpenCPN: het GLES2-shaderpad, niet Android-specifiek
  USE_GLSL
  USE_GLU_TESS
  HAVE_WX_GESTURE_EVENTS
  ocpnUSE_SVG
  ocpnUSE_wxBitmapBundle
  wxUSE_WEBVIEW=0
  GLES_SILENCE_DEPRECATION
  PREFIX=\"\"
)
add_compile_options(-Wno-deprecated-declarations -Wno-unused-command-line-argument)

configure_file("${CMAKE_SOURCE_DIR}/cmake/in-files/config.h.in"
               "${CMAKE_BINARY_DIR}/include/config.h")
include_directories(BEFORE "${CMAKE_BINARY_DIR}/include")

# curl (met OpenSSL voor https) uit build/<doel>/deps-install
set(CURL_INCLUDE_DIR "${OCPNIOS_DEPS}/include")
set(CURL_LIBRARY "${OCPNIOS_DEPS}/lib/libcurl.a")
find_package(CURL REQUIRED)

# ---------------------------------------------------------------------------
# Stand-ins voor wat iOS niet heeft of wat we (nog) niet bouwen
# libarchive: de SDK heeft libarchive.tbd maar geen headers; OpenCPN levert ze voor Android.
add_library(_ocpnios_libarchive INTERFACE)
target_include_directories(_ocpnios_libarchive INTERFACE
  "${CMAKE_SOURCE_DIR}/buildandroid/libarchive/include")
target_link_libraries(_ocpnios_libarchive INTERFACE archive)
add_library(ocpn::libarchive ALIAS _ocpnios_libarchive)

# De upstream-CMake zet wx-includes en -definities globaal; hier via link_libraries, zodat
# alle doelen hieronder de usage requirements van wx (en daarmee Qt) krijgen.
link_libraries(${wxWidgets_LIBRARIES})

# ---------------------------------------------------------------------------
# OpenCPN master (na 5.14): nlohmann-json; de bron komt via FETCHCONTENT_SOURCE_DIR_JSON.
if(EXISTS "${CMAKE_SOURCE_DIR}/libs/nlohmann-json/CMakeLists.txt")
  add_subdirectory("${CMAKE_SOURCE_DIR}/libs/nlohmann-json"
                   "${CMAKE_BINARY_DIR}/libs/nlohmann-json")
endif()
# OpenCPN-bibliotheken (zelfde volgorde als de upstream-CMake waar het kan)
foreach(_lib
    gl_headers glu ssl_sha1 SQLiteCpp shapelib ShapefileCpp std_filesystem libtess2
    tinyxml wxservdisc manual s57-charts gui garmin texcmp mipmap lz4 IXWebSocket gdal geoprim
    iso8211 mdns mongoose N2KParser nmea0183 observable picosha2 pugixml rapidjson
    s52plib sqlite wxJSON)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libs/${_lib}" "${CMAKE_BINARY_DIR}/libs/${_lib}")
endforeach()
target_include_directories(_GL_HEADERS INTERFACE "${CMAKE_SOURCE_DIR}/libs/gl_headers/android")

set(SOUND_WX_INCLUDE_DIR "${wxWidgets_INCLUDE_DIRS}" CACHE STRING "" FORCE)
set(SOUND_WX_LIBS "${wxWidgets_LIBRARIES}" CACHE STRING "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/libs/o_sound" "${CMAKE_BINARY_DIR}/libs/o_sound")

add_subdirectory("${CMAKE_SOURCE_DIR}/model" "${CMAKE_BINARY_DIR}/model")
# OpenSSL ontbreekt op iOS: certificates.cpp (REST-server-certificaten) eruit, stub erin.
get_target_property(_model_srcs _model_src SOURCES)
list(FILTER _model_srcs EXCLUDE REGEX "certificates\\.cpp$")
set_property(TARGET _model_src PROPERTY SOURCES ${_model_srcs})
target_sources(_model_src PRIVATE "${OCPNIOS_ROOT}/app/ios/certificates_stub.cpp")
# Ingebouwde GPS: de Android-driver werkt ook op iOS (sensoren: app/ios/internal_gps.mm).
target_sources(_model_src PRIVATE "${CMAKE_SOURCE_DIR}/model/src/comm_drv_n0183_android_int.cpp")
# libs/serial: alleen de kern (serial.cc, impl/unix.cc). De upstream-CMake voegt voor APPLE
# list_ports_osx.cc toe, dat IOKit vraagt; dat heeft iOS niet. Poorten opsommen gebeurt op
# iOS in ser_ports.cpp (lege lijst). Een iPad heeft geen seriële poorten; dit is om te linken.
add_library(_ocpnios_serial STATIC
  "${CMAKE_SOURCE_DIR}/libs/serial/src/serial.cc"
  "${CMAKE_SOURCE_DIR}/libs/serial/src/impl/unix.cc")
target_include_directories(_ocpnios_serial PUBLIC "${CMAKE_SOURCE_DIR}/libs/serial/include")
target_link_libraries(_OCPN_MODEL INTERFACE _ocpnios_serial)
# libs/mongoose zet MG_ENABLE_OPENSSL altijd; zonder OpenSSL weghalen (dan lege TLS-stubs).
get_target_property(_v MONGOOSE COMPILE_DEFINITIONS)
list(REMOVE_ITEM _v MG_ENABLE_OPENSSL)
set_property(TARGET MONGOOSE PROPERTY COMPILE_DEFINITIONS "${_v}")
# model/ en libs/mongoose koppelen op APPLE vast aan de macOS-OpenSSL in /usr/local/opt.
# Die bestaat niet voor iOS; weghalen (mongoose valt dan terug op MG_ENABLE_OPENSSL 0).
foreach(_t _OCPN_MODEL MONGOOSE)
  foreach(_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES INCLUDE_DIRECTORIES
                INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_v ${_t} ${_prop})
    if(_v)
      list(FILTER _v EXCLUDE REGEX "openssl")
      set_property(TARGET ${_t} PROPERTY ${_prop} "${_v}")
    endif()
  endforeach()
endforeach()

# ---------------------------------------------------------------------------
# GUI-bronnen: lijst uit de upstream-CMake (regels 895-1050) plus GL, zonder Android,
# Linux-crashprint en ocpn_fontdlg (upstream: niet op APPLE).
set(GUI_SRC_DIR "${CMAKE_SOURCE_DIR}/gui/src")
file(GLOB GUI_SRC "${GUI_SRC_DIR}/*.cpp")
list(APPEND GUI_SRC
  "${GUI_SRC_DIR}/mbtiles/mbtiles.cpp"
  "${GUI_SRC_DIR}/mbtiles/tile_thread.cpp"
  "${GUI_SRC_DIR}/mbtiles/tile_cache.cpp")
# Niet in upstream-lijst (s57_ocpn_utils), alleen Windows (AdapterInfo), alleen Linux
# (crashprint), niet op APPLE (ocpn_fontdlg). De eerste-start-wizard (initwiz, vraagt
# libdnet) laten we net als Android weg.
list(FILTER GUI_SRC EXCLUDE REGEX "/(crashprint|adapter_info|ocpn_fontdlg|s57_ocpn_utils)\\.cpp$")

# Ingebouwde plugins (dashboard, grib, wmm, chartdldr): statisch, zie ios-plugins.cmake.
include("${OCPNIOS_ROOT}/app/cmake/ios-plugins.cmake")

qt_add_executable(opencpn MACOSX_BUNDLE ${GUI_SRC}
  ${OCPNIOS_PLUGIN_OBJECTS}
  "${OCPNIOS_ROOT}/app/ios/static_plugins.cpp"
  "${OCPNIOS_ROOT}/app/ios/a11y_workaround.cpp"
  "${OCPNIOS_ROOT}/app/ios/assert_log.cpp"
  "${OCPNIOS_ROOT}/app/ios/window_fit.cpp"
  "${OCPNIOS_ROOT}/app/ios/fit_width.cpp"
  "${OCPNIOS_ROOT}/app/ios/nav_list.cpp"
  "${OCPNIOS_ROOT}/app/ios/pinch_zoom.cpp"
  "${OCPNIOS_ROOT}/app/ios/share_sheet.mm"
  "${OCPNIOS_ROOT}/app/ios/color_scheme.mm"
  "${OCPNIOS_ROOT}/app/ios/internal_gps.mm"
  "${OCPNIOS_ROOT}/app/ios/ios_sound.mm"
  "${OCPNIOS_ROOT}/app/ios/startup_busy.mm"
  "${OCPNIOS_ROOT}/app/ios/app_window.mm"
  "${OCPNIOS_ROOT}/app/ios/ios_look.mm"
  "${OCPNIOS_ROOT}/app/ios/touch_ui.cpp"
  "${OCPNIOS_ROOT}/app/ios/startup_diag.cpp"
  "${OCPNIOS_ROOT}/app/ios/gl_share.cpp"
  "${OCPNIOS_ROOT}/app/ios/app_active.cpp"
  "${OCPNIOS_ROOT}/app/ios/screen_capture.mm"
  "${OCPNIOS_ROOT}/app/ios/source_scan.cpp"
  "${OCPNIOS_ROOT}/app/ios/action_sheet.mm"
  "${OCPNIOS_ROOT}/app/ios/file_picker.mm"
  "${OCPNIOS_ROOT}/app/ios/display_path.cpp"
  "${OCPNIOS_ROOT}/app/ios/options_ios.cpp"
  "${OCPNIOS_ROOT}/app/ios/frame_ios.cpp"
  "${OCPNIOS_ROOT}/app/ios/dialogs_ios.cpp"
  "${OCPNIOS_ROOT}/app/ios/plugins_ios.cpp"
  "${OCPNIOS_ROOT}/app/ios/canvas_ios.cpp"
  "${OCPNIOS_ROOT}/app/ios/connections_ios.cpp")
# OpenCPN master (na 5.14) heeft API's hernoemd; app/ios kiest met deze macro.
if(EXISTS "${CMAKE_SOURCE_DIR}/libs/nlohmann-json/CMakeLists.txt")
  target_compile_definitions(opencpn PRIVATE OCPNIOS_OCPN_MASTER=1)
endif()
target_include_directories(opencpn PRIVATE
  "${OCPNIOS_ROOT}/app/ios"
  "${CMAKE_SOURCE_DIR}/gui/include/gui"
  "${CMAKE_SOURCE_DIR}/gui/include"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/resources"
  "${CMAKE_SOURCE_DIR}/gui/src/mbtiles"
  "${CMAKE_SOURCE_DIR}/gui/src/s57/include")

# Lijst van ingebouwde plugins voor static_plugins.cpp en de pluginlader.
set(_inc_txt "// Gemaakt door app/CMakeLists.txt; niet met de hand wijzigen.\n")
foreach(e "dashboard_pi|dashboard" "grib_pi|grib" "wmm_pi|wmm"
          "chartdldr_pi|chartdownloader")
  string(REPLACE "|" ";" e "${e}")
  list(GET e 0 _n)
  list(GET e 1 _c)
  string(APPEND _inc_txt "OCPNIOS_PLUGIN(${_n}, \"lib${_n}.dylib\", \"${_c}\")\n")
endforeach()
if(OCPNIOS_HAVE_RADAR_PI)
  string(APPEND _inc_txt "OCPNIOS_PLUGIN(radar_pi, \"libradar_pi.dylib\", \"radar\")\n")
endif()
foreach(e ${OCPNIOS_EXT_PLUGINS})
  string(REPLACE "|" ";" e "${e}")
  list(GET e 0 _n)
  list(GET e 2 _c)
  string(TOLOWER "${_c}" _c)
  # Symboolnaam zonder koppelteken (o-charts_pi -> o_charts_pi), bestandsnaam als op de Mac.
  string(MAKE_C_IDENTIFIER "${_n}" _sym)
  string(APPEND _inc_txt "OCPNIOS_PLUGIN(${_sym}, \"lib${_n}.dylib\", \"${_c}\")\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/include/ocpnios_static_plugins.inc.tmp" "${_inc_txt}")
configure_file("${CMAKE_BINARY_DIR}/include/ocpnios_static_plugins.inc.tmp"
               "${CMAKE_BINARY_DIR}/include/ocpnios_static_plugins.inc" COPYONLY)

target_link_libraries(opencpn PRIVATE
  ocpn::model-src ocpn::model
  ocpn::s57-charts ocpn::guilib ocpn::texcmp ocpn::mipmap ocpn::lz4 ocpn::tess2
  ocpn::tinyxml ocpn::sqlite_cpp ocpn::shapefile_cpp ocpn::manual ocpn::gl-headers
  ssl::sha1 GLU_static
  ${OCPNIOS_PLUGIN_LINK}
  ${wxWidgets_LIBRARIES}
  CURL::libcurl
  "${OCPNIOS_DEPS}/lib/libssl.a" "${OCPNIOS_DEPS}/lib/libcrypto.a"
  "-framework Security" "-framework CoreFoundation" "-framework CoreServices"
  Qt6::Widgets Qt6::OpenGLWidgets Qt6::PrintSupport Qt6::Test
  "-framework OpenGLES" "-framework QuartzCore"
  "-framework CoreLocation" "-framework CoreMotion" "-framework AVFoundation"
  "-framework UniformTypeIdentifiers" z bz2 iconv)

include("${OCPNIOS_ROOT}/app/cmake/ios-bundle-data.cmake")
ocpnios_bundle_data(opencpn)
ocpnios_bundle_translations(opencpn)

set(OCPNIOS_BUNDLE_PREFIX "local.opencpnios" CACHE STRING "Voorvoegsel voor bundel-ID's")
set_target_properties(opencpn PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.opencpn"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OCPNIOS_BUNDLE_PREFIX}.opencpn"
  MACOSX_BUNDLE_BUNDLE_NAME "OpenCPN"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")

if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
  set(OCPNIOS_ICON_PLATFORM iphonesimulator)
else()
  set(OCPNIOS_ICON_PLATFORM iphoneos)
endif()

# Qt's standaard-Info.plist zet CFBundleDisplayName op "{PRODUCT_NAME}", wat alleen
# Xcode invult. Met Ninja blijft dat letterlijk staan (zichtbaar in de terugknop van
# iOS). Na het bouwen vervangen, en de vraag om lokaal netwerk (NMEA/AIS) toevoegen.
add_custom_command(TARGET opencpn POST_BUILD
  COMMAND /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName OpenCPN"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Delete :NSLocalNetworkUsageDescription"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c
          "Add :NSLocalNetworkUsageDescription string 'OpenCPN leest NMEA- en AIS-gegevens van instrumenten op het lokale netwerk.'"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  # Documents-map van de app zichtbaar in Finder (iPhone aangesloten) en in de
  # Bestanden-app, zodat kaarten (ENC, CM93) zonder devicectl erin kunnen.
  COMMAND /usr/libexec/PlistBuddy -c "Delete :UIFileSharingEnabled"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIFileSharingEnabled bool true"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Delete :LSSupportsOpeningDocumentsInPlace"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c "Add :LSSupportsOpeningDocumentsInPlace bool true"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  # Ingebouwde GPS (app/ios/internal_gps.mm): uitleg bij de toestemming, en doorlopen op de
  # achtergrond zolang die verbinding aan staat.
  COMMAND /usr/libexec/PlistBuddy -c "Delete :NSLocationWhenInUseUsageDescription"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c
          "Add :NSLocationWhenInUseUsageDescription string 'OpenCPN gebruikt de GPS van dit toestel als positiebron wanneer de verbinding Ingebouwde GPS aan staat.'"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Delete :NSMotionUsageDescription"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c
          "Add :NSMotionUsageDescription string 'OpenCPN leest de barometer van dit toestel voor de luchtdruk.'"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Delete :UIBackgroundModes"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIBackgroundModes array"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIBackgroundModes:0 string location"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  # App-pictogram (tegel op het beginscherm) uit OpenCPN's Mac-pictogram; ontbrak eerst.
  COMMAND "${OCPNIOS_ROOT}/scripts/make-app-icon.sh"
          "${CMAKE_SOURCE_DIR}/buildosx/opencpn.icns" "${CMAKE_BINARY_DIR}/ocpnios-icon"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>"
          "${OCPNIOS_ICON_PLATFORM}"
  # iPhone en iPad (1,2). XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY geldt alleen met de
  # Xcode-generator; met Ninja ontbrak UIDeviceFamily en draaide de app op de iPad in
  # de iPhone-compatibiliteitsstand (8 nov 2026).
  COMMAND /usr/libexec/PlistBuddy -c "Delete :UIDeviceFamily"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist" || true
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIDeviceFamily array"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIDeviceFamily:0 integer 1"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  COMMAND /usr/libexec/PlistBuddy -c "Add :UIDeviceFamily:1 integer 2"
          "$<TARGET_BUNDLE_CONTENT_DIR:opencpn>/Info.plist"
  VERBATIM)
