// Ingebouwde plugins op iOS (zie static_plugins.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef OCPNIOS_STATIC_PLUGINS_H
#define OCPNIOS_STATIC_PLUGINS_H

extern "C" {
struct OcpnIosStaticPlugin {
  const char* file_name;           // bijv. "libdashboard_pi.dylib"
  void* (*create)(void* manager);  // opencpn_plugin* create_pi(void*)
  void (*destroy)(void* plugin);   // void destroy_pi(opencpn_plugin*)
  const char* common_name;         // GetCommonName() in kleine letters, bijv. "dashboard"
};
/** Lijst, afgesloten met een element met file_name == nullptr. */
const OcpnIosStaticPlugin* OcpnIosStaticPlugins();
/** Zoek op bestandsnaam (zonder map); nullptr als het geen ingebouwde plugin is. */
const OcpnIosStaticPlugin* OcpnIosFindStaticPlugin(const char* file_name);
}

#endif
