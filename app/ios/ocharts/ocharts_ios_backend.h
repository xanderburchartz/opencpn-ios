// o-charts op iOS: de ene interface tussen de open o-charts-plugin en de gesloten
// ontsleuteling van o-charts.
//
// Op Windows, macOS, Linux en Android start de plugin een apart programma, oexserverd,
// en praat ermee op twee manieren:
//   1. opdrachtregel: `oexserverd -a` (versie), `-g <map>` (fingerprint maken), enz.;
//      de plugin leest de uitvoerregels;
//   2. een serverproces met een publieke FIFO (/tmp/OCPN_PIPEX): de plugin schrijft een
//      verzoek (struct fifo_msg of rnc_fifo_msg, zie Osenc.h en oernc_inStream.h) en
//      leest het antwoord uit een eigen, private FIFO.
// Op iOS mag een app geen eigen processen starten. Daarom zijn beide wegen hier
// functies binnen de app. De plugin zelf is ongewijzigd in wat hij vraagt en wat hij
// terug verwacht; alleen het vervoer is anders (zie ocharts_ios_glue.cpp en
// ocharts_ios_pipe.cpp).
//
// Wie levert wat:
//   - Deze header en de plugin-kant: opencpn-ios (GPL-2.0-or-later).
//   - De implementatie: o-charts, als statische bibliotheek of xcframework in de app
//     (CMake-optie OCPNIOS_OCHARTS_BACKEND). Zonder die bibliotheek linkt de app
//     ocharts_backend_stub.cpp, die bij elke vraag OCIOS_ERR_NOT_AVAILABLE geeft.
//
// Alle functies worden aangeroepen vanuit de draad van de plugin (de wx-hoofddraad of
// een werkdraad van OpenCPN bij het laden van kaarten). Een implementatie moet daarom
// draadveilig zijn; twee stromen kunnen tegelijk open zijn.
//
// Tekst is UTF-8. Paden zijn absolute paden binnen de app-container.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Versie van deze interface. Een implementatie geeft hem terug in
// ocios_backend_abi_version(); de plugin weigert een andere hoofdversie.
#define OCIOS_BACKEND_ABI 1

// Foutcodes (negatief).
#define OCIOS_OK 0
#define OCIOS_ERR_NOT_AVAILABLE (-1)  // geen ontsleuteling in deze build (de stub)
#define OCIOS_ERR_BAD_ARGS (-2)
#define OCIOS_ERR_FAILED (-3)         // algemene fout; details in ocios_backend_status()

// ---- Levensloop -------------------------------------------------------------------

// Hoofdversie van de interface waartegen de implementatie gebouwd is.
int ocios_backend_abi_version(void);

// Starten, op de plek waar de plugin op andere platforms oexserverd start
// (`oexserverd [-d]`, wxEXEC_ASYNC). Mag vaker aangeroepen worden; tweede keer = niets doen.
//   private_data_dir: schrijfbare map van de plugin (GetpPrivateApplicationDataLocation),
//                     voor fingerprints en eigen bestanden van de implementatie;
//   debug: 1 als de plugin om uitgebreide logging vraagt (`-d`).
// Geeft OCIOS_OK of een foutcode.
int ocios_backend_start(const char* private_data_dir, int debug);

// Stoppen (CMD_EXIT). Daarna mag ocios_backend_start opnieuw.
void ocios_backend_stop(void);

// Korte, leesbare toestand voor het log en voor meldingen, bijv. "stub: niet beschikbaar".
// Geldig tot de volgende aanroep van een ocios_-functie.
const char* ocios_backend_status(void);

// ---- 1. Opdrachtregel -------------------------------------------------------------

// Voert één aanroep uit met dezelfde argumenten en dezelfde uitvoerregels als
// `oexserverd <argv...>` op macOS. argv[0] is het eerste argument (niet de programmanaam).
// De plugin gebruikt nu (o-charts_pi 2.2.5.0):
//   -a            versie van de server            (fpr.cpp GetServerVersionString)
//   -s            dongle aanwezig: "1" of "0"     (fpr.cpp IsDongleAvailable)
//   -t            serienummer dongle               (fpr.cpp GetDongleSN)
//   -v <sleutel>  vervaldatum                       (fpr.cpp getExpDate)
//   -g <map>      fingerprint maken, uitvoerregel "FPR:<pad>"  (fpr.cpp getFPR)
//   -k <map>      idem voor de SGLock-dongle
//   -y '<tekst>'  extra info bij -g/-k
// Elke uitvoerregel (zonder regeleinde) gaat naar out_line(ctx, regel, is_stderr).
// Geeft de exitcode van de aanroep (0 = goed) of een negatieve foutcode.
typedef void (*ocios_line_fn)(void* ctx, const char* line, int is_stderr);
int ocios_exec(int argc, const char* const* argv, ocios_line_fn out_line, void* ctx);

// ---- 2. Serververzoeken (de FIFO-weg) -----------------------------------------------

typedef struct ocios_stream ocios_stream;

// Eén verzoek, byte voor byte zoals de plugin het in de publieke FIFO schrijft:
// struct fifo_msg (Osenc.cpp: CMD_READ_ESENC, CMD_READ_ESENC_HDR, CMD_TEST_AVAIL,
// CMD_EXIT, CMD_GET_HK) of struct rnc_fifo_msg (oernc_inStream.cpp: CMD_OPEN_RNC,
// CMD_OPEN_RNC_FULL). Het veld fifo_name bevat een naam die alleen binnen de app
// betekenis heeft; de implementatie hoeft hem niet te openen.
// Geeft een stroom met precies de bytes die oexserverd in de private FIFO zou schrijven,
// of NULL bij een fout (de plugin behandelt dat als "server niet bereikbaar").
ocios_stream* ocios_request(const void* msg, size_t msg_len);

// Leest tot n bytes. Geeft het aantal (>0), 0 aan het eind van de stroom, of -1 bij een
// fout. Mag blokkeren tot er gegevens zijn (zoals read() op de FIFO).
long ocios_stream_read(ocios_stream* s, void* buf, size_t n);

// Sluit de stroom en geeft alles vrij. Mag midden in de stroom (de plugin leest bij
// CMD_READ_ESENC_HDR alleen de kop).
void ocios_stream_close(ocios_stream* s);

#ifdef __cplusplus
}
#endif
