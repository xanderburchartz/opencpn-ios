// Stub voor de o-charts-ontsleuteling op iOS (zie ocharts_ios_backend.h).
// Wordt gelinkt zolang o-charts geen eigen bibliotheek levert (CMake-optie
// OCPNIOS_OCHARTS_BACKEND leeg). Doet niets, ontsleutelt niets en geeft bij elke vraag
// OCIOS_ERR_NOT_AVAILABLE. De plugin meldt dat aan de gebruiker; niets crasht.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ocharts_ios_backend.h"

extern "C" {

int ocios_backend_abi_version(void) { return OCIOS_BACKEND_ABI; }

int ocios_backend_start(const char*, int) { return OCIOS_ERR_NOT_AVAILABLE; }

void ocios_backend_stop(void) {}

const char* ocios_backend_status(void) {
  return "stub: o-charts decryption library not linked (OCPNIOS_OCHARTS_BACKEND)";
}

int ocios_exec(int, const char* const*, ocios_line_fn, void*) {
  return OCIOS_ERR_NOT_AVAILABLE;
}

ocios_stream* ocios_request(const void*, size_t) { return nullptr; }

long ocios_stream_read(ocios_stream*, void*, size_t) { return -1; }

void ocios_stream_close(ocios_stream*) {}

}  // extern "C"
