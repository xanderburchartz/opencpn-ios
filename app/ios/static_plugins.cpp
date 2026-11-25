// Lijst van de ingebouwde plugins voor iOS (zie app/cmake/ios-plugins.cmake).
// PluginLoader (gepatcht, model/src/plugin_loader.cpp) vraagt deze lijst op in plaats van
// een map met dylibs te doorzoeken, en haalt create/destroy hier op in plaats van met dlsym.
// De bestandsnamen zijn die van de Mac-versie, zodat de [PlugIns/...]-secties in de config
// (aan/uit) gelijk zijn. De lijst zelf (ocpnios_static_plugins.inc) maakt CMake, met één
// regel OCPNIOS_PLUGIN(symbool, "bestand", "gewone naam") per ingebouwde plugin.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "static_plugins.h"

#include <cstring>

extern "C" {
#define OCPNIOS_PLUGIN(sym, file, common) \
  void* sym##_create_pi(void*);           \
  void sym##_destroy_pi(void*);
#include "ocpnios_static_plugins.inc"
#undef OCPNIOS_PLUGIN
}

static const ocpnios_StaticPlugin kPlugins[] = {
#define OCPNIOS_PLUGIN(sym, file, common) {file, sym##_create_pi, sym##_destroy_pi, common},
#include "ocpnios_static_plugins.inc"
#undef OCPNIOS_PLUGIN
    {nullptr, nullptr, nullptr, nullptr}};

extern "C" const ocpnios_StaticPlugin* ocpnios_StaticPlugins() { return kPlugins; }

extern "C" const ocpnios_StaticPlugin* ocpnios_FindStaticPlugin(const char* file_name) {
  if (!file_name) return nullptr;
  for (const ocpnios_StaticPlugin* p = kPlugins; p->file_name; ++p)
    if (std::strcmp(p->file_name, file_name) == 0) return p;
  return nullptr;
}
