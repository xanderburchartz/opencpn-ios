# o-charts op iOS: wat de ontsleutelingsbibliotheek moet kunnen

Stand 11 nov 2026. Technische notitie voor het gesprek met o-charts; Engelse versie in
[`o-charts-ios.en.md`](o-charts-ios.en.md). Gebaseerd op de open bron van o-charts_pi 2.2.5.0 (GPL-2.0,
github.com/bdbcat/o-charts_pi, commit `cc4d4e75`). Het gesloten deel (oexserverd,
libsgl) is niet bekeken of ontleed; alles hieronder volgt uit wat de open plugin aan
oexserverd vraagt en wat hij terug verwacht.

## Uitgangspunt
- De plugin o-charts_pi is in deze iOS-app ingebouwd (statisch, zoals alle plugins op iOS)
  en laadt; zijn schermen werken.
- De ontsleuteling en de licentiecontrole zitten bij o-charts in het programma
  `oexserverd` plus de bibliotheek `libsgl`. Op iOS mag een app geen eigen programma
  starten, dus dat kan daar niet draaien.
- Daarom is de koppeling met oexserverd in deze app één C-interface:
  [`app/ios/ocharts/ocharts_ios_backend.h`](../app/ios/ocharts/ocharts_ios_backend.h).
  o-charts levert de implementatie als statische bibliotheek of xcframework; die wordt
  meegelinkt met de CMake-optie `OCPNIOS_OCHARTS_BACKEND=<pad>`. Code achteraf downloaden
  mag op iOS niet, de bibliotheek zit dus in de app-bundel.
- Zonder die bibliotheek linkt de app een stub
  ([`ocharts_backend_stub.cpp`](../app/ios/ocharts/ocharts_backend_stub.cpp)). De plugin
  meldt dan "o-charts-ontsleuteling niet beschikbaar op iOS" en maakt geen verbinding met
  de o-charts-winkel.

## Wat de plugin nu van oexserverd vraagt
De plugin gebruikt twee wegen. In de iOS-app gaan beide ongewijzigd naar de interface:
dezelfde argumenten, dezelfde berichten, dezelfde antwoorden. Alleen het vervoer is anders.

### 1. Aanroepen op de opdrachtregel (`ocios_exec`)
Synchroon, de plugin leest de uitvoerregels.

| argument | doel | waar in de plugin | verwachte uitvoer |
|---|---|---|---|
| `-a` | versie van de server | `fpr.cpp` GetServerVersionString, `validator.cpp` | één regel, langer dan 2 tekens |
| `-s` | dongle aanwezig | `fpr.cpp` IsDongleAvailable | `1` of `0` |
| `-t` | serienummer van de dongle | `fpr.cpp` GetDongleSN | getal |
| `-v <sleutel>` | vervaldatum van een sleutel | `fpr.cpp` getExpDate | één regel |
| `-g <map>` | fingerprint (FPR) van dit toestel maken | `fpr.cpp` getFPR | regel `FPR:<pad>`; `ERROR` bij een fout |
| `-k <map>` | idem voor de SGLock-dongle | `fpr.cpp` getFPR | idem |
| `-y '<tekst>'` | extra informatie bij `-g`/`-k` | `fpr.cpp` getFPR | |

Op iOS is er geen USB-dongle (SGLock); `-s` hoort `0` te geven. `-m` (TPM) en `-b` (TPM
uit) gebruikt de plugin alleen op Linux. Op Android maakt de plugin de fingerprint met
`-y <WVID> -k` en leest hij hem van stdout (`ochartShop.cpp` doUploadXFPR, voorvoegsel
`oc04R_`); op iOS volgt de plugin nu de macOS/Linux-weg met een bestand.

### 2. Serververzoeken (`ocios_request`, `ocios_stream_read`, `ocios_stream_close`)
Op macOS/Linux start de plugin `oexserverd` als achtergrondproces (op iOS:
`ocios_backend_start`) en stuurt verzoeken over de FIFO `/tmp/OCPN_PIPEX`. Elk verzoek
is één struct; het antwoord komt als bytestroom.

| struct (header) | opdracht | doel |
|---|---|---|
| `fifo_msg` (`Osenc.h`) | `CMD_TEST_AVAIL` (1) | server beschikbaar; antwoord `OK` |
| | `CMD_READ_ESENC` (0) | versleutelde vectorkaart (eSENC/oesu) lezen |
| | `CMD_READ_ESENC_HDR` (3) | alleen de kop daarvan |
| | `CMD_GET_HK` (6) | `getHK()` in `oernc_inStream.cpp`; betekenis van het antwoord niet nagegaan |
| | `CMD_EXIT` (2) | server stoppen |
| `rnc_fifo_msg` (`oernc_inStream.h`) | `CMD_OPEN_RNC` (4) / `CMD_OPEN_RNC_FULL` (5) | rasterkaart (oernc), kop of volledig |

`ocios_request` krijgt de struct byte voor byte en geeft een stroom met precies wat
oexserverd in de private FIFO zou schrijven. Het veld `fifo_name` hoeft de bibliotheek niet
te openen. Het in-app vervoer staat in
[`ocharts_ios_pipe.cpp`](../app/ios/ocharts/ocharts_ios_pipe.cpp).

### Eisen aan de bibliotheek
- C-koppeling (`extern "C"`), arm64 voor toestel en simulator (xcframework), iOS 18 of
  hoger, zonder eigen kopie van wxWidgets of Qt (die zitten al in de app).
- Draadveilig: OpenCPN opent kaarten ook in werkdraden; meerdere stromen tegelijk.
- Geen eigen processen, geen dlopen van code buiten de bundel.
- `ocios_backend_abi_version()` geeft `OCIOS_BACKEND_ABI` (nu 1).

## Toestelherkenning (de open vraag)
Op de desktop maakt oexserverd met libsgl een fingerprint van de hardware; `fpr.cpp` roept
alleen `-g`/`-k` aan en uploadt het bestand. Op Android geeft de app zelf gegevens mee aan
oexserverd: een UUID en de Widevine-ID (`-z`, `-y`, zie `o-charts_pi.cpp` Init en
validate_SENC_server), omdat Android vaste hardware-ID's afschermt.

iOS geeft apps geen vaste hardware-ID (geen serienummer, IMEI of MAC-adres). Wat er wel is
(volgens Apple-documentatie; niet in deze app getest):

| middel | blijft gelijk bij | verandert bij |
|---|---|---|
| `identifierForVendor` | bijwerken van de app | verwijderen van alle apps van dezelfde ontwikkelaar |
| eigen UUID in de Keychain (alleen dit toestel) | opnieuw installeren | wissen of herstellen van het toestel naar een ander toestel |
| DeviceCheck (2 bits per toestel, bij Apple) | | niet nagegaan |
| App Attest (sleutel in de Secure Enclave, door Apple ondertekend) | | opnieuw installeren (nieuwe sleutel) |

Widevine bestaat niet op iOS. De vraag aan o-charts is dus welke herkenning zij accepteren
voor een licentie per toestel, en hoe een gebruiker na een nieuw toestel of herinstallatie
verder kan. Dat bepaalt de inhoud van `-g` op iOS; de interface hoeft er niet voor te
veranderen, de bibliotheek kan de iOS-API's zelf aanroepen.

## Andere open punten
- **Licentie:** een gesloten bibliotheek in een app met GPL-code (OpenCPN, deze plugin) is
  een juridische vraag voor o-charts en het OpenCPN-team samen. Op de desktop staat
  oexserverd als apart programma naast de GPL-plugin; op iOS zit alles in één bundel.
- **Verkeer met de winkel:** zolang er geen bibliotheek is, houdt de app elke verbinding van
  de plugin met o-charts.org tegen (`libs/wxcurl/src/base.cpp`, patch). Er is niet
  ingelogd en geen toestel geregistreerd.
- **Rasterkaarten (oernc)** volgen dezelfde weg; niet apart beproefd.

## Wat aan de app-kant klaarstaat
- `patches/o-charts_pi/0001-ios.patch`: iOS-takken in de plugin (vervoer, GL ES,
  wx 3.3-aanpassingen, melding, tegenhouden van winkelverkeer) en de Nederlandse vertaling
  van de melding.
- `app/cmake/ios-ocharts.cmake`: bouw van de plugin en keuze tussen stub en bibliotheek.
  Staat standaard aan sinds 12 nov 2026 (besluit Xander); uit met `-DOCPNIOS_WITH_OCHARTS=OFF`. Met bibliotheek
  `-DOCPNIOS_OCHARTS_BACKEND=<pad>`.
- `app/ios/ocharts/`: interface, stub, lijm aan de plugin-kant.

## Wat getest is (simulator, iPhone 17 Pro Max, 11 en 12 nov 2026)
| onderdeel | uitkomst |
|---|---|
| plugin laden, aanzetten, pluginlijst | laadt; staat erin met pictogram en Nederlandse tekst |
| start met stub | `-s` gaat naar de stub, "No Dongle detected", plugin werkt door |
| winkelpaneel (Opties › Kaarten › o-charts) | opent; "Kaartenlijst vernieuwen" geeft de melding, geen netwerkverkeer in het log |
| Voorkeuren › fingerprint maken | geeft de melding (eerst foutief "Fingerprint file created", opgelost) |
| USB-dongleknop | verborgen op iOS (geen dongle mogelijk) |
| nep-`.oesu` in een kaartmap | plugin weigert vóór de ontsleuteling ("RInstallKey not found"), geen crash |
| GRIB, WeatherRouting naast o-charts | werken |
| herstarts met o-charts aan en uit | geen crash |

Niet getest: een echte o-charts-kaart openen (vraagt een echte sleutel en de bibliotheek),
de tegenhouding in `libs/wxcurl` (wordt in de huidige schermen niet bereikt, de knop houdt
eerder tegen), iPad, nachtstand. Toestel: valt buiten deze notitie.

## Vragen aan o-charts
1. Willen jullie de ontsleuteling als in-app bibliotheek voor iOS leveren, tegen deze
   interface (of een eigen voorstel)?
2. Welke toestelherkenning accepteren jullie op iOS, en wat gebeurt er bij een nieuw toestel
   of herinstallatie?
3. Hoe zien jullie de combinatie van een gesloten bibliotheek met de GPL-code in één
   app-bundel (samen met het OpenCPN-team)?
4. Mogen wij tegen jullie testomgeving testen voordat er iets naar testers gaat?

Engelse versie: [`o-charts-ios.en.md`](o-charts-ios.en.md) (13 nov 2026). Wijzig beide samen.
