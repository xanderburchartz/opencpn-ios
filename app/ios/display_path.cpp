// Mappen tonen zoals de Bestanden-app ze laat zien.
//
// In OpenCPN stonden kaartmappen als volledig pad van de app-container, bijv.
// /var/mobile/Containers/Data/Application/<UUID>/Documents/Charts/ENC/NL_INLAND (Xander,
// 13 nov 2026: "oogt nog heel erg desktop"). De map Documents van de app is in de
// Bestanden-app zichtbaar als "Op mijn iPhone › OpenCPN". Dit geeft dezelfde weergave:
// "OpenCPN › Charts › ENC › NL_INLAND". Alleen weergave; het pad zelf blijft ongewijzigd.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/stdpaths.h>
#include <wx/string.h>

wxString ocpnios_DisplayPath(const wxString& path) {
  const wxString sep = wxString::FromUTF8(" › ");
  wxString docs = wxStandardPaths::Get().GetDocumentsDir();
  wxString p = path;
  while (p.EndsWith("/")) p.RemoveLast();

  wxString rest;
  bool inDocs = false;
  if (!docs.IsEmpty() && (p == docs || p.StartsWith(docs + "/"))) {
    rest = p.Mid(docs.Length());
    inDocs = true;
  } else {
    // Pad uit een vorige container (na een herinstallatie): ook vanaf Documents tonen.
    const int d = p.Find("/Documents");
    if (d != wxNOT_FOUND && (p.Length() == (size_t)d + 10 || p[d + 10] == '/')) {
      rest = p.Mid(d + 10);
      inDocs = true;
    }
  }
  if (inDocs) {
    rest.Replace("/", sep);
    return "OpenCPN" + rest;  // rest begint met de scheiding, of is leeg
  }
  // Buiten Documents (bijv. Library): niet zichtbaar in de Bestanden-app.
  const int lib = p.Find("/Library/");
  if (lib != wxNOT_FOUND) {
    wxString r = p.Mid(lib + 1);
    r.Replace("/", sep);
    return wxString::FromUTF8("OpenCPN (intern)") + sep + r;
  }
  return path;
}
