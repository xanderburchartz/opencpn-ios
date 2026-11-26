// iOS parts of the OpenCPN main frame (gui/src/ocpn_frame.cpp) and of MyApp
// (gui/src/ocpn_app.cpp): resize handling during rotation, colours of menu bar and
// status bar per colour scheme, end of startup. Members of
// MyFrame are declared in gui/include/gui/ocpn_frame.h; the free functions are
// declared at their call sites. They live here so that the core files only carry
// the hooks that call them. Built once MyFrame declares IosOnSize
// (app/CMakeLists.txt).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <wx/wx.h>
#include <wx/aui/aui.h>

#include <QtWidgets/QWidget>
#include <QGuiApplication>

#include "model/gui_vars.h"  // g_compass_scalefactor
#include "model/safe_mode.h"
#include "model/ais_state_vars.h"  // alarm sound files
#include "model/config_vars.h"     // g_SART_sound_file, gWorldMapLocation
#include "ocpn_platform.h"         // g_Platform
#include "chcanv.h"          // ChartCanvas, g_canvasArray, global_color_scheme
#include "compass.h"
#include "navutil.h"         // pConfig
#include "ocpn_frame.h"
#include "toolbar.h"         // g_MainToolbar
#include "color_scheme.h"    // OcpnIosApplyUiColors (app/ios/color_scheme.mm)

#ifdef __OCPN__IOS__
void ocpnios_HideStartupBusy();  // app/ios/startup_busy.mm
void ocpnios_StatusPanelInit();  // app/ios/status_panel.cpp
void ocpnios_ToolbarInit();      // app/ios/toolbar_ios.cpp
void ocpnios_MapControlsInit();  // app/ios/map_controls_ios.cpp
void ocpnios_CoreOverlaysOff();  // app/ios/map_controls_ios.cpp

// iOS: menu bar, status bar and iOS status bar follow the color scheme, using
// the UI colors from the OpenCPN color table (DILG1 background, UITX1 text).
// Day: light grey with black text, as before (the status bar sits at the bottom
// near the home indicator, where it is black).
// iOS installs every app update (and every TestFlight build) in a new folder, so
// settings that point into the app bundle point to a folder that no longer exists.
// Tide/current data is mapped when the config is read (ocpnios_MapBundleDataPath,
// plugins_ios.cpp); the alarm sounds and the basemap were not: after an update the AIS,
// DSC, SART and anchor alarm sound files pointed to the old bundle (simulator config,
// 26 Nov 2026). Point them into the current bundle again, once, before any alarm plays.
static wxString ocpnios_Rebase(const wxString &path, const wxString &appRoot) {
  if (path.empty() || appRoot.empty() || wxFileExists(path) || wxDirExists(path))
    return path;
  const int i = path.Find(".app/");
  if (i == wxNOT_FOUND) return path;
  const wxString moved = appRoot + path.Mid(i + 5);
  return (wxFileExists(moved) || wxDirExists(moved)) ? moved : path;
}

static void ocpnios_RebaseBundlePaths() {
  static bool done = false;
  if (done || !g_Platform) return;
  done = true;
  const wxString shared = g_Platform->GetSharedDataDir();
  const int i = shared.Find(".app/");
  if (i == wxNOT_FOUND) return;
  const wxString appRoot = shared.Left(i + 5);
  for (wxString *s : {&g_AIS_sound_file, &g_DSC_sound_file, &g_SART_sound_file,
                      &g_anchorwatch_sound_file, &g_sAIS_Alert_Sound_File,
                      &gWorldMapLocation})
    *s = ocpnios_Rebase(*s, appRoot);
}

void ocpnios_ApplySchemeColors(wxFrame *frame) {
  ocpnios_RebaseBundlePaths();
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
  // OpenCPN's zoom bar and compass off before the first chart is drawn (iOS versions in
  // map_controls_ios.cpp).
  ocpnios_CoreOverlaysOff();
  // The iOS status display as soon as the status bar exists, so the desktop status
  // line does not show during startup (it did for ~3 s until ocpnios_OnStartupDone).
  ocpnios_StatusPanelInit();
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
  ocpnios_CoreOverlaysOff();  // Options may have set g_bShowCompassWin again
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

// iOS: place the overlays on the chart again (zoom bar, compass, chart bar, toolbar).
// Called from ApplySafeArea (app/ios/window_fit.cpp) after the safe area or the
// orientation changed. After rotating back to portrait the zoom bar stayed in the
// middle of the screen (Xander, TestFlight build 5): on the device the new safe area
// can arrive after the resize, and MyFrame::IosOnSize skips a repeated size event with
// the same size, so nothing placed the bars again. Only while the app is active:
// resizing the GL canvas in the background crashes in Qt (crash report build 5).
void ocpnios_RelayoutOverlays() {
  if (!gFrame || !qGuiApp) return;
  if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
    // Rotated while in the background: place the bars once the app is active again.
    static bool waiting = false;
    if (!waiting) {
      waiting = true;
      auto *conn = new QMetaObject::Connection;
      *conn = QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, qGuiApp,
                               [conn](Qt::ApplicationState st) {
                                 if (st != Qt::ApplicationActive) return;
                                 QObject::disconnect(*conn);
                                 delete conn;
                                 waiting = false;
                                 ocpnios_RelayoutOverlays();
                               });
    }
    return;
  }
  for (unsigned int i = 0; i < g_canvasArray.GetCount(); i++) {
    ChartCanvas *cc = g_canvasArray.Item(i);
    if (!cc) continue;
    cc->SetMUIBarPosition();
    cc->FormatPianoKeys();
    if (cc->GetCompass()) cc->UpdateGPSCompassStatusBox(true);
    cc->Refresh(false);
  }
  if (g_MainToolbar) {
    g_MainToolbar->SetDefaultPosition();
    g_MainToolbar->Realize();
  }
}

// iOS: screen rectangle of the main toolbar, so a floating plugin window
// (mayara_pi, see app/cmake/ios-plugins.cmake) can leave it reachable. Empty
// when there is no toolbar.
wxRect ocpnios_ToolbarScreenRect() {
  if (!g_MainToolbar || !g_MainToolbar->IsToolbarShown()) return wxRect();
  ChartCanvas *cc = g_canvasArray.GetCount() ? g_canvasArray.Item(0) : nullptr;
  if (!cc) return wxRect();
  wxRect r = g_MainToolbar->GetToolbarRect();
  r.SetPosition(cc->ClientToScreen(r.GetPosition()));
  wxLogDebug("ocpnios: toolbar rect %d,%d %dx%d", r.x, r.y, r.width, r.height);
  return r;
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
  ocpnios_StatusPanelInit();  // status bar / floating box in iOS style
  ocpnios_ToolbarInit();      // iOS toolbar in place of OpenCPN's own
  ocpnios_MapControlsInit();  // iOS zoom bar and compass box
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

#endif
