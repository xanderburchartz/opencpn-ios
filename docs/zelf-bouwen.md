# OpenCPN voor iOS zelf bouwen en installeren

Deze handleiding beschrijft hoe je de app zelf bouwt uit de broncode en op je eigen iPhone of
iPad zet, met een gratis Apple-ID. Ook wie de app via TestFlight krijgt, kan zo een eigen of
gewijzigde versie maken: dat vraagt de licentie (GPLv3, met Qt onder LGPLv3, zie
[licenties-verspreiding.md](licenties-verspreiding.md)).

English version: [zelf-bouwen.en.md](zelf-bouwen.en.md). Wijzig beide samen.

Gemeten met: Mac met Apple M4 Pro, macOS 26.5, Xcode 26.6, Qt 6.12.0 (november 2026).
Andere versies kunnen werken, maar zijn niet geprobeerd.

## 1. Wat je nodig hebt

- Een Mac met Apple Silicon en ongeveer 45 GB vrije ruimte (Qt ~16 GB, broncode ~2 GB,
  bouwmappen ~10 GB per doel).
- **Xcode** uit de App Store, één keer geopend (dan installeert hij zijn onderdelen).
  Controle: `xcode-select -p` geeft `/Applications/Xcode.app/Contents/Developer`.
- **Homebrew** (<https://brew.sh>) en daarmee:

      brew install cmake ninja gettext xcodegen

- **Qt 6.12.0 voor iOS**, via het Qt-installatieprogramma met een (gratis) Qt-account. Kies
  onder Qt 6.12.0: macOS, iOS Simulator (Apple Silicon), iOS Device en Qt Image Formats.
  Laat de map op `~/Qt` staan. Stap voor stap: [qt-installeren.md](qt-installeren.md).
  Andere map: zet `QT_ROOT=/pad/naar/Qt/6.12.0` bij alle bouwopdrachten.
- Een **Apple-ID**. Een gratis account volstaat voor je eigen toestellen (zie §6 voor de
  beperkingen).
- Voor de iPhone of iPad: iOS of iPadOS 18 of nieuwer.

## 2. Broncode ophalen

    git clone https://github.com/xanderburchartz/opencpn-for-ios.git
    cd opencpn-for-ios
    scripts/fetch-upstream.sh        # OpenCPN, wxWidgets, plugins, OpenSSL, curl, ... (~2 GB)
    scripts/apply-patches.sh         # de iOS-wijzigingen op die broncode zetten

`fetch-upstream.sh` haalt elk onderdeel op een vaste commit; zo bouwt iedereen precies
dezelfde versie. `scripts/fetch-upstream.sh --lijst` toont welke. De broncode van derden
komt in `upstream/` en staat niet in git.

## 3. Eerst proberen in de simulator (aanrader)

    scripts/build-deps-ios.sh sim          # OpenSSL en curl
    scripts/build-wxqt-ios.sh sim all      # wxWidgets op Qt
    scripts/build-ocpn-ios.sh sim all      # OpenCPN met plugins

Daarna in een iOS-simulator zetten en starten (simulator eerst openen in Xcode of met
`open -a Simulator`):

    xcrun simctl install booted build/sim/ocpn/opencpn.app
    xcrun simctl launch booted local.opencpnios.opencpn

## 4. Bouwen voor je toestel

Kies eerst een **eigen bundel-ID-voorvoegsel**. Apple staat niet toe dat twee ontwikkelaars
dezelfde bundel-ID gebruiken. Neem iets wat van jou is, bijvoorbeeld een omgekeerde
domeinnaam of je naam:

    export OCPNIOS_BUNDLE_PREFIX=nl.jouwnaam.opencpnios

En je **Team-ID**: Xcode › Settings › Accounts › je Apple-ID › het team "(Personal Team)".
Het tiencijferige ID staat ook in `security find-identity -v -p codesigning` achter je naam,
of in de Apple-ontwikkelaarssite.

    export OCPNIOS_TEAM_ID=ABCDE12345

Dan de Release-bouw voor het toestel:

    scripts/build-deps-ios.sh device
    scripts/build-wxqt-ios.sh device-release all
    scripts/build-ocpn-ios.sh device-release all

Resultaat: `build/device-release/ocpn/opencpn.app` (nog niet ondertekend).

## 5. Ondertekenen en installeren

**Toestel voorbereiden** (eenmalig):
1. Sluit het toestel met een kabel aan op de Mac en vertrouw de computer op het toestel.
2. Open Xcode › Window › Devices and Simulators, kies het toestel en wacht tot Xcode het
   klaarmaakt voor ontwikkeling.
3. Zet op het toestel **Ontwikkelaarsmodus** aan: Instellingen › Privacy en beveiliging ›
   Ontwikkelaarsmodus, daarna herstart het toestel.

**Profiel ophalen** (eenmalig per toestel, en opnieuw na 7 dagen bij een gratis account):

    scripts/make-profile.sh iphone        # of: ipad

Dit maakt een leeg Xcode-project met jouw bundel-ID, laat Xcode een ontwikkelprofiel
aanmaken en bewaart het in `build/device/profielen/iphone.mobileprovision`. Bij een fout:
zie `build/logs/profiel-iphone.log`.

**Ondertekenen en installeren:**

    OCPNIOS_BUILD=device-release scripts/sign-ocpn-ios.sh build/device/profielen/iphone.mobileprovision
    xcrun devicectl list devices                     # noteer het Identifier van je toestel
    xcrun devicectl device install app --device <Identifier> build/device-release/signed/opencpn.app

Het toestel moet ontgrendeld zijn. Installeren kan ook zonder kabel als de Mac en het toestel
op hetzelfde netwerk zitten en het toestel eerder via de kabel gekoppeld is.

Bij de eerste start vraagt iOS of OpenCPN het lokale netwerk mag gebruiken (voor NMEA- en
AIS-gegevens van je instrumenten) en of de app je locatie mag gebruiken (ingebouwde GPS).

## 6. Beperkingen van een gratis Apple-ID

- Een app die je met een gratis account installeert, **werkt 7 dagen**. Daarna start hij niet
  meer: opnieuw `make-profile.sh`, ondertekenen en installeren. Je kaarten en instellingen
  blijven bewaard zolang je de app niet verwijdert.
- Hooguit **3 eigen apps** tegelijk per toestel.
- Met een betaald ontwikkelaarsaccount gelden die grenzen niet en kan het ook via TestFlight.

## 7. Wijzigingen maken

- De iOS-specifieke code staat in `app/` (eigen code) en in `patches/` (wijzigingen aan de
  broncode van derden). Pas je iets aan in `upstream/<project>`, maak dan de patch opnieuw,
  bijvoorbeeld voor OpenCPN:

      git -C upstream/OpenCPN add -A && git -C upstream/OpenCPN diff --cached Release_5.14.2 > patches/OpenCPN/0001-ios-port.patch

- Opnieuw bouwen na een wijziging: `scripts/build-ocpn-ios.sh device-release build` (alleen
  wat veranderd is). Na een wijziging aan `app/cmake/ios-plugins.cmake` of in een plugin
  met fixups: `... device-release all`.

## 8. Als het misgaat

| melding | oorzaak en oplossing |
|---|---|
| `geen qt-cmake in ...` | Qt staat niet in `~/Qt/6.12.0` of de iOS-onderdelen ontbreken; zet `QT_ROOT` of installeer iOS Device/Simulator in de Qt-installer. |
| `upstream/... ontbreekt` | `scripts/fetch-upstream.sh` (en daarna `scripts/apply-patches.sh`) niet gedraaid. |
| `geen Apple Development-identiteit gevonden` | Log in Xcode in met je Apple-ID (Settings › Accounts) en laat Xcode een certificaat maken; `make-profile.sh` doet dat ook. |
| `maximum number of installed apps using a free developer profile` | Gratis account: verwijder een andere eigen app van het toestel. |
| `unable to locate a device` bij installeren | Toestel vergrendeld, op een ander netwerk of niet gekoppeld; ontgrendelen of met de kabel aansluiten. |
| App start niet meer na een week | Gratis profiel verlopen, zie §6. |

## 9. Hoe lang het duurt

Gemeten op een Mac met M4 Pro: zie de tabel hieronder (eerste keer, daarna gaat het
grotendeels incrementeel).

| stap | tijd |
|---|---|
| `fetch-upstream.sh` | ~2 min (afhankelijk van de verbinding) |
| `build-deps-ios.sh` (per doel) | ~3 min |
| `build-wxqt-ios.sh device-release all` | ~1,5 min |
| `build-ocpn-ios.sh device-release all` | ~4,5 min |
