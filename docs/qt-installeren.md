# Qt 6.12.0 voor iOS installeren (officiële installer)

Handmatige stap van fase 1, stap 0. Xander doet dit zelf, omdat de installer een Qt-account
vraagt. Gecontroleerd op 8 nov 2026; wat niet gecontroleerd is, staat erbij.

## Vooraf (gemeten 8 nov 2026)
- Xcode 26.6 actief (`xcode-select -p` geeft `/Applications/Xcode.app/Contents/Developer`).
- iOS SDK 26.5, simulator-runtimes iOS 26.4 en 26.5, iPad- en iPhone-simulators aanwezig.
  Qt 6.12 vraagt minimaal Xcode 16 en iOS 18 als doel; dat is ruim gehaald.
- 317 GB vrij. `~/Qt` bestaat nog niet.

## 1. Installer downloaden
- Bestand: `qt-online-installer-macOS-universal.dmg` (versie 4.11.0, 46.448.205 bytes, 25 mrt 2026)
- Bron: <https://download.qt.io/official_releases/online_installers/qt-online-installer-macOS-universal.dmg>
  (verwijst door naar een spiegelserver)
- SHA-256 volgens download.qt.io (`...dmg.sha256`):
  `917fb4048226db081b780b751138c896165f66df523f3b0f4385bcfb4a1c59da`
- Controleren: `shasum -a 256 ~/Downloads/qt-online-installer-macOS-universal*.dmg`

## 2. Installer doorlopen
De schermvolgorde hieronder is de gebruikelijke en niet stap voor stap nagelopen voor 4.11.0.
1. Dmg openen, het installatieprogramma starten.
2. Inloggen met een Qt-account, of er in de installer een aanmaken.
3. Bevestigen dat je Qt onder de open-source licentie (LGPL/GPL) gebruikt.
4. Bij de vraag over het meesturen van gebruiksgegevens: niet meesturen.
5. Map: de standaard `~/Qt` laten staan.
6. Kies **Custom installation**.

## 3. Onderdelen kiezen
Onder **Qt → Qt 6.12.0** alleen dit aanvinken:

| in de installer | pakket | download | uitgepakt |
|---|---|---|---|
| macOS | `qt.qt6.6120.clang_64` | 254 MB | 1,8 GB |
| iOS → Simulator (Apple Silicon) | `qt.qt6.6120.qtforios.simulator_arm64` | 863 MB | 7,2 GB |
| iOS → Device | `qt.qt6.6120.qtforios.device` | 863 MB | 7,2 GB |
| Additional Libraries → Qt Image Formats (voor macOS en voor beide iOS-doelen) | `...addons.qtimageformats.*` | 3 × ~4 MB | ~65 MB |

- Qt SVG hoeft niet apart: die zit in Qt 6 in het basispakket (gezien in de archieflijst).
- Uit laten: "Simulator (Intel)", Android, WebAssembly, Qt Quick 3D, Qt Multimedia, Qt WebEngine,
  Debug Information Files, Sources, en andere Qt-versies.
- Qt Creator en de Maintenance Tool installeert de installer waarschijnlijk verplicht mee;
  dat is goed (grootte niet gemeten).
- Totaal voor Qt zelf ongeveer 2 GB download en 16 GB op schijf.

## Uitgevoerd op 8 nov 2026
Geïnstalleerd in `~/Qt/6.12.0/{macos,ios_simulator_arm64,ios_device}` (16 GB). In de
installer staan de onderdelen onder Qt for Development → Qt → Qt 6.12.0. Achtergrondklikken
werken niet in de installer; Claude had volledige schermbesturing nodig, en de installer mag
niet op een ander bureaublad staan dan een schermvullend venster. Controle: zie logboek.

## 4. Na afloop
Zeg tegen Claude dat het klaar is. Claude controleert dan (alleen lezen):
- of `~/Qt/6.12.0/macos`, `~/Qt/6.12.0/ios` en de simulatorkit bestaan;
- de versie via `qt-cmake --version` en `qtpaths`;
- of `QtPrintSupport`, `QtOpenGLWidgets` en `QtTest` in de iOS-kits zitten (wxWidgets 3.3.4
  vraagt ze verplicht);
- en zet het resultaat in het logboek (werknotitie, niet in de openbare repo).

Bijwerken naar 6.12.1 (gepland 17 nov 2026) kan later met `~/Qt/MaintenanceTool.app`.
