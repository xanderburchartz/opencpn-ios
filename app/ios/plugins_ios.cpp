// iOS helpers for the plugin manager, the route manager dialog and the config
// loader. They were iOS-only blocks in gui/src/pluginmanager.cpp,
// gui/src/routemanagerdialog.cpp and gui/src/navutil.cpp; the core now only
// carries the declaration and the call, each inside #ifdef __OCPN__IOS__.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <initializer_list>

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/textwrapper.h>
#include <wx/utils.h>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QVariant>
#include <QWidget>

#include "model/config_vars.h"
#include "navutil.h"
#include "ocpn_platform.h"

// ---------------------------------------------------------------------------
// gui/src/pluginmanager.cpp: plugin list as iOS cards. Behind the card the
// window background; the card itself white (night #1c1c1e), without border,
// also when expanded.
// ---------------------------------------------------------------------------
static bool IosDark() {
  return QCoreApplication::instance() &&
         QCoreApplication::instance()->property("ocpnios_dark").toBool();
}
wxColour ocpnios_PluginPageBg() {
  return IosDark() ? wxColour(0, 0, 0) : wxColour(0xf2, 0xf2, 0xf7);
}
wxColour ocpnios_PluginCardBg() {
  return IosDark() ? wxColour(0x1c, 0x1c, 0x1e) : wxColour(255, 255, 255);
}

namespace {
// Wraps like PanelHardBreakWrapper in pluginmanager.cpp, but measures as Qt
// draws (font after the stylesheet; wx measured about 15% too narrow) and
// breaks a word without spaces that is wider than the line per character
// (e.g. "$STALK,84,36,9D,88,40,00,FF,02,06*02").
class IosPluginWrapper : public wxTextWrapper {
public:
  IosPluginWrapper(wxWindow* win, const wxString& text, int width)
      : m_win(win), m_width(width) {
    Wrap(win, text, width);
  }
  const wxString& GetWrapped() const { return m_wrapped; }

protected:
  void OnOutputLine(const wxString& line) override {
    if (Width(line) <= m_width) {
      m_wrapped += line;
      return;
    }
    wxString part;
    for (wxUniChar c : line) {
      if (!part.empty() && Width(part + c) > m_width) {
        m_wrapped += part + '\n';
        part.clear();
      }
      part += c;
    }
    m_wrapped += part;
  }
  void OnNewLine() override { m_wrapped += '\n'; }

private:
  int Width(const wxString& t) const {
    if (QWidget* w = m_win->GetHandle()) {
      w->ensurePolished();
      return QFontMetrics(w->font()).horizontalAdvance(
          QString::fromUtf8(t.utf8_str().data()));
    }
    return m_win->GetTextExtent(t).x;
  }
  wxWindow* m_win;
  int m_width;
  wxString m_wrapped;
};
}  // namespace

wxString ocpnios_PluginWrap(wxWindow* win, const wxString& text, int width) {
  return IosPluginWrapper(win, text, width).GetWrapped();
}

// ---------------------------------------------------------------------------
// gui/src/routemanagerdialog.cpp: on a phone the list on top and the buttons
// below in two columns.
// ---------------------------------------------------------------------------

// FlexGrid with two growable columns: a wxGridSizer made every cell as wide
// as the widest button ("Send to Peer") and ran off the right edge.
static wxSizer* RmdButtonSizer() {
  auto* fg = new wxFlexGridSizer(0, 2, 0, 0);
  fg->AddGrowableCol(0, 1);
  fg->AddGrowableCol(1, 1);
  fg->SetFlexibleDirection(wxHORIZONTAL);
  return fg;
}

// Button area as tall as the buttons: no scrolling needed.
// Buttons may get narrower than their text, otherwise the widest button
// ("Send to Peer") sets both columns and they fall off the screen.
// dlg is the RouteManagerDialog, margin its DIALOG_MARGIN.
void ocpnios_RmdFitButtons(wxWindow* dlg, int margin, wxScrolledWindow* winr,
                           wxScrolledWindow* wint, wxScrolledWindow* winw,
                           wxScrolledWindow* winl) {
  for (wxScrolledWindow* w : {winr, wint, winw, winl}) {
    if (!w || !w->GetSizer()) continue;
    // The core builds the desktop layout: list and button column side by side,
    // buttons in one column. Stack the panel and move the buttons into a
    // two-column grid.
    if (auto* panel = dynamic_cast<wxBoxSizer*>(w->GetParent()->GetSizer()))
      panel->SetOrientation(wxVERTICAL);
    wxSizer* column = w->GetSizer();
    wxSizer* grid = RmdButtonSizer();
    for (wxSizerItem* it : column->GetChildren()) {
      if (it->IsWindow())
        grid->Add(it->GetWindow(), it->GetProportion(), it->GetFlag(),
                  it->GetBorder());
      else if (it->IsSpacer())
        grid->Add(it->GetSpacer().x, it->GetSpacer().y, it->GetProportion(),
                  it->GetFlag(), it->GetBorder());
    }
    column->Clear(false);
    w->SetSizer(grid);  // deletes the old column sizer
    // Half the screen width minus margins: the space a button has in a column.
    const int colw = dlg->GetParent()->GetClientSize().x / 2 - 6 * margin -
                     2 * dlg->GetCharWidth();
    for (wxWindow* c : w->GetChildren()) {
      auto* b = dynamic_cast<wxButton*>(c);
      if (!b) continue;
      b->SetMinSize(wxSize(10, -1));
      // If the text does not fit, use a slightly smaller font (down to 14 pt) so
      // that e.g. "Send to Peer" stays whole.
      wxString label = wxStripMenuCodes(b->GetLabel());
      wxFont f = b->GetFont();
      while (f.GetPointSize() > 14 && b->GetTextExtent(label).x > colw) {
        f.SetPointSize(f.GetPointSize() - 1);
        b->SetFont(f);
      }
    }
    w->SetMinSize(wxSize(-1, w->GetSizer()->GetMinSize().y));
  }
}

// ---------------------------------------------------------------------------
// gui/src/navutil.cpp: one-time config fixes and path remapping after a
// reinstall (MyConfig::LoadMyConfigRaw, MyConfig::LoadChartDirArray).
// ---------------------------------------------------------------------------

// On the GL path chart text is drawn in physical pixels and is tiny on Retina
// (about 6 pt). So default the text and sounding size sliders to 5 (2x), once,
// so that a later choice by the user is kept.
void ocpnios_ConfigTextScaleDefault(MyConfig* cfg) {
  long done = 0;
  cfg->Read(_T ( "IOSTextScaleDefaultApplied" ), &done, 0L);
  if (!done) {
    g_ENCSoundingScaleFactor = 5;
    g_ENCTextScaleFactor = 5;
    cfg->Write(_T ( "ENCSoundingScaleFactor" ), g_ENCSoundingScaleFactor);
    cfg->Write(_T ( "ENCTextScaleFactor" ), g_ENCTextScaleFactor);
    cfg->Write(_T ( "/Settings/GlobalState/ENCSoundingScaleFactor" ), 5);
    cfg->Write(_T ( "/Settings/GlobalState/ENCTextScaleFactor" ), 5);
    cfg->Write(_T ( "IOSTextScaleDefaultApplied" ), 1L);
    cfg->Flush();
  }
}

// Reset the language to the device language once. A bug in the iOS language
// selection (options.cpp) stored en_US when the User Interface page was visited
// while the app had no translations yet. After that the user's own choice
// applies again.
void ocpnios_ConfigLocaleResetOnce(MyConfig* cfg) {
  long done = 0;
  cfg->Read(_T ( "IOSLocaleResetApplied" ), &done, 0L);
  if (!done) {
    g_locale.Clear();
    g_localeOverride.Clear();
    cfg->Write(_T ( "Locale" ), g_locale);
    cfg->Write(_T ( "LocaleOverride" ), g_localeOverride);
    cfg->Write(_T ( "IOSLocaleResetApplied" ), 1L);
    cfg->Flush();
  }
}

// The app bundle gets a new path on every (re)install. Map a source in the
// bundle (".../opencpn.app/SharedSupport/...") that no longer exists to the
// SharedSupport dir of the current bundle.
void ocpnios_MapBundleDataPath(wxString& val) {
  int pos = val.Find("/SharedSupport/");
  if (pos != wxNOT_FOUND && !wxFileExists(val)) {
    wxString new_path =
        g_Platform->GetSharedDataDir() + val.Mid(pos + wxStrlen("/SharedSupport/"));
    if (wxFileExists(new_path)) val = new_path;
  }
}

// The data container of an iOS app gets a new path on reinstall. A chart dir
// under <old container>/Documents/ then points nowhere; map it to the Documents
// dir of the current container.
void ocpnios_MapContainerChartDir(wxString& dirname, int& nAdjustChartDirs) {
  wxString path = dirname.BeforeFirst('^');
  int pos = path.Find("/Documents/");
  if (pos != wxNOT_FOUND && !wxDirExists(path)) {
    wxString new_path = wxGetHomeDir() + path.Mid(pos);
    if (wxDirExists(new_path)) {
      wxString magic = dirname.AfterFirst('^');
      dirname = magic.IsEmpty() ? new_path : new_path + "^" + magic;
      nAdjustChartDirs++;
    }
  }
}
