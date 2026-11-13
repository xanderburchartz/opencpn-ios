# Zet OpenCPN's gedeelde data in de iOS-app-bundel onder SharedSupport/, zoals de upstream-
# CMake dat voor de Mac-bundel doet (upstream CMakeLists.txt, regels ~1620-1840).
# Gebruik: ocpnios_bundle_data(<doel>) na qt_add_executable.
# SPDX-License-Identifier: GPL-2.0-or-later

function(_ocpnios_add_res target location)
  foreach(f ${ARGN})
    if(NOT IS_ABSOLUTE "${f}")
      set(f "${CMAKE_SOURCE_DIR}/${f}")
    endif()
    if(EXISTS "${f}" AND NOT IS_DIRECTORY "${f}")
      set_source_files_properties("${f}" PROPERTIES MACOSX_PACKAGE_LOCATION "${location}")
      target_sources(${target} PRIVATE "${f}")
    endif()
  endforeach()
endfunction()

function(ocpnios_bundle_data target)
  set(S "${CMAKE_SOURCE_DIR}")
  file(GLOB gshhs "${S}/data/gshhs/*")
  file(GLOB basemap "${S}/data/basemap_shp/basemap_*")
  file(GLOB s57data "${S}/data/s57data/*")
  file(GLOB filters "${S}/data/filters/*.json")
  file(GLOB tcdata "${S}/data/tcdata/*")
  file(GLOB sounds "${S}/data/sounds/*")
  file(GLOB configs "${S}/data/configs/*")
  file(GLOB plugin_icons "${S}/plugins/icons/*")
  _ocpnios_add_res(${target} SharedSupport/gshhs ${gshhs})
  _ocpnios_add_res(${target} SharedSupport/basemap_shp ${basemap})
  _ocpnios_add_res(${target} SharedSupport/ssfndata data/ssfn/FreeSans.sfn)
  _ocpnios_add_res(${target} SharedSupport/s57data ${s57data})
  _ocpnios_add_res(${target} SharedSupport/filters ${filters})
  _ocpnios_add_res(${target} SharedSupport/tcdata ${tcdata})
  _ocpnios_add_res(${target} SharedSupport/sounds ${sounds})
  _ocpnios_add_res(${target} SharedSupport/configs ${configs})
  _ocpnios_add_res(${target} SharedSupport/uidata/plugins ${plugin_icons})
  _ocpnios_add_res(${target} SharedSupport
    data/license.html data/authors.html data/opencpn.png data/ocpn-plugins.xml
    data/license.txt
    # Inland ECDIS-gedragscodes, zoals in de Mac-bundel. Geen code opent ze; alleen pariteit.
    data/CoC-909_2013-InlandECDIS_20170308s.pdf
    data/CoC-1973_2018-InlandECDIS_20220912.pdf)
  # Help > Help laadt doc/help_web.html (verwijst naar de online handleiding).
  _ocpnios_add_res(${target} SharedSupport/doc data/doc/help_web.html)

  # uidata: zelfde lijst als upstream (uiData) plus de SVG-stijlmappen.
  _ocpnios_add_res(${target} SharedSupport/uidata
    resources/bitmaps/styles.xml
    resources/bitmaps/toolicons_journeyman.png
    resources/bitmaps/toolicons_journeyman_flat.png
    resources/bitmaps/toolicons_traditional.png
    resources/bitmaps/iconRMinus.png resources/bitmaps/iconRPlus.png
    resources/bitmaps/iconMinimum.png resources/bitmaps/iconStandard.png
    resources/bitmaps/iconUserStd.png resources/bitmaps/iconAll.png
    resources/bitmaps/DragHandle.svg resources/bitmaps/eye.svg resources/bitmaps/eyex.svg
    resources/bitmaps/eyeGray.svg resources/bitmaps/iENC_All.svg
    resources/bitmaps/iENC_Minimum.svg resources/bitmaps/iENC_Standard.svg
    resources/bitmaps/iENC_UserStd.svg resources/bitmaps/iENC_RPlus.svg
    resources/bitmaps/iENC_RMinus.svg resources/bitmaps/package-x-generic.png
    resources/bitmaps/plus.svg)
  # markicons: pictogrammen voor markeringen en routepunten. Ontbraken eerst; de
  # pictogramkeuze had dan alleen "tempsub" (8 nov 2026).
  foreach(style traditional journeyman journeyman_flat MUI_flat markicons)
    file(GLOB svgs "${S}/data/svg/${style}/*")
    _ocpnios_add_res(${target} SharedSupport/uidata/${style} ${svgs})
  endforeach()

  # Afbeeldingen voor de iOS-vormgeving (schakelaars, app/ios/touch_ui.cpp).
  file(GLOB _iosui "${OCPNIOS_ROOT}/app/ios/ui/*.svg")
  _ocpnios_add_res(${target} SharedSupport/uidata/ios ${_iosui})

  # Data van de ingebouwde plugins, zoals de Mac-bundel: SharedSupport/plugins/<naam>/data.
  set(_pis dashboard_pi grib_pi wmm_pi chartdldr_pi radar_pi)
  foreach(e ${OCPNIOS_EXT_PLUGINS})
    string(REPLACE "|" ";" e "${e}")
    list(GET e 0 _n)
    list(APPEND _pis ${_n})
  endforeach()
  foreach(pi ${_pis})
    set(_pd "${S}/plugins/${pi}/data")
    if(NOT EXISTS "${_pd}")
      set(_pd "${OCPNIOS_ROOT}/upstream/${pi}/data")
    endif()
    file(GLOB_RECURSE _files RELATIVE "${_pd}" "${_pd}/*")
    foreach(_rel ${_files})
      get_filename_component(_sub "${_rel}" DIRECTORY)
      if(_sub)
        _ocpnios_add_res(${target} "SharedSupport/plugins/${pi}/data/${_sub}" "${_pd}/${_rel}")
      else()
        _ocpnios_add_res(${target} "SharedSupport/plugins/${pi}/data" "${_pd}/${_rel}")
      endif()
    endforeach()
  endforeach()
endfunction()

# Vertalingen. De upstream-CMake maakt .mo-bestanden met msgfmt (gettext_build_mo) en
# zet ze bij de installatie neer; die stap zat niet in de iOS-bouw, waardoor de app alleen
# Engels was (9 nov 2026). Hier: per taal opencpn.mo (uit po/) en wxstd-3.3.mo (uit de
# wxWidgets-installatie) in SharedSupport/locale/<taal>/LC_MESSAGES/. OCPNPlatform zoekt
# daar op iOS (patch).
function(ocpnios_bundle_translations target)
  find_program(OCPNIOS_MSGFMT msgfmt HINTS /opt/homebrew/bin /usr/local/bin)
  if(NOT OCPNIOS_MSGFMT)
    message(WARNING "msgfmt niet gevonden: geen vertalingen in de app")
    return()
  endif()
  set(S "${CMAKE_SOURCE_DIR}")
  # Zelfde talen en mapnamen als upstream (CMakeLists.txt, gettext_build_mo).
  set(_langs
    cs_CZ:cs da_DK:da de_DE:de es_ES:es fr_FR:fr it_IT:it nl_NL:nl pl_PL:pl
    ru_RU:ru sv_SE:sv et_EE:et pt_PT:pt_PT pt_BR:pt_BR nb_NO:nb_NO tr_TR:tr_TR
    fi_FI:fi_FI el_GR:el_GR zh_TW:zh_TW zh_CN:zh_CN hu_HU:hu_HU gl_ES:gl_ES
    ca_ES:ca_ES ar_SA:ar_SA ja_JP:ja_JP vi_VN:vi_VN he_IL:he_IL en_GB:en_GB)
  set(_wxloc "${wxWidgets_DIR}/../../../share/locale")
  foreach(pair ${_langs})
    string(REPLACE ":" ";" pl "${pair}")
    list(GET pl 0 _po)
    list(GET pl 1 _lang)
    set(_src "${S}/po/opencpn_${_po}.po")
    if(NOT EXISTS "${_src}")
      continue()
    endif()
    set(_out "${CMAKE_BINARY_DIR}/ocpnios-locale/${_lang}/opencpn.mo")
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ocpnios-locale/${_lang}"
      COMMAND "${OCPNIOS_MSGFMT}" --check -o "${_out}" "${_src}"
      DEPENDS "${_src}"
      COMMENT "i18n: opencpn.mo ${_lang}")
    set_source_files_properties("${_out}" PROPERTIES
      GENERATED TRUE
      MACOSX_PACKAGE_LOCATION "SharedSupport/locale/${_lang}/LC_MESSAGES")
    target_sources(${target} PRIVATE "${_out}")
    # Ingebouwde plugins: opencpn-<plugin>.mo in dezelfde taalmap (AddLocaleCatalog).
    set(_pis dashboard_pi grib_pi wmm_pi chartdldr_pi radar_pi)
    foreach(e ${OCPNIOS_EXT_PLUGINS})
      string(REPLACE "|" ";" e "${e}")
      list(GET e 0 _n)
      list(APPEND _pis ${_n})
    endforeach()
    foreach(_pi ${_pis})
      set(_pisrc "${S}/plugins/${_pi}/po/${_po}.po")
      if(NOT EXISTS "${_pisrc}")
        set(_pisrc "${OCPNIOS_ROOT}/upstream/${_pi}/po/${_po}.po")
      endif()
      if(EXISTS "${_pisrc}")
        set(_piout "${CMAKE_BINARY_DIR}/ocpnios-locale/${_lang}/opencpn-${_pi}.mo")
        add_custom_command(
          OUTPUT "${_piout}"
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ocpnios-locale/${_lang}"
          COMMAND "${OCPNIOS_MSGFMT}" -o "${_piout}" "${_pisrc}"
          DEPENDS "${_pisrc}"
          COMMENT "i18n: opencpn-${_pi}.mo ${_lang}")
        set_source_files_properties("${_piout}" PROPERTIES
          GENERATED TRUE
          MACOSX_PACKAGE_LOCATION "SharedSupport/locale/${_lang}/LC_MESSAGES")
        target_sources(${target} PRIVATE "${_piout}")
      endif()
    endforeach()
    # wxWidgets' eigen teksten (knoppen, standaarddialogen): taalcode zonder land.
    string(REGEX REPLACE "_.*" "" _short "${_lang}")
    foreach(_cand "${_lang}" "${_short}")
      set(_wxmo "${_wxloc}/${_cand}/LC_MESSAGES/wxstd-3.3.mo")
      if(EXISTS "${_wxmo}")
        get_filename_component(_wxmo "${_wxmo}" ABSOLUTE)
        set_source_files_properties("${_wxmo}" PROPERTIES
          MACOSX_PACKAGE_LOCATION "SharedSupport/locale/${_lang}/LC_MESSAGES")
        target_sources(${target} PRIVATE "${_wxmo}")
        break()
      endif()
    endforeach()
  endforeach()
endfunction()
