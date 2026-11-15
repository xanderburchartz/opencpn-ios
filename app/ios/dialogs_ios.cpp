// iOS parts of smaller OpenCPN dialogs: the About dialog (phone layout, Help opens
// the online manual). These are members of the core classes; they live here so that
// the core files only carry the hooks that call them.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <wx/wx.h>
#include <wx/hyperlink.h>
#include <wx/scrolwin.h>

#include "about_frame_impl.h"
#ifdef OCPNIOS_OCPN_MASTER
#include "manual/manual.h"
#else
#include "manual.h"
#endif
#include "ocpn_platform.h"  // g_Platform

#ifdef __OCPN__IOS__
// iOS: phone layout of the About dialog (called at the end of the constructor).
void AboutFrameImpl::IosPhoneLayout() {
  // - Hide the logo: it took ~40% of the width.
  m_bitmapLogo->Hide();
  // - Paths below their label and relative to the app dir ("~/Library/..."); next
  //   to the label they were truncated. Rows must not stretch (large empty areas).
  const wxString home = wxGetHomeDir();
  for (wxHyperlinkCtrl* link : {m_hyperlinkLogFile, m_hyperlinkIniFile}) {
    wxString path = link->GetURL();
    if (!home.empty() && path.StartsWith(home)) path = "~" + path.Mid(home.length());
    link->SetLabel(path);
    if (auto* row = dynamic_cast<wxBoxSizer*>(link->GetContainingSizer())) {
      row->SetOrientation(wxVERTICAL);
      if (wxSizerItem* it = m_scrolledWindowAbout->GetSizer()->GetItem(row))
        it->SetProportion(0);
    }
  }
  for (wxSizerItem* it : m_scrolledWindowAbout->GetSizer()->GetChildren())
    it->SetProportion(0);
  // - Bottom links: white on grey became white on white (wxQt does not paint the
  //   background behind the link). Use the normal link colour on the window colour
  //   (wxNullColour gave black).
  const wxColour bg = m_scrolledWindowAbout->GetBackgroundColour();
  m_panelMainLinks->SetBackgroundColour(bg);
  for (wxHyperlinkCtrl* link : {m_hyperlinkWebsite, m_hyperlinkHelp, m_hyperlinkLicense}) {
    link->SetNormalColour(wxColour(0, 122, 255));
    link->SetBackgroundColour(bg);
    // Tap target at least 44 pt high and a bit wider than the text ("Help" was
    // 36x20 pt). On iOS wx counts a tap anywhere in the control (hyperlinkg.cpp).
    link->SetMinSize(wxSize(link->GetBestSize().x + 24, 44));
  }
  SetMinSize(wxSize(-1, -1));
  m_scrolledWindowAbout->Layout();
  Layout();
}

// iOS: the local help page only links to opencpn.org, and Help was otherwise
// only in the menu bar, which iOS hides. Open the online manual in Safari.
void AboutFrameImpl::IosLaunchManual() {
  wxString datadir = g_Platform->GetSharedDataDir();
  Manual(this, datadir.ToStdString()).Launch("Toc");
}
#endif
