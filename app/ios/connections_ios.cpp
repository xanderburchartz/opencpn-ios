// iOS parts of dialogs: the Connections page (switch renderer, row layouts,
// "Find sources..." / "Data Monitor" buttons), the Cancel/Done bar of the
// connection editor, the icon combo of the mark dialog, the iOS alert layout of
// OCPNMessageDialog and the two-line messages in the data monitor (TtyScroll).
// ConnectionEditDialog::IosAddNavButtons and OCPNIconCombo are members of core
// classes (gui/include/gui/connection_edit.h, mark_info.h); the rest are free
// ocpnios_* functions declared at their call sites in gui/src/connections_dlg.cpp,
// gui_lib.cpp and tty_scroll.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <algorithm>
#include <string>
#include <vector>

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/grid.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/stockitem.h>
#include <wx/textctrl.h>

#include <QComboBox>
#include <QIcon>
#include <QStyle>
#include <QWidget>

#include "model/comm_util.h"
#include "model/config_vars.h"
#include "model/conn_params.h"
#include "model/gui.h"
#include "model/svg_utils.h"

#include "connection_edit.h"
#include "mark_info.h"
#ifdef OCPNIOS_OCPN_MASTER
#include "observable/evtvar.h"
using obs::EventVar;
#else
#include "observable_evtvar.h"
#endif
#include "ocpn_platform.h"

#ifdef __OCPN__IOS__

std::vector<ConnectionParams*> ocpnios_ScanSources(  // app/ios/source_scan.cpp
    wxWindow* parent, const std::vector<ConnectionParams*>& existing);

// ---------------------------------------------------------------------------
// Connections page (gui/src/connections_dlg.cpp)
// ---------------------------------------------------------------------------

/** iOS: on/off as an iOS switch (same SVGs as the rest of the app). */
class IosSwitchCellRenderer : public wxGridCellRenderer {
public:
  IosSwitchCellRenderer() {
    const std::string dir =
        g_Platform->GetSharedDataDir().ToStdString() + "uidata/ios/";
    m_on = LoadSVG(dir + "switch_on.svg", 51, 31);
    m_off = LoadSVG(dir + "switch_off.svg", 51, 31);
  }
  void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect,
            int row, int col, bool isSelected) override {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
    dc.DrawRectangle(rect);
    const wxBitmap& bm = grid.GetCellValue(row, col) == "1" ? m_on : m_off;
    if (bm.IsOk())
      dc.DrawBitmap(bm, rect.x + (rect.width - bm.GetWidth()) / 2,
                    rect.y + (rect.height - bm.GetHeight()) / 2, true);
  }
  wxSize GetBestSize(wxGrid&, wxGridCellAttr&, wxDC&, int, int) override {
    return wxSize(60, 40);
  }
  IosSwitchCellRenderer* Clone() const override {
    return new IosSwitchCellRenderer();
  }

private:
  wxBitmap m_on, m_off;
};

wxGridCellRenderer* ocpnios_NewSwitchCellRenderer() {
  return new IosSwitchCellRenderer();
}

// iOS: scan for sources (the 5.14 first-start wizard does not run on iOS) and
// open the Data Monitor (it lives in the menu bar, which iOS hides).
// panel is the TopPanel, evt_add_connection its m_evt_add_connection.
void ocpnios_AddSourceButtons(wxWindow* panel, wxSizer* vbox,
                              EventVar& evt_add_connection) {
  auto* scan = new wxButton(panel, wxID_ANY, _("Find sources..."));
  scan->Bind(wxEVT_BUTTON, [panel, &evt_add_connection](wxCommandEvent&) {
    auto added = ocpnios_ScanSources(panel, TheConnectionParams());
    for (ConnectionParams* cp : added) {
#ifdef OCPNIOS_OCPN_MASTER
      cp->is_setup = false;  // start a new stream
#else
      cp->b_IsSetup = false;  // start a new stream
#endif
      TheConnectionParams().push_back(cp);
    }
    if (!added.empty()) {
      UpdateDatastreams();
      evt_add_connection.Notify();
    }
  });
  vbox->Add(scan, wxSizerFlags().Border());
  auto* monitor = new wxButton(panel, wxID_ANY, _("Data Monitor"));
  monitor->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
    if (wxWindow* w = wxWindow::FindWindowByName(kDataMonitorWindowName)) {
      w->Show();
      w->Raise();
    }
  });
  vbox->Add(monitor, wxSizerFlags().Border());
}

// iOS: the switch on its own row, the field on a row below with the value on
// the right (like "Enable Toolbar auto-hide" / "seconds").
// Creates the NmeaFilterRow members checkbox and filter_period.
void ocpnios_NmeaFilterRowLayout(wxPanel* row, wxCheckBox*& checkbox,
                                 wxTextCtrl*& filter_period) {
  auto vbox = new wxBoxSizer(wxVERTICAL);
  checkbox = new wxCheckBox(
      row, wxID_ANY, _("Filter NMEA course and speed data. Filter period: "));
  checkbox->SetValue(g_bfilter_cogsog);
  vbox->Add(checkbox, wxSizerFlags().Expand());
  auto hbox = new wxBoxSizer(wxHORIZONTAL);
  hbox->AddStretchSpacer(1);
  filter_period = new wxTextCtrl(row, wxID_ANY, "", wxDefaultPosition,
                                 wxSize(60, -1), wxTE_RIGHT);
  filter_period->SetValue(std::to_string(g_COGFilterSec));
  hbox->Add(filter_period, wxSizerFlags().Center());
  vbox->Add(hbox, wxSizerFlags().Expand().Border(wxTOP));
  row->SetSizer(vbox);
}

// iOS: two rows, value on the right ("length (bits)" was cut off).
void ocpnios_NetmaskRowLayout(wxPanel* row, wxStaticText* text,
                              wxSpinCtrl* spin, const wxSizerFlags& flags) {
  auto vbox = new wxBoxSizer(wxVERTICAL);
  auto r1 = new wxBoxSizer(wxHORIZONTAL);
  r1->Add(new wxStaticText(row, wxID_ANY, _("Netmask: ")), flags);
  r1->AddStretchSpacer(1);
  r1->Add(text, flags);
  auto r2 = new wxBoxSizer(wxHORIZONTAL);
  r2->Add(new wxStaticText(row, wxID_ANY, _("length (bits): ")), flags);
  r2->AddStretchSpacer(1);
  r2->Add(spin, flags);
  vbox->Add(r1, wxSizerFlags().Expand());
  vbox->Add(r2, wxSizerFlags().Expand());
  row->SetSizer(vbox);
}

// ---------------------------------------------------------------------------
// ConnectionEditDialog (gui/src/connection_edit.cpp)
// ---------------------------------------------------------------------------

// Called from AddOKCancelButtons(), which returns right after.
void ConnectionEditDialog::IosAddNavButtons() {
  // iOS: like an iOS edit screen: "Cancel" top left, "Done" (bold) top right.
  // At the bottom of a long panel users did not find them.
#ifdef OCPNIOS_OCPN_MASTER  // OpenCPN master renamed the button members
  wxButton*& m_btnOK = m_dlg_ok_btn;
  wxButton*& m_btnCancel = m_dlg_cancel_btn;
  if (!m_btnOK) {
    wxBoxSizer* m_btnSizerBox = new wxBoxSizer(wxHORIZONTAL);
#else
  if (!m_btnSizerBox) {
    m_btnSizerBox = new wxBoxSizer(wxHORIZONTAL);
#endif
    m_btnCancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
    m_btnOK = new wxButton(this, wxID_OK, _("Done"));
    wxFont bf = m_btnOK->GetFont();
    bf.SetWeight(wxFONTWEIGHT_BOLD);
    m_btnOK->SetFont(bf);
    m_btnOK->SetDefault();
    // Same style as the title bar of other windows (touch_ui.cpp: ocpnios_nav).
    for (wxButton* b : {m_btnCancel, m_btnOK}) {
      QWidget* q = b->GetHandle();
      q->setProperty("ocpnios_nav", true);
      q->style()->unpolish(q);
      q->style()->polish(q);
      b->InvalidateBestSize();
      b->SetMinSize(wxSize(std::max(64, q->sizeHint().width()),
                           std::max(44, q->sizeHint().height())));
    }
    // Title centred, like an iOS navigation bar ("Cancel | Edit Selected
    // Connection | Done"), instead of as the caption of the box below.
    m_iosNavTitle = new wxStaticText(
        this, wxID_ANY, m_iosPropsLabel, wxDefaultPosition, wxDefaultSize,
        wxALIGN_CENTRE_HORIZONTAL | wxST_ELLIPSIZE_END);
    wxFont tf = m_iosNavTitle->GetFont();
    tf.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    m_iosNavTitle->SetFont(tf);
    m_iosNavTitle->GetHandle()->setProperty("ocpnios_nowrap", true);
    m_btnSizerBox->Add(m_btnCancel, 0, wxALIGN_CENTER_VERTICAL);
    m_btnSizerBox->Add(m_iosNavTitle, 1,
                       wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
    m_btnSizerBox->Add(m_btnOK, 0, wxALIGN_CENTER_VERTICAL);
    GetSizer()->Insert(0, m_btnSizerBox, 0, wxALL | wxEXPAND, 5);
    m_btnOK->Bind(wxEVT_COMMAND_BUTTON_CLICKED,
                  [&](wxCommandEvent& ev) { OnOKClick(); });
    m_btnCancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED,
                      [&](wxCommandEvent& ev) { OnCancelClick(); });
  }
}

// ---------------------------------------------------------------------------
// OCPNIconCombo (gui/include/gui/mark_info.h): a wxChoice (QComboBox) with a
// QIcon per item, because wxOwnerDrawnComboBox does not work in wxQt.
// ---------------------------------------------------------------------------

OCPNIconCombo::OCPNIconCombo(wxWindow* parent, wxWindowID id,
                             const wxString& value, const wxPoint& pos,
                             const wxSize& size, int n,
                             const wxString choices[], long style,
                             const wxValidator& validator, const wxString& name)
    : wxChoice(parent, id, pos, size, n, choices, 0, validator, name) {}

int OCPNIconCombo::Append(const wxString& item, wxBitmap bmp) {
  int idx = wxChoice::Append(item);
  QComboBox* cb = GetQComboBox();
  if (cb && bmp.IsOk() && bmp.GetHandle()) {
    cb->setItemIcon(idx, QIcon(*bmp.GetHandle()));
    const int h = std::max(cb->iconSize().height(), bmp.GetHeight());
    cb->setIconSize(QSize(h, h));
  }
  return idx;
}

void OCPNIconCombo::Clear(void) { wxChoice::Clear(); }

// ---------------------------------------------------------------------------
// OCPNMessageDialog (gui/src/gui_lib.cpp): layout like an iOS alert.
// ---------------------------------------------------------------------------

// iOS: the alert has no title bar (window_fit.cpp, ocpnios_alert), so show the
// title in bold at the top, as in an iOS alert. Otherwise it was lost, e.g. for
// "OpenCPN Route Create".
void ocpnios_AddAlertTitle(wxDialog* dlg, wxSizer* topsizer,
                           const wxString& caption) {
  if (!caption.IsEmpty()) {
    auto* title = new wxStaticText(dlg, wxID_ANY, caption, wxDefaultPosition,
                                   wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    wxFont tf = title->GetFont();
    tf.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    tf.SetPointSize(tf.GetPointSize() + 1);
    title->SetFont(tf);
    topsizer->Add(title, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxTOP, 14);
  }
}

// iOS: buttons as in an iOS alert: side by side, equal width, full width;
// cancel/no on the left, the action on the right. wx stacked Yes and No small
// on top of each other. Frame: window_fit.cpp (ocpnios_alert).
void ocpnios_AddAlertButtons(wxDialog* dlg, wxSizer* topsizer, long style) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto addBtn = [&](wxWindowID id) {
    auto* b = new wxButton(dlg, id, wxGetStockLabel(id, wxSTOCK_NOFLAGS));
    row->Add(b, 1, wxALL | wxEXPAND, 4);
    return b;
  };
  if (style & wxHELP) addBtn(wxID_HELP);
  if (style & wxCANCEL) addBtn(wxID_CANCEL);
  if (style & wxNO) addBtn(wxID_NO);
  wxButton* def = nullptr;
  if (style & wxYES) def = addBtn(wxID_YES);
  if (style & wxOK) def = addBtn(wxID_OK);
  if (def && !(style & wxNO_DEFAULT)) def->SetDefault();
  topsizer->Add(new wxStaticLine(dlg), 0, wxEXPAND | wxTOP, 10);
  topsizer->Add(row, 0, wxEXPAND | wxALL, 6);
  if (dlg->GetHandle()) dlg->GetHandle()->setProperty("ocpnios_alert", true);
}

// ---------------------------------------------------------------------------
// TtyScroll (gui/src/tty_scroll.cpp): data monitor lines.
// ---------------------------------------------------------------------------

// iOS: the message on its own indented line below; next to the source it fell
// off a phone screen. Each log entry is three text lines high (line_height,
// TtyScroll::m_line_height): the source, then the message. Too long for the
// width (an NMEA sentence is ~80 chars): split over two lines, the second
// truncated if needed. Sets data_pos (the caller uses it for m_text_width).
void ocpnios_DrawTtyMessage(wxWindow* win, wxDC& dc, const wxString& ws,
                            int& data_pos, int y, int line_height) {
  data_pos = 2 * win->GetCharWidth();
  const int room = std::max(50, win->GetClientSize().x - data_pos - 4);
  const int row = line_height / 3;
  size_t n = ws.length();
  while (n > 1 && dc.GetTextExtent(ws.Left(n)).GetWidth() > room) n--;
  dc.DrawText(ws.Left(n), data_pos, y + row);
  if (n < ws.length()) {
    wxString rest = ws.Mid(n);
    if (dc.GetTextExtent(rest).GetWidth() > room)
      rest = wxControl::Ellipsize(rest, dc, wxELLIPSIZE_END, room);
    dc.DrawText(rest, data_pos, y + 2 * row);
  }
}

#endif  // __OCPN__IOS__
