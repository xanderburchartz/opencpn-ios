// iOS: the chart bar (piano) only while there is a choice, i.e. more than one chart at
// the current position (HIG review 26 Nov 2026, round 2; Xander's choice). With a single
// chart the bar only took space at the bottom of the chart.
//
// Without a core change: the user's own choice ("Show chart bar" in Options) is kept in
// /Settings/iOS/ChartBarWanted; g_bShowChartBar follows that choice and the chart count.
// A change of g_bShowChartBar that this file did not make came from the user and
// becomes the new choice. Called once a second (status_panel.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <wx/wx.h>

#include "model/config_vars.h"
#include "chartdb.h"
#include "chcanv.h"
#include "navutil.h"  // pConfig
#include "ocpn_frame.h"

#ifdef __OCPN__IOS__

namespace {

int s_set = -1;      // value of g_bShowChartBar this file set last; -1 = none yet
int s_wanted = -1;   // the user's choice

void SaveWanted() {
  if (!pConfig) return;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath("/Settings/iOS");
  pConfig->Write("ChartBarWanted", (long)s_wanted);
  pConfig->SetPath(old);
  pConfig->Flush();
}

int ChartsHere(ChartCanvas* cc) {
  if (!cc) return 0;
  if (cc->GetQuiltMode()) return (int)cc->GetQuiltExtendedStackdbIndexArray().size();
  ChartStack* st = cc->GetpCurrentStack();
  return st ? st->nEntry : 0;
}

}  // namespace

// The user's choice, -1 while not known yet (options_ios.cpp shows it in Options).
int ocpnios_ChartBarWanted() { return s_wanted; }
void ocpnios_ChartBarTick();

// Set the user's choice (chart display options, canvas_ios.cpp) and apply it now.
void ocpnios_ChartBarSetWanted(bool wanted) {
  s_wanted = wanted ? 1 : 0;
  SaveWanted();
  ocpnios_ChartBarTick();
}

void ocpnios_ChartBarTick() {
  if (!gFrame || !pConfig) return;
  ChartCanvas* cc = gFrame->GetPrimaryCanvas();
  if (!cc) return;
  if (s_wanted < 0) {
    const wxString old = pConfig->GetPath();
    pConfig->SetPath("/Settings/iOS");
    long w = -1;
    pConfig->Read("ChartBarWanted", &w, -1L);
    pConfig->SetPath(old);
    s_wanted = w >= 0 ? (int)w : (g_bShowChartBar ? 1 : 0);
  }
  // Changed elsewhere (Options, plugin API): that is the user's choice now.
  if (s_set >= 0 && (int)g_bShowChartBar != s_set) {
    s_wanted = g_bShowChartBar ? 1 : 0;
    SaveWanted();
  }
  const bool show = s_wanted == 1 && ChartsHere(cc) > 1;
  if (g_bShowChartBar != show) gFrame->ToggleChartBar(cc);
  s_set = g_bShowChartBar ? 1 : 0;
}

#endif  // __OCPN__IOS__
