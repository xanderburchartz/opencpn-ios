// o-charts_pi op iOS: hulpfuncties aan de plugin-kant (zie ocharts_ios_backend.h).
// Ingesloten door de iOS-delen van patches/o-charts_pi/0001-ios.patch.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Waarde van g_sencutil_bin op iOS. Geen pad: elke "aanroep" van dit programma gaat naar
// de functies in ocharts_ios_backend.h (ocharts_ios_glue.cpp).
#define OCIOS_SENCUTIL_SENTINEL "ocharts-ios-backend"

// Start de ontsleuteling (eenmalig) op de plek waar de plugin anders oexserverd start.
// Geeft een nep-procesnummer (>0) als het lukte, anders 0. Bij 0 heeft de functie de
// gebruiker al gemeld dat de ontsleuteling op iOS niet beschikbaar is (eenmaal per sessie).
long OcIosStartServer(bool debug);

// true als de ontsleuteling draait (of nu gestart kan worden). Meldt niets.
bool OcIosBackendUsable();

// Toont "o-charts-ontsleuteling niet beschikbaar op iOS". Met always=false hooguit
// eenmaal per sessie (automatische paden zoals een kaart openen), met always=true bij elke
// handeling van de gebruiker.
void OcIosReportUnavailable(bool always);

// Voor de veiligheidspal in libs/wxcurl: de plugin maakt geen verbinding met de
// o-charts-winkel zolang er geen ontsleuteling is (zonder kan niets geregistreerd of
// gebruikt worden).
extern "C" int OcIosOchartsNetworkAllowed(void);
