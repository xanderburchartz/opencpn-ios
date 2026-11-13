// wx-asserts op iOS naar opencpn.log in plaats van een dialoog. In een Debug-bouw toont wx
// bij een assert een modale melding; op de iPhone is die lastig te lezen en kan hij zelf
// problemen geven. Zo staan de asserts in het log, dat we met devicectl ophalen.
// Uitzetten (dialoog terug) met env OCPNIOS_ASSERT_DIALOG.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/debug.h>
#include <wx/log.h>
#include <wx/string.h>

#include <cstdio>
#include <cxxabi.h>
#include <execinfo.h>
#include <cstdlib>

namespace {

void LogAssert(const wxString& file, int line, const wxString& func,
               const wxString& cond, const wxString& msg) {
  // Een assert tijdens het loggen van een assert niet opnieuw loggen.
  static thread_local bool busy = false;
  if (busy) return;
  busy = true;
  wxString s = wxString::Format("ASSERT %s:%d %s: %s %s", file, line, func, cond, msg);
  std::fprintf(stderr, "%s\n", s.utf8_str().data());
  wxLogMessage("%s", s);
  // De eerste asserts krijgen een aanroepstapel mee; zonder stapel is op het toestel
  // niet te zien waar een assert vandaan komt.
  static int withStack = 0;
  if (withStack < 5) {
    withStack++;
    void* frames[16];
    int n = backtrace(frames, 16);
    char** syms = backtrace_symbols(frames, n);
    for (int i = 2; syms && i < n; i++) {
      // Formaat: "<nr> <image> <adres> <naam> + <offset>"
      char mod[64], name[512];
      unsigned long addr;
      int off;
      wxString line(syms[i]);
      if (std::sscanf(syms[i], "%*d %63s %lx %511s + %d", mod, &addr, name, &off) == 4) {
        int st = 0;
        char* dem = abi::__cxa_demangle(name, nullptr, nullptr, &st);
        line = wxString::Format("%s %s", mod, st == 0 && dem ? dem : name);
        std::free(dem);
      }
      wxLogMessage("ASSERT   #%d %s", i - 1, line.Left(200));
    }
    std::free(syms);
  }
  busy = false;
}

struct Installer {
  Installer() {
    // wxTheAssertHandler is constant-geïnitialiseerd in wx (appbase.cpp), dus dit
    // wordt niet overschreven door de dynamische initialisatie van wx.
    if (!std::getenv("OCPNIOS_ASSERT_DIALOG")) wxSetAssertHandler(LogAssert);
  }
} g_installer;

}  // namespace
