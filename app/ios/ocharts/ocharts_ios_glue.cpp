// o-charts_pi op iOS: wxExecute-vervanger en meldingen (zie ocharts_ios_prefix.h en
// ocharts_ios_backend.h). Hoort bij de plugin (statisch gelinkt in de app).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/wx.h>
#include <wx/cmdline.h>
#include <wx/process.h>
#include <wx/utils.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "ocpn_plugin.h"
#include "ocharts_ios_backend.h"
#include "ocharts_ios_glue.h"

namespace {

std::mutex g_mutex;
bool g_started = false;          // ocios_backend_start gaf OCIOS_OK
bool g_abi_checked = false;
bool g_abi_ok = false;
std::atomic<bool> g_reported{false};

bool AbiOk() {
  if (!g_abi_checked) {
    g_abi_checked = true;
    int v = ocios_backend_abi_version();
    g_abi_ok = (v == OCIOS_BACKEND_ABI);
    if (!g_abi_ok)
      wxLogMessage("o-charts_pi iOS: backend ABI %d, verwacht %d", v, OCIOS_BACKEND_ABI);
  }
  return g_abi_ok;
}

// Probeert de ontsleuteling te starten; idempotent.
bool EnsureStarted(bool debug) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_started) return true;
  if (!AbiOk()) return false;
  wxString dir = *GetpPrivateApplicationDataLocation();
  int rc = ocios_backend_start(dir.utf8_str().data(), debug ? 1 : 0);
  wxLogMessage("o-charts_pi iOS: ocios_backend_start = %d (%s)", rc,
               wxString::FromUTF8(ocios_backend_status()));
  g_started = (rc == OCIOS_OK);
  return g_started;
}

struct ExecOut {
  wxArrayString* out;
  wxArrayString* err;
};

void CollectLine(void* ctx, const char* line, int is_stderr) {
  auto* o = static_cast<ExecOut*>(ctx);
  wxString s = wxString::FromUTF8(line ? line : "");
  if (is_stderr && o->err)
    o->err->Add(s);
  else if (o->out)
    o->out->Add(s);
}

// Splitst een opdracht zoals de plugin hem bouwt (aanhalingstekens " en ').
bool SplitSentinel(const wxString& command, std::vector<std::string>& args) {
  wxArrayString a = wxCmdLineParser::ConvertStringToArgs(command, wxCMD_LINE_SPLIT_UNIX);
  if (a.IsEmpty() || a[0] != OCIOS_SENCUTIL_SENTINEL) return false;
  for (size_t i = 1; i < a.GetCount(); i++) args.push_back(a[i].utf8_string());
  return true;
}

long RunSync(const wxString& command, wxArrayString* out, wxArrayString* err) {
  std::vector<std::string> args;
  if (!SplitSentinel(command, args)) {
    wxLogMessage("o-charts_pi iOS: programma starten kan niet op iOS: %s", command);
    if (err) err->Add("ERROR: cannot run external programs on iOS");
    return -1;
  }
  if (!AbiOk()) {
    if (err) err->Add("ERROR: o-charts decryption library has a different interface version");
    return -1;
  }
  std::vector<const char*> argv;
  for (auto& s : args) argv.push_back(s.c_str());
  ExecOut o{out, err};
  int rc = ocios_exec(static_cast<int>(argv.size()), argv.data(), CollectLine, &o);
  if (rc == OCIOS_ERR_NOT_AVAILABLE) {
    wxLogMessage("o-charts_pi iOS: '%s': ontsleuteling niet beschikbaar (%s)", command,
                 wxString::FromUTF8(ocios_backend_status()));
    // Ook in de gewone uitvoer: getFPR() zoekt alleen daar naar "ERROR" en meldde
    // anders "Fingerprint file created. DONGLE_NOT_PRESENT".
    if (out) out->Add("ERROR: o-charts decryption is not available on iOS");
    if (err && err != out) err->Add("ERROR: o-charts decryption is not available on iOS");
    return -1;
  }
  return rc;
}

}  // namespace

// ---- wxExecute-vervangers (namen via ocharts_ios_prefix.h) --------------------------

long OcIosWxExecute(const wxString& command, int flags, wxProcess* process,
                    const wxExecuteEnv*) {
  if (flags & wxEXEC_SYNC) {
    wxArrayString out, err;
    return RunSync(command, &out, &err);
  }
  std::vector<std::string> args;
  if (!SplitSentinel(command, args)) {
    wxLogMessage("o-charts_pi iOS: programma starten kan niet op iOS: %s", command);
    return 0;
  }
  // Asynchroon starten van de server: `oexserverd [-d] [-b]`. Andere asynchrone vormen
  // (de oude SENCclient met -t -s) gebruikt de plugin niet meer.
  bool debug = false, server = true;
  for (auto& a : args) {
    if (a == "-d")
      debug = true;
    else if (a != "-b")
      server = false;
  }
  if (!server) {
    wxLogMessage("o-charts_pi iOS: asynchrone aanroep niet ondersteund: %s", command);
    return 0;
  }
  (void)process;
  return EnsureStarted(debug) ? 1 : 0;
}

long OcIosWxExecute(const char* const* argv, int flags, wxProcess* process,
                    const wxExecuteEnv* env) {
  wxString cmd;
  for (const char* const* p = argv; p && *p; ++p) {
    if (!cmd.empty()) cmd += " ";
    cmd += wxString::Format("\"%s\"", wxString::FromUTF8(*p));
  }
  return OcIosWxExecute(cmd, flags, process, env);
}

long OcIosWxExecute(const wchar_t* const* argv, int flags, wxProcess* process,
                    const wxExecuteEnv* env) {
  wxString cmd;
  for (const wchar_t* const* p = argv; p && *p; ++p) {
    if (!cmd.empty()) cmd += " ";
    cmd += wxString::Format("\"%s\"", wxString(*p));
  }
  return OcIosWxExecute(cmd, flags, process, env);
}

long OcIosWxExecute(const wxString& command, wxArrayString& output, int,
                    const wxExecuteEnv*) {
  return RunSync(command, &output, &output);
}

long OcIosWxExecute(const wxString& command, wxArrayString& output, wxArrayString& error,
                    int, const wxExecuteEnv*) {
  return RunSync(command, &output, &error);
}

// ---- Hulpfuncties voor de patch ------------------------------------------------------

bool OcIosBackendUsable() { return EnsureStarted(false); }

long OcIosStartServer(bool debug) {
  if (EnsureStarted(debug)) return 1;
  OcIosReportUnavailable(false);
  return 0;
}

void OcIosReportUnavailable(bool always) {
  if (!always && g_reported.exchange(true)) return;
  g_reported = true;
  wxString msg = _(
      "o-charts decryption is not available on iOS.\n\n"
      "The o-charts plugin is built into this app, but the decryption library from "
      "o-charts is not included yet. Encrypted o-charts charts cannot be shown, and this "
      "device cannot be registered in the o-charts shop. Other charts are not affected.");
  wxLogMessage("o-charts_pi iOS: melding 'niet beschikbaar' getoond (%s)",
               wxString::FromUTF8(ocios_backend_status()));
  wxString caption = _("o-charts_pi Message");
  // Kaarten worden soms in een werkdraad geopend; een venster alleen op de hoofddraad.
  if (!wxThread::IsMain()) {
    if (wxTheApp)
      wxTheApp->CallAfter([msg, caption] {
        OCPNMessageBox_PlugIn(nullptr, msg, caption, wxOK, -1, -1);
      });
    return;
  }
  OCPNMessageBox_PlugIn(nullptr, msg, caption, wxOK, -1, -1);
}

extern "C" int OcIosOchartsNetworkAllowed(void) {
  if (OcIosBackendUsable()) return 1;
  wxLogMessage("o-charts_pi iOS: verbinding met o-charts tegengehouden (geen ontsleuteling)");
  return 0;
}
