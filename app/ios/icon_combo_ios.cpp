// OCPNIconCombo for iOS (gui/include/gui/mark_info.h): wxOwnerDrawnComboBox does
// not work in wxQt, so the core declares a wxChoice (QComboBox) with a QIcon per
// item on iOS.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>

#include <wx/wx.h>
#include <wx/choice.h>

#include <QComboBox>
#include <QIcon>

#include "mark_info.h"

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
