// Deelvenster van iOS voor een bestand (zie share_sheet.mm).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Toont het iOS-deelvenster (AirPrint, Mail, Bestanden, ...) voor het bestand op
// `path`. Doet niets als het bestand niet bestaat. Keert direct terug; het venster
// verschijnt na de huidige event-ronde.
void OcpnIosShareFile(const char* path);
