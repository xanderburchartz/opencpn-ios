// iOS parts of the OpenCPN main frame (gui/src/ocpn_frame.cpp) and of MyApp
// (gui/src/ocpn_app.cpp): resize handling during rotation, colours of menu bar and
// status bar per colour scheme, end of startup, floating plugin panes. Members of
// MyFrame are declared in gui/include/gui/ocpn_frame.h; the free functions are
// declared at their call sites. They live here so that the core files only carry
// the hooks that call them.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <wx/wx.h>
#include <wx/aui/aui.h>

#include <QtWidgets/QWidget>
#include <QGuiApplication>

#include "model/gui_vars.h"  // g_compass_scalefactor
#include "model/safe_mode.h"
#include "chcanv.h"          // ChartCanvas, g_canvasArray, global_color_scheme
#include "compass.h"
#include "navutil.h"         // pConfig
#include "ocpn_frame.h"
#include "color_scheme.h"    // OcpnIosApplyUiColors (app/ios/color_scheme.mm)

#ifdef __OCPN__IOS__
void ocpnios_HideStartupBusy();  // app/ios/startup_busy.mm

// iOS: menu bar, status bar and iOS status bar follow the color scheme, using
// the UI colors from the OpenCPN color table (DILG1 background, UITX1 text).
// Day: light grey with black text, as before (the status bar sits at the bottom
// near the home indicator, where it is black).
void ocpnios_ApplySchemeColors(wxFrame *frame) {
  if (!frame || !frame->GetHandle()) return;
  const bool day = global_color_scheme == GLOBAL_COLOR_SCHEME_DAY ||
                   global_color_scheme == GLOBAL_COLOR_SCHEME_RGB;
  // Dusk and night: near black like the toolbar, with dimmed text. DILG1 from the
  // color table gave mid grey (dusk ~#6e6e6e, night ~#4f4f4f), lighter than the
  // chart and the toolbar.
  const bool night = global_color_scheme == GLOBAL_COLOR_SCHEME_NIGHT;
  wxColour bg = day ? wxColour(235, 235, 235)
                    : (night ? wxColour(0, 0, 0) : wxColour(20, 20, 22));
  wxColour fg = day ? wxColour(0, 0, 0)
                    : (night ? wxColour(90, 90, 90) : wxColour(141, 141, 147));
  OcpnIosApplyUiColors(frame->GetHandle(), bg.Red(), bg.Green(), bg.Blue(),
                       fg.Red(), fg.Green(), fg.Blue(), !day);
}

// iOS: called from MyFrame::OnSize instead of ODoSetSize().
void MyFrame::IosOnSize() {
  // iOS: after rotation 5 size events with the same size arrived (5 x ODoSetSize
  // in 1.2 s, 80-350 ms each); all that time the screen was half black. Skip a
  // repeat with the same size.
  static wxSize s_last(-1, -1);
  const wxSize now = GetClientSize();
  if (now == s_last) return;
  s_last = now;
  // First resize only the chart (fast), so no half-black screen remains; the full
  // relayout (status bar, toolbar, plugins, ~240 ms) runs once afterwards.
  // Intermediate sizes during rotation (e.g. 440x881) are merged this way.
  SetCanvasSizes(now);
  for (unsigned int i = 0; i < g_canvasArray.GetCount(); i++)
    if (g_canvasArray.Item(i)) g_canvasArray.Item(i)->Refresh(false);
  static bool s_pending = false;
  if (!s_pending) {
    s_pending = true;
    CallAfter([this]() {
      s_pending = false;
      ODoSetSize();
    });
  }
}

// iOS: the Retina factor of chart and compass depends on g_bopengl.
// Recompute it after switching (via OnSize and SetScaleFactor), otherwise
// zoom jumps and oversized text without OpenGL. Called from ProcessOptionsDialog.
void MyFrame::IosRescaleCanvases() {
  for (unsigned int i = 0; i < g_canvasArray.GetCount(); i++) {
    ChartCanvas *gcc = g_canvasArray.Item(i);
    if (!gcc) continue;
    wxSizeEvent sev(gcc->GetSize(), gcc->GetId());
    sev.SetEventObject(gcc);
    gcc->GetEventHandler()->ProcessEvent(sev);
    if (gcc->GetCompass())
      gcc->GetCompass()->SetScaleFactor(g_compass_scalefactor);
    gcc->ReloadVP();
  }
}

// iOS: end of startup, called from the last step of MyFrame::OnInitTimer.
void ocpnios_OnStartupDone() {
  // iOS: iOS never quits an app cleanly (it is suspended and later killed), so
  // the safe mode check file remained and every start offered "Safe Restart".
  // Startup succeeded here: clean up.
#ifdef OCPNIOS_OCPN_MASTER
  safe_mode::MarkStartAsOk();
#else
  safe_mode::clear_check();
#endif
  ocpnios_HideStartupBusy();
  // iOS: iOS never quits the app cleanly (swipe away = hard kill), so OpenCPN
  // never wrote its settings to disk: an enabled plugin or a chosen pilot type
  // was lost after a restart. Save on every transition to the background, as
  // on exit.
  {
    static bool connected = false;
    if (!connected && qGuiApp) {
      connected = true;
      QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, qGuiApp,
                       [](Qt::ApplicationState st) {
                         if (st == Qt::ApplicationActive || !pConfig) return;
                         pConfig->UpdateSettings();
                         pConfig->Flush();
                         wxLogMessage("iOS: settings saved (background)");
                         wxLog::FlushActive();
                       });
    }
  }
}

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
#endif
