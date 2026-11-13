// Fase 1, stap 4: kleinste wxWidgets-app op wxQt voor iOS.
// Toont een frame met menubalk, tekst, invoerveld en twee knoppen. De ene knop telt tikken,
// de andere opent een modale wxMessageBox (het gevoeligste punt op iOS).
// Uitvoer naar stderr met voorvoegsel WXMIN, te volgen met `simctl spawn ... log stream`
// of in de console van Xcode.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/wx.h>

#include <QAccessible>

#include <cstdio>
#include <cstdlib>

namespace {

void Trace(const wxString &msg) {
  std::fprintf(stderr, "WXMIN %s\n", msg.utf8_str().data());
  std::fflush(stderr);
}

// Omweg voor een fout in Qt 6.12.0 op iOS: bij het vernietigen van een knop vraagt
// QIOSPlatformAccessibility::notifyAccessibilityUpdate de rol op van het half afgebroken
// object en crasht (gezien op de iPhone na het sluiten van een wxMessageBox, alleen als
// iOS-toegankelijkheid actief is). Opgelost in qtbase 0142eda401 ("iOS: Don't try to read
// role of destroyed a11y object"), te verwachten in Qt 6.12.1. Tot dan geven we
// ObjectDestroyed niet door aan de iOS-laag; de rest wel.
QAccessible::UpdateHandler g_prevA11yHandler = nullptr;

void FilterA11yUpdate(QAccessibleEvent *event) {
  if (event->type() == QAccessible::ObjectDestroyed) return;
  if (g_prevA11yHandler) {
    g_prevA11yHandler(event);
    return;
  }
  // Geen eerdere handler: dan stuurt Qt zelf door naar het platform. Dat bereiken we door
  // onze handler even weg te halen. Zonder dit gaan alle meldingen verloren en werkt
  // typen in een tekstveld niet meer (gemeten in de simulator, 8 nov 2026).
  QAccessible::installUpdateHandler(nullptr);
  QAccessible::updateAccessibility(event);
  QAccessible::installUpdateHandler(FilterA11yUpdate);
}

void InstallA11yWorkaround() {
  if (std::getenv("WXMIN_NO_A11Y_WORKAROUND")) {
    Trace("a11y-omweg uit (WXMIN_NO_A11Y_WORKAROUND)");
    return;
  }
  if (QT_VERSION < QT_VERSION_CHECK(6, 12, 1))
    g_prevA11yHandler = QAccessible::installUpdateHandler(FilterA11yUpdate);
}

class MinimalFrame : public wxFrame {
public:
  MinimalFrame() : wxFrame(nullptr, wxID_ANY, "wx-minimal op iOS") {
    auto *fileMenu = new wxMenu;
    fileMenu->Append(wxID_ABOUT, "Over");
    fileMenu->Append(wxID_EXIT, "Stoppen");
    auto *menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, "Bestand");
    SetMenuBar(menuBar);

    auto *panel = new wxPanel(this);
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    m_label = new wxStaticText(panel, wxID_ANY,
                               wxString::Format("wxWidgets %s op %s: nog niet getikt",
                                                wxVERSION_NUM_DOT_STRING, wxGetOsDescription()));
    m_text = new wxTextCtrl(panel, wxID_ANY, "");
    m_text->SetHint("Tik hier om te typen");
    auto *tapButton = new wxButton(panel, wxID_ANY, "Tik hier");
    auto *modalButton = new wxButton(panel, wxID_ANY, "Toon melding (modaal)");

    sizer->Add(m_label, 0, wxALL | wxEXPAND, 20);
    sizer->Add(m_text, 0, wxALL | wxEXPAND, 20);
    sizer->Add(tapButton, 0, wxALL | wxEXPAND, 20);
    sizer->Add(modalButton, 0, wxALL | wxEXPAND, 20);
    panel->SetSizer(sizer);

    tapButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      ++m_taps;
      m_label->SetLabel(wxString::Format("Getikt: %d keer", m_taps));
      Trace(wxString::Format("tik %d", m_taps));
    });
    modalButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      Trace("modaal open");
      int rc = wxMessageBox("Dit is een modale wxMessageBox.", "Melding", wxOK | wxCANCEL, this);
      Trace(wxString::Format("modaal dicht, rc=%d", rc));
      m_label->SetLabel(wxString::Format("Melding gesloten (rc=%d)", rc));
    });
    Bind(wxEVT_MENU, [this](wxCommandEvent &) {
      wxMessageBox("wx-minimal, fase 1 van opencpn-ios", "Over", wxOK, this);
    }, wxID_ABOUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      Trace(wxString::Format("grootte %dx%d", e.GetSize().x, e.GetSize().y));
      e.Skip();
    });
  }

private:
  wxStaticText *m_label = nullptr;
  wxTextCtrl *m_text = nullptr;
  int m_taps = 0;
};

class MinimalApp : public wxApp {
public:
  bool OnInit() override {
    if (!wxApp::OnInit()) return false;
    InstallA11yWorkaround();
    Trace(wxString::Format("OnInit, %s", wxGetOsDescription()));
    auto *frame = new MinimalFrame;
    frame->Show(true);
    frame->Maximize(true);
    return true;
  }
  int OnExit() override {
    Trace("OnExit");
    return wxApp::OnExit();
  }
};

}  // namespace

wxIMPLEMENT_APP(MinimalApp);
