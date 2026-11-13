// Vervanger van upstream model/src/certificates.cpp voor iOS: daar is geen OpenSSL.
// make_certificate maakt de TLS-certificaten voor de REST-server (routes en waypoints delen
// tussen apparaten). Zonder certificaat werkt die server niet; de rest van OpenCPN wel.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>

#include "model/certificates.h"

int make_certificate(std::string /*ip_v4*/, std::string /*destination_dir*/) {
  return 1;  // niet ondersteund op iOS (geen OpenSSL)
}
