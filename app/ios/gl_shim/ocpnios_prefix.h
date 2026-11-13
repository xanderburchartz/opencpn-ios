// opencpn-ios: geforceerd ingesloten bij losse plugins (ios-plugins.cmake).
// - GLdouble bestaat niet in OpenGL ES; de GLU-kopie in opencpn-libs/plugin_dc gebruikt het.
// - Sommige bronnen gebruiken LONG_MAX of std::list zonder de header (gaat elders goed via
//   indirecte includes).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <limits.h>
#ifdef __cplusplus
#include <list>
#endif
typedef double GLdouble;
