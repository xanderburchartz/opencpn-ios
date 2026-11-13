# Licenties bij verspreiding via TestFlight (onderzoek 13 nov 2026)

Doel: nagaan wat de licenties vragen als de iOS-app van OpenCPN via TestFlight wordt
verspreid en de repo openbaar wordt. Dit is een inventarisatie van wat de bronnen zeggen,
geen juridisch advies.

## Wat er in de app zit

| Onderdeel | Licentie (gemeten in de bron) |
|---|---|
| OpenCPN 5.14.2 en alle meegebouwde plugins (radar, weather_routing, autopilot_route, raymarine/navico autopilot, polar, tactics, vdr, watchdog, pypilot, o-charts_pi) | GPL versie 2 **of later**. Gescand: geen enkel bronbestand met "alleen versie 2" (script in logboek 13 nov). |
| wxWidgets 3.3 | wxWindows-licentie (LGPL met uitzondering die binaire verspreiding onder eigen voorwaarden toestaat) |
| Qt 6.12 (Core, Gui, Widgets, OpenGLWidgets, PrintSupport, Test, SVG, beeldformaten, ios-platformplugin) | Open source: LGPLv3, ook beschikbaar onder GPL. Geen van deze modules staat in Qt's lijst van alleen-GPL-modules. Statisch gelinkt (iOS kan niet anders). |
| OpenSSL 3.5.9 | Apache 2.0 |
| curl | curl-licentie (MIT-achtig) |
| shapelib, rapidjson | MIT / LGPL / BSD-achtig |
| o-charts: oexserverd (de gesloten ontsleutelaar) | **Niet** meegeleverd; in de app zit alleen een stub. |

Gevolg: het geheel kan alleen onder **GPLv3** worden verspreid. Apache 2.0 (OpenSSL) en
LGPLv3 (Qt) zijn verenigbaar met GPLv3, niet met GPLv2. Omdat alle GPL-code "versie 2 of
later" is, mag dat. Eigen code in deze repo: GPL-2.0-or-later (CLAUDE.md), dus ook goed.

## Qt bij statisch linken

Wat Qt zelf zegt (https://www.qt.io/licensing/open-source-lgpl-obligations, gelezen 13 nov):
- Bij statisch linken valt de app zelf mogelijk ook onder de licentie: "It is recommended
  to either link dynamically, or provide the application source code".
- LGPLv3 eist dat de gebruiker de bibliotheek kan wijzigen, de app opnieuw kan linken én de
  opnieuw gelinkte app kan draaien; "sufficient installation information must be provided".
- Qt raadt bij app stores vaak de commerciële licentie aan; de open-sourcelicenties "may or
  may not be compatible" met app stores (https://www.qt.io/faq/qt-open-source-licensing).

Wat dat hier betekent:
- **Broncode:** als de repo openbaar is met alle patches en bouwscripts, en `upstream/`
  reproduceerbaar opgehaald wordt (`scripts/fetch-upstream.sh` op vaste commits), is de hele
  app als broncode beschikbaar. Dat is de route die Qt zelf noemt.
- **Installatie-informatie:** iemand moet een gewijzigde versie op zijn eigen toestel kunnen
  zetten. Dat kan met Xcode en een gratis Apple-ID (Personal Team), zoals wij nu zelf doen.
  Nodig: een README-hoofdstuk "zelf bouwen en installeren" met de exacte stappen.
- Niet nodig: Qt dynamisch linken of een commerciële Qt-licentie, zolang de app GPL is en de
  bron openbaar.

## Apple-voorwaarden versus GPL

- De FSF vond in 2010 dat verspreiding via de App Store niet voldoet aan de GPL, omdat
  Apple's voorwaarden extra beperkingen opleggen; Apple haalde GNU Go daarop weg
  (https://www.fsf.org/news/2010-05-app-store-compliance).
- Of dit ook voor TestFlight geldt en hoe streng het nu is, heb ik niet vastgesteld. Het risico
  is dat een **auteursrechthebbende** (een OpenCPN- of plugin-ontwikkelaar) bezwaar maakt.
- Gangbare oplossing: de makers vooraf informeren of om toestemming vragen. Voor OpenCPN
  zelf ligt het voor de hand dat eerst op het OpenCPN-forum of bij de kernontwikkelaars te
  melden; voor plugins de maintainers (radar_pi, weather_routing_pi, o-charts_pi, …).
- Dit is een keuze voor Xander, geen technische.

## Bijvangst: debugbouw

De toestelbuild is nu een **Debug**-build (`CMAKE_BUILD_TYPE=Debug` in
`scripts/build-ocpn-ios.sh` en `scripts/build-wxqt-ios.sh`) en linkt de `_debug`-varianten
van Qt. Voor TestFlight hoort een Release-build: kleiner, sneller, en Apple verwacht dat.
De releasebibliotheken van Qt zijn er (`~/Qt/6.12.0/ios_device/lib/QtCore.framework/QtCore`,
statisch archief, naast `QtCore_debug`); omzetten is een bouwkeuze, geen installatie.

## Voor de repo openbaar gaat (niet uitgevoerd)

- Persoonlijke gegevens nalopen: CLAUDE.md en het logboek bevatten toestel-UDID's, het
  Team-ID, IP-adressen van het boordnetwerk en namen van apparaten. Geen wachtwoorden of
  sleutels gevonden in eerdere geheimencontroles, maar wel nalopen.
- `build/` en `upstream/` staan buiten git; controleren dat er geen grote of andermans
  bestanden in de geschiedenis zitten.
- Een LICENSE-bestand (GPL-3.0 voor het geheel, met vermelding van de onderdelen hierboven)
  en een README-hoofdstuk over zelf bouwen.
