// Fase 2: OpenCPN-kern op iOS. Opent een S-57 ENC-cel (.000) met de OGR-S57-driver uit
// OpenCPN, telt de objecten per objectklasse en ontleedt een NMEA 0183-zin.
// Geen GUI: uitvoer naar stderr (simctl launch --console-pty).
//
// Zoekt de ENC in <bundel>/enc/*.000, of in het pad uit de omgeving S57COUNT_ENC.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/init.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/string.h>

#include "ogr_s57.h"
#include "s57class_registrar.h"
#include "nmea0183.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {

void Out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void Out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::fputs("S57COUNT ", stderr);
  std::vfprintf(stderr, fmt, ap);
  std::fputc('\n', stderr);
  va_end(ap);
  std::fflush(stderr);
}

wxString ResourceDir() {
  // Op iOS is de bundel zelf de resourcemap.
  return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
}

int CountEnc(const wxString &csvDir, const wxString &encPath) {
  S57ClassRegistrar registrar;
  if (!registrar.LoadInfo(csvDir.utf8_str(), FALSE)) {
    Out("FOUT: klassenregistrar niet geladen uit %s", (const char *)csvDir.utf8_str());
    return 1;
  }
  Out("registrar geladen uit %s", (const char *)csvDir.utf8_str());

  OGRS57DataSource ds;
  ds.SetS57Registrar(&registrar);
  // Updates (.001, .002, ...) naast de basiscel toepassen.
  char *opts[] = {const_cast<char *>("UPDATES=APPLY"), nullptr};
  ds.SetOptionList(opts);
  // Open geeft 0 bij succes, anders een foutcode (BAD_FILE, BAD_OPEN, BAD_UPDATE, ...).
  const int openRc = ds.Open(encPath.utf8_str(), TRUE);
  if (openRc != 0) {
    Out("FOUT: kan ENC niet openen (rc=%d): %s", openRc, (const char *)encPath.utf8_str());
    return 1;
  }

  S57Reader *reader = ds.GetModule(0);
  Out("ENC geopend: %s, DSNM=%s, schaal 1:%d", (const char *)encPath.utf8_str(),
      reader ? reader->GetDSNM() : "?", reader ? reader->GetCSCL() : 0);

  // Tellen via de lezer: elk feature-record (FE) uit de cel, ingedeeld naar objectklasse.
  std::map<std::string, int> counts;
  int total = 0, unknown = 0;
  if (reader) {
    reader->Rewind();
    while (OGRFeature *f = reader->ReadNextFeature()) {
      ++total;
      OGRFeatureDefn *defn = f->GetDefnRef();
      if (defn)
        ++counts[defn->GetName()];
      else
        ++unknown;
      delete f;
    }
  }
  std::vector<std::pair<int, std::string>> perClass;
  for (auto &kv : counts) perClass.emplace_back(kv.second, kv.first);
  std::sort(perClass.rbegin(), perClass.rend());
  Out("lagen: %d, FE-records in index: %d, gelezen objecten: %d (zonder klasse: %d), "
      "klassen: %zu",
      ds.GetLayerCount(), reader ? reader->GetFeatureCount() : -1, total, unknown,
      perClass.size());
  for (size_t i = 0; i < perClass.size() && i < 15; ++i)
    Out("  %-8s %d", perClass[i].second.c_str(), perClass[i].first);

  OGREnvelope env;
  if (reader && reader->GetExtent(&env, TRUE) == OGRERR_NONE)
    Out("omvang: lat %.4f..%.4f, lon %.4f..%.4f", env.MinY, env.MaxY, env.MinX, env.MaxX);
  return 0;
}

void ParseNmea() {
  // Voorbeeldzin uit de NMEA-documentatie (geen boot- of vaargebiedgegevens).
  const wxString rmc =
      "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
  NMEA0183 nmea;
  nmea << rmc;
  if (nmea.PreParse() && nmea.LastSentenceIDReceived == "RMC" && nmea.Parse()) {
    Out("NMEA RMC: lat %.4f %s, lon %.4f %s, SOG %.1f kn, COG %.1f",
        nmea.Rmc.Position.Latitude.Latitude,
        nmea.Rmc.Position.Latitude.Northing == North ? "N" : "S",
        nmea.Rmc.Position.Longitude.Longitude,
        nmea.Rmc.Position.Longitude.Easting == East ? "E" : "W",
        nmea.Rmc.SpeedOverGroundKnots, nmea.Rmc.TrackMadeGoodDegreesTrue);
  } else {
    Out("FOUT: NMEA niet ontleed: %s", (const char *)nmea.ErrorMessage.utf8_str());
  }
}

}  // namespace

int main(int argc, char **argv) {
  wxInitializer init(argc, argv);
  if (!init.IsOk()) {
    Out("FOUT: wxInitializer mislukt");
    return 1;
  }
  Out("start, wxWidgets %s", wxVERSION_NUM_DOT_STRING);

  ParseNmea();

  const wxString res = ResourceDir();
  const wxString csvDir = res + "/s57data";
  wxString encPath;
  if (const char *env = std::getenv("S57COUNT_ENC")) {
    encPath = wxString::FromUTF8(env);
  } else {
    wxArrayString found;
    if (wxDir::Exists(res + "/enc"))
      wxDir::GetAllFiles(res + "/enc", &found, "*.000");
    if (!found.empty()) encPath = found[0];
  }
  if (encPath.empty()) {
    Out("geen ENC gevonden in %s/enc (of S57COUNT_ENC)", (const char *)res.utf8_str());
    return 2;
  }
  int rc = CountEnc(csvDir, encPath);
  Out("klaar, rc=%d", rc);
  return rc;
}
