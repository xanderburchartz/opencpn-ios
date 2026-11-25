// iOS: plugin panes float over the chart (gui/src/ocpn_app.cpp and
// gui/src/ocpn_frame.cpp call this). Separate from frame_ios.cpp because the
// platform commit of the OpenCPN series already calls it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <wx/wx.h>
#include <wx/aui/aui.h>

#include "chcanv.h"

// iOS: plugin instrument panes (Dashboard, Tactics) are never docked next to the
// chart. Docked, the pane took a whole column and left a large grey area below
// the instruments. Floating instead, and draggable over the chart
// (app/ios/window_fit.cpp). A docked position saved earlier floats on startup.
// Called from MyApp::BuildMainFrame (ocpn_app.cpp) and ApplyLocale (ocpn_frame.cpp).
void ocpnios_FloatPluginPanes(wxAuiManager* mgr) {
  if (!mgr) return;
  bool changed = false;
  wxAuiPaneInfoArray& panes = mgr->GetAllPanes();
  for (size_t i = 0; i < panes.GetCount(); i++) {
    wxAuiPaneInfo& p = panes.Item(i);
    if (!p.window || dynamic_cast<ChartCanvas*>(p.window)) continue;
    if (p.IsToolbar()) continue;
    p.Dockable(false);
    if (p.IsDocked()) {
      p.Float();
      if (p.floating_pos.x < 0 || p.floating_pos.y < 0) p.FloatingPosition(80, 80);
      changed = true;
    }
  }
  if (changed) mgr->Update();
}
