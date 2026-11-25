// iOS parts of dialogs: the Connections page (switch renderer, row layouts,
// "Find sources..." / "Data Monitor" buttons), the Cancel/Done bar of the
// connection editor, the iOS alert layout of OCPNMessageDialog and the two-line
// messages in the data monitor (TtyScroll).
// ConnectionEditDialog::IosAddNavButtons is a member of a core class
// (gui/include/gui/connection_edit.h) and is only built when that class
// declares it (OCPNIOS_CONN_EDIT_NAV, app/CMakeLists.txt); OpenCPN master
// rewrote the editor without it. The rest are free ocpnios_* functions declared
// in model/ios_hooks.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <algorithm>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/grid.h>
#include <wx/html/htmlwin.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stockitem.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <map>
#include <set>
#include <wx/statbmp.h>
#include <wx/radiobox.h>
#include <wx/treectrl.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QHeaderView>
#include <QInputMethod>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QScroller>
#include <QTreeView>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QPointer>
#include <QTimer>
#include <QStyle>
#include <QWidget>

QImage ocpnios_SymbolImage(const char* name, int pointSize);  // ios_look.mm

#include "model/comm_util.h"
#include "model/config_vars.h"
#include "model/conn_params.h"
#include "model/conn_states.h"
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
#include "options.h"

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

#ifdef OCPNIOS_OCPN_MASTER
/**
 * OpenCPN master shows status, edit and delete as text symbols (gear, waste
 * basket, circles); iOS draws those as old emoji. Draw the line icons of the
 * 5.14 list instead (data/svg/MUI_flat, also in master).
 */
class IosIconCellRenderer : public wxGridCellRenderer {
  static constexpr int kIconSize = 30;  // points, as the 5.14 list on iOS

public:
  IosIconCellRenderer() {
    const std::string dir =
        g_Platform->GetSharedDataDir().ToStdString() + "uidata/MUI_flat/";
    const int size = kIconSize;
    auto load = [&](const char* name) {
      return LoadSVG(dir + name, size, size);
    };
    m_icons = {
        {wxString::FromUTF8("\u2699"), load("setting_gear.svg")},
        {wxString::FromUTF8("\U0001f5d1"), load("trash_bin.svg")},
        {wxString::FromUTF8("\u25cf"), load("circle-on.svg")},
        {wxString::FromUTF8("\u25c8"), load("circle-off.svg")},
        {wxString::FromUTF8("\u2713"), load("check_mark.svg")},
        {"!", load("exclaim_mark.svg")},
        {"X", load("X_mult.svg")}};
  }
  void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect,
            int row, int col, bool) override {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
    dc.DrawRectangle(rect);
    const wxString value = grid.GetCellValue(row, col);
    for (const auto& [glyph, bm] : m_icons)
      if (value == glyph && bm.IsOk()) {
        dc.DrawBitmap(bm, rect.x + (rect.width - bm.GetWidth()) / 2,
                      rect.y + (rect.height - bm.GetHeight()) / 2, true);
        return;
      }
  }
  wxSize GetBestSize(wxGrid&, wxGridCellAttr&, wxDC&, int, int) override {
    return wxSize(kIconSize + 8, kIconSize + 8);
  }
  IosIconCellRenderer* Clone() const override {
    return new IosIconCellRenderer();
  }

private:
  std::vector<std::pair<wxString, wxBitmap>> m_icons;
};

// Status, edit and delete columns (4-6) of the connections list. Per cell:
// the list sets its column attributes again on every paint.
bool IosIconColumns(wxGrid* grid) {
  bool changed = false;
  for (int row = 0; row < grid->GetNumberRows(); row++)
    for (int col = 4; col <= 6; col++) {
      wxGridCellRenderer* r = grid->GetCellRenderer(row, col);
      const bool ours = dynamic_cast<IosIconCellRenderer*>(r) != nullptr;
      if (r) r->DecRef();
      if (!ours) {
        grid->SetCellRenderer(row, col, new IosIconCellRenderer());
        changed = true;
      }
    }
  return changed;
}
#endif  // OCPNIOS_OCPN_MASTER

// ---------------------------------------------------------------------------
// Connections list as the Wi-Fi list in Settings (Xander, 24 nov 2026): tap a
// row to edit it, swipe it to the left for a red "Delete", a switch on the
// right turns the connection on or off; no gear and no waste basket. The
// list (Connections in gui/src/connections_dlg.cpp) keeps its columns and its
// handlers: a tap is passed on as a click on its edit or enable column, the
// delete button as a click on its delete column.
// ---------------------------------------------------------------------------

namespace {

constexpr int kColEnable = 0, kColText = 1, kColStatus = 4, kColEdit = 5,
              kColDelete = 6;

// The state of a connection as a short grey word on the second line of its
// row, as "Not connected" in the Wi-Fi list. OpenCPN shows it as a symbol in
// a column of its own (filled dot, !, X, check mark) whose meaning is only in
// a tooltip, which iOS does not have (Xander, 24 nov 2026). Same source as
// that column: the driver statistics (ConnStates).
ConnStates& IosConnStates() {
  static ConnStates states;
  return states;
}

wxString StateWord(int row) {
  const auto& conns = TheConnectionParams();
  if (row < 0 || row >= static_cast<int>(conns.size())) return "";
  ConnectionParams* cp = conns[row];
#ifdef OCPNIOS_OCPN_MASTER
  if (!cp->is_enabled) return "";
#else
  if (!cp->bEnabled) return "";
#endif
  switch (IosConnStates().GetDriverState(cp->GetCommProtocol(),
                                         cp->GetStrippedDSPort())) {
    case ConnState::NoData:
      return _("No data");
    case ConnState::Unavailable:
      return _("Unreachable");
    case ConnState::Ok:
      return _("Receiving");
    default:
      return "";
  }
}

/** Name on the first line, details and state in grey on the second. */
class IosRowTextRenderer : public wxGridCellStringRenderer {
public:
  void Draw(wxGrid& grid, wxGridCellAttr& attr, wxDC& dc, const wxRect& rect,
            int row, int col, bool) override {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(attr.GetBackgroundColour()));
    dc.DrawRectangle(rect);
    wxString text = grid.GetCellValue(row, col);
    wxString first = text.BeforeFirst('\n'), second = text.AfterFirst('\n');
    const wxString state = StateWord(row);
    if (!state.empty()) second += second.empty() ? state : "  \u00b7  " + state;
    dc.SetFont(attr.GetFont());
    const int h1 = dc.GetTextExtent(first).y, h2 = dc.GetTextExtent("Ag").y;
    const int top = rect.y + (rect.height - h1 - h2 - 2) / 2;
    dc.SetTextForeground(attr.GetTextColour());
    dc.DrawText(first, rect.x + 4, top);
    dc.SetTextForeground(wxColour(0x8e, 0x8e, 0x93));  // iOS secondary label
    dc.DrawText(second, rect.x + 4, top + h1 + 2);
  }
  IosRowTextRenderer* Clone() const override { return new IosRowTextRenderer(); }
};

// Column order and widths: text | switch (status as text, see above). Checked on every paint:
// the list sets its own widths on every resize (IosLayoutColumns).
void WifiColumns(wxGrid* grid) {
  if (grid->GetNumberCols() < 7) return;
  if (grid->GetColPos(kColEnable) != 6) grid->SetColPos(kColEnable, 6);
  const int w = grid->GetClientSize().x;
  const int sw = grid->GetColSize(kColEnable), st = grid->GetColSize(kColStatus);
  (void)st;
  const int text = std::max(80, w - sw - 2);
  bool changed = false;
  for (int row = 0; row < grid->GetNumberRows(); row++) {
    wxGridCellRenderer* r = grid->GetCellRenderer(row, kColText);
    const bool ours = dynamic_cast<IosRowTextRenderer*>(r) != nullptr;
    if (r) r->DecRef();
    if (!ours) {
      grid->SetCellRenderer(row, kColText, new IosRowTextRenderer());
      changed = true;
    }
  }
  for (int c : {kColStatus, kColEdit, kColDelete})
    if (grid->GetColSize(c) != 0) {
      grid->SetColSize(c, 0);
      changed = true;
    }
  if (w > 200 && grid->GetColSize(kColText) != text) {
    grid->SetColSize(kColText, text);
    changed = true;
  }
  if (changed) grid->CallAfter([grid] { grid->ForceRefresh(); });
}

class WifiListFilter : public QObject {
public:
  explicit WifiListFilter(wxGrid* grid)
      : QObject(grid->GetGridWindow()->GetHandle()), m_grid(grid) {
    QWidget* area = grid->GetGridWindow()->GetHandle();
    // Horizontal swipes are for the rows, not for the page (touch_ui.cpp).
    area->setProperty("ocpnios_hswipe", true);
    area->installEventFilter(this);
    m_delete = new QPushButton(
        QString::fromUtf8(wxGetTranslation("Delete").utf8_str().data()), area);
    m_delete->setStyleSheet(
        "QPushButton { background: #ff3b30; color: white; border: none;"
        " border-radius: 0px; font-weight: 600; padding: 0px; margin: 0px;"
        " min-width: 0px; min-height: 0px; }");
    m_delete->hide();
    QObject::connect(m_delete, &QPushButton::clicked, [this] {
      const int row = m_open;
      CloseDelete();
      if (row >= 0) Click(row, kColDelete);
    });
    // Real clicks (on press, also at the start of a swipe) are not passed on;
    // a tap is sent as a click after the release (see eventFilter).
    grid->Bind(wxEVT_GRID_CELL_LEFT_CLICK, [this](wxGridEvent& ev) {
      if (m_passing) ev.Skip();
    });
  }

protected:
  bool eventFilter(QObject*, QEvent* ev) override {
    switch (ev->type()) {
      case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        m_press = me->position();
        m_swiped = false;
        m_tracking = true;
        if (m_open >= 0 && !m_delete->geometry().contains(m_press.toPoint())) {
          CloseDelete();
          m_tracking = false;  // this tap only closes the button
          return true;
        }
        return false;
      }
      case QEvent::MouseMove: {
        if (!m_tracking || m_swiped) return false;
        const QPointF d = static_cast<QMouseEvent*>(ev)->position() - m_press;
        if (std::abs(d.x()) > 30 && std::abs(d.x()) > 2 * std::abs(d.y())) {
          m_swiped = true;
          if (d.x() < 0) OpenDelete(RowAt(m_press));
          return true;
        }
        return false;
      }
      case QEvent::MouseButtonRelease: {
        if (!m_tracking) return false;
        m_tracking = false;
        const QPointF p = static_cast<QMouseEvent*>(ev)->position();
        const QPointF d = p - m_press;
        if (m_swiped || std::abs(d.x()) > 10 || std::abs(d.y()) > 10) return false;
        const int row = RowAt(p);
        if (row < 0) return false;
        const int col = ColAt(p);
        m_grid->CallAfter([this, row, col] {
          Click(row, col == kColEnable ? kColEnable : kColEdit);
        });
        return false;
      }
      default:
        return false;
    }
  }

private:
  wxGrid* m_grid;
  QPushButton* m_delete;
  QPointF m_press;
  bool m_tracking = false, m_swiped = false, m_passing = false;
  int m_open = -1;

  int RowAt(const QPointF& p) const {
    int x = 0, y = 0;
    m_grid->CalcUnscrolledPosition(int(p.x()), int(p.y()), &x, &y);
    const int row = m_grid->YToRow(y);
    return row >= 0 && row < m_grid->GetNumberRows() ? row : -1;
  }
  int ColAt(const QPointF& p) const {
    int x = 0, y = 0;
    m_grid->CalcUnscrolledPosition(int(p.x()), int(p.y()), &x, &y);
    return m_grid->XToCol(x);
  }
  void Click(int row, int col) {
    wxGridEvent ev(m_grid->GetId(), wxEVT_GRID_CELL_LEFT_CLICK, m_grid, row, col);
    m_passing = true;
    m_grid->GetEventHandler()->ProcessEvent(ev);
    m_passing = false;
  }
  void OpenDelete(int row) {
    if (row < 0) return;
    m_open = row;
    const wxRect r = m_grid->CellToRect(row, kColText);
    int x = 0, y = 0;
    m_grid->CalcScrolledPosition(0, r.y, &x, &y);
    QWidget* area = m_grid->GetGridWindow()->GetHandle();
    const int w = std::min(
        m_delete->fontMetrics().horizontalAdvance(m_delete->text()) + 32,
        area->width() / 2);
    m_delete->setGeometry(area->width() - w, y, w, r.height);
    m_delete->show();
    m_delete->raise();
  }
  void CloseDelete() {
    m_open = -1;
    m_delete->hide();
  }
};

// Thin separators between the rows of the connections list, starting at the
// text, as in an iOS list (the grid lines are off). A transparent layer over
// the grid window; it is repainted when rows or their places change.
class RowLines : public QWidget {
public:
  RowLines(wxGrid* grid, QWidget* parent) : QWidget(parent), m_grid(grid) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setGeometry(parent->rect());
    show();
    parent->installEventFilter(this);
  }

  // Called after each paint of the grid: repaint only when something moved,
  // a repaint of this layer would otherwise paint the grid again.
  void Check() {
    QString sig = QString::number(m_grid->GetNumberRows());
    for (int r = 0; r < m_grid->GetNumberRows(); r++) sig += "," + QString::number(Bottom(r));
    if (sig != m_sig) {
      m_sig = sig;
      update();
    }
  }

protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() == QEvent::Resize && obj == parent()) {
      setGeometry(static_cast<QWidget*>(obj)->rect());
      raise();
    }
    return false;
  }

  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
    p.setPen(QPen(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.5));
    for (int r = 0; r < m_grid->GetNumberRows(); r++) {
      const int y = Bottom(r);
      p.drawLine(QPointF(kInset, y - 0.5), QPointF(width(), y - 0.5));
    }
  }

private:
  static constexpr int kInset = 12;  // where the row text starts
  int Bottom(int row) const {
    int x = 0, y = 0;
    int bottom = 0;  // GetRowBottom is protected
    for (int r = 0; r <= row; r++) bottom += m_grid->GetRowSize(r);
    m_grid->CalcScrolledPosition(0, bottom, &x, &y);
    return y;
  }
  wxGrid* m_grid;
  QString m_sig;
};

void WifiList(wxGrid* grid) {
  if (!grid->GetGridWindow() || !grid->GetGridWindow()->GetHandle()) return;
  new WifiListFilter(grid);
  WifiColumns(grid);
  // A list, not a table: no vertical lines, no blue selected row.
  grid->EnableGridLines(false);
  // The state word follows the driver statistics.
  auto* timer = new wxTimer(grid);
  grid->Bind(wxEVT_TIMER, [grid](wxTimerEvent&) {
    if (grid->IsShownOnScreen()) grid->GetGridWindow()->Refresh();
  }, timer->GetId());
  timer->Start(2000);
  IosConnStates();
  grid->SetSelectionBackground(grid->GetDefaultCellBackgroundColour());
  grid->SetSelectionForeground(grid->GetDefaultCellTextColour());
  auto* lines = new RowLines(grid, grid->GetGridWindow()->GetHandle());
  QPointer<RowLines> lines_guard(lines);
  grid->GetGridWindow()->Bind(wxEVT_PAINT, [grid, lines_guard](wxPaintEvent& ev) {
    ev.Skip();
    WifiColumns(grid);
    if (lines_guard) {
      QPointer<RowLines> g = lines_guard;
      QTimer::singleShot(0, g, [g] {
        if (g) g->Check();
      });
    }
  });
}

// Buttons as rows of an iOS settings list instead of filled blue buttons
// (Xander, 24 Nov 2026): a navigation row has black text and a grey chevron on
// the right, an action row ("Add new connection") blue text after a "+".
// Rows of one card share its rounded corners; between them an inset
// separator, as in the iOS Settings app.
enum class RowKind { kNav, kAdd, kAction, kDestructive };
enum RowPos { kOnly = 0, kFirst = 1, kMiddle = 2, kLast = 3 };

class RowButtonFilter : public QObject {
public:
  RowButtonFilter(QWidget* button, bool chevron, bool separator)
      : QObject(button), m_button(button) {
    const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
    if (chevron) {
      m_chevron = new QLabel(QString::fromUtf8("›"), button);
      QFont f = m_chevron->font();
      f.setPointSizeF(f.pointSizeF() * 1.5);
      m_chevron->setFont(f);
      m_chevron->setStyleSheet(QString("color: %1; background: transparent;")
                                   .arg(dark ? "#5a5a5e" : "#c4c4c7"));
      m_chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    if (separator) {
      m_line = new QWidget(button);
      m_line->setStyleSheet(QString("background: %1;").arg(dark ? "#38383a" : "#c6c6c8"));
      m_line->setAttribute(Qt::WA_TransparentForMouseEvents);
      // A plain QWidget only paints a style sheet background with this flag
      // (the line was missing in the Data Monitor sheet).
      m_line->setAttribute(Qt::WA_StyledBackground);
    }
    // Children added to a button that is already on screen stay hidden until
    // shown (Data Monitor: the separator was missing).
    if (m_chevron) m_chevron->show();
    if (m_line) m_line->show();
    button->installEventFilter(this);
    Place();
  }

protected:
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() == QEvent::Resize || ev->type() == QEvent::Show) Place();
    return false;
  }

private:
  void Place() {
    const QSize s = m_button->size();
    if (m_chevron) {
      m_chevron->adjustSize();
      m_chevron->move(s.width() - 16 - m_chevron->width(),
                      (s.height() - m_chevron->height()) / 2);
      m_chevron->raise();
    }
    if (m_line) {
      m_line->setGeometry(16, s.height() - 1, s.width() - 16, 1);
      m_line->raise();
    }
  }

  QWidget* m_button;
  QLabel* m_chevron = nullptr;
  QWidget* m_line = nullptr;
};

void RowButton(wxButton* b, RowKind kind, int pos, bool separator_below = true) {
  QWidget* q = b->GetHandle();
  if (!q) return;
  wxString label = b->GetLabel();
  if (label.EndsWith("...")) label.RemoveLast(3);
  if (label.EndsWith(wxString::FromUTF8("…"))) label.RemoveLast(1);
  if (kind == RowKind::kAdd) {
    label = "+  " + label;
    // The editor recognises this button by name (ocpnios_ConnEditLayout:
    // "New connection" title), the label no longer matches.
    b->SetName("ocpnios_add_connection");
  }
  b->SetLabel(label);
  const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
  const bool top = pos == kOnly || pos == kFirst;
  const bool bottom = pos == kOnly || pos == kLast;
  q->setStyleSheet(
      QString("QPushButton { background-color: %1; color: %2; border: none;"
              "  border-top-left-radius: %4px; border-top-right-radius: %4px;"
              "  border-bottom-left-radius: %5px; border-bottom-right-radius: %5px;"
              "  text-align: left; padding: 13px 16px; font-weight: normal; }"
              "QPushButton:pressed { background-color: %3; }"
              "QPushButton:disabled { color: %6; }")
          .arg(dark ? "#1c1c1e" : "#ffffff",
               kind == RowKind::kNav ? (dark ? "#ffffff" : "#000000")
               : kind == RowKind::kDestructive ? "#ff3b30" : "#007aff",
               dark ? "#3a3a3c" : "#d1d1d6")
          .arg(top ? 10 : 0)
          .arg(bottom ? 10 : 0)
          .arg(dark ? "#636366" : "#a0a0a5"));
  q->setProperty("ocpnios_rowbutton", true);
  new RowButtonFilter(q, kind == RowKind::kNav, !bottom && separator_below);
  b->InvalidateBestSize();
  // At least an iOS row high (44 pt plus room for the padding); the best size
  // from before the style sheet clipped the text (Data Monitor).
  b->SetMinSize(wxSize(-1, std::max(b->GetBestSize().y, 48)));
}

// The sizer item that holds w, searched from sizer s down.
wxSizerItem* FindItem(wxSizer* s, wxWindow* w) {
  if (!s) return nullptr;
  for (wxSizerItem* it : s->GetChildren()) {
    if (it->GetWindow() == w) return it;
    if (wxSizerItem* in = FindItem(it->GetSizer(), w)) return in;
  }
  return nullptr;
}

wxButton* FindButton(wxWindow* w, const wxString& label) {
  for (wxWindow* c : w->GetChildren()) {
    if (auto* b = dynamic_cast<wxButton*>(c); b && b->GetLabel() == label) return b;
    if (wxButton* in = FindButton(c, label)) return in;
  }
  return nullptr;
}

}  // namespace

namespace {

// The "General" and "Advanced" boxes of the Connections page hold settings
// for all connections, which was not clear (Xander, 24 Nov 2026). As iOS
// groups under one heading "For all connections":
//   - "Output to GPS or autopilot": upload format, magnetic bearing in APB,
//     always send RMB/RMC, talker ID; a grey footer says they only apply to
//     a connection with output;
//   - then the COG/SOG filter, the netmask and the source priorities.
// No "Advanced" toggle: everything is shown. Only places and labels change;
// the controls and what they store are OpenCPN's.
void IosGeneralGroups(wxWindow* panel) {
  if (!panel->GetSizer() || panel->GetHandle()->property("ocpnios_general").toBool()) return;
  wxPanel* general = nullptr;
  wxPanel* advanced = nullptr;
  for (wxWindow* c : panel->GetChildren()) {
    auto* p = dynamic_cast<wxPanel*>(c);
    auto* sb = p ? dynamic_cast<wxStaticBoxSizer*>(p->GetSizer()) : nullptr;
    if (!sb) continue;
    bool radio = false;
    for (wxSizerItem* it : sb->GetChildren())
      if (dynamic_cast<wxRadioBox*>(it->GetWindow())) radio = true;
    if (radio)
      general = p;
    else if (!advanced && sb->GetItemCount() >= 4)
      advanced = p;
  }
  if (!general || !advanced) return;
  panel->GetHandle()->setProperty("ocpnios_general", true);
  auto* gs = static_cast<wxStaticBoxSizer*>(general->GetSizer());
  auto* as = static_cast<wxStaticBoxSizer*>(advanced->GetSizer());
  wxSizer* page = panel->GetSizer();

  // No toggle: hide the "Advanced" row (5.14: text with a triangle; master:
  // text with an expand icon), show the panel.
  const wxString adv1 = wxGetTranslation("Advanced ").Strip(wxString::both);
  const wxString adv2 = wxGetTranslation("Advanced").Strip(wxString::both);
  for (wxSizerItem* it : page->GetChildren()) {
    wxWindow* w = it->GetWindow();
    if (auto* t = dynamic_cast<wxStaticText*>(w)) {
      if (t->GetLabel().StartsWith(adv1) || t->GetLabel().StartsWith(adv2)) t->Hide();
    } else if (wxSizer* row = it->GetSizer()) {
      bool is_adv = false;
      for (wxSizerItem* r : row->GetChildren())
        if (auto* t = dynamic_cast<wxStaticText*>(r->GetWindow()))
          if (t->GetLabel().StartsWith(adv2)) is_adv = true;
      if (is_adv) page->Show(row, false, true);
    }
  }
  advanced->Show();

  // Output settings into the first group, after the upload format.
  std::vector<wxWindow*> rows;
  for (wxSizerItem* it : as->GetChildren())
    if (it->GetWindow()) rows.push_back(it->GetWindow());
  // 0 bearings, 1 RMB/RMC, 2 COG/SOG filter, 3 talker ID, 4 netmask
  if (rows.size() < 5) return;
  wxButton* prio = nullptr;
  for (wxSizerItem* it : gs->GetChildren())
    if (auto* b = dynamic_cast<wxButton*>(it->GetWindow())) prio = b;
  int at = 1;  // after the radio box
  for (wxWindow* w : {rows[0], rows[1], rows[3]}) {
    as->Detach(w);
    w->Reparent(general);
    gs->Insert(at++, w, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
  }
  if (prio) {
    // Its own row under the second card, as "Find sources" / "Data Monitor";
    // it presses OpenCPN's button, which stays (hidden) in its place.
    prio->Hide();
    auto* row = new wxButton(panel, wxID_ANY, prio->GetLabel());
    row->Bind(wxEVT_BUTTON, [prio](wxCommandEvent&) {
      wxCommandEvent click(wxEVT_BUTTON, prio->GetId());
      click.SetEventObject(prio);
      prio->ProcessWindowEvent(click);
    });
    int ai = 0;
    for (wxSizerItem* it : page->GetChildren()) {
      if (it->GetWindow() == advanced) break;
      ++ai;
    }
    page->Insert(ai + 1, row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 15);
    RowButton(row, RowKind::kNav, kOnly);
  }
  // The space under "Furuno GP4X": the radio box keeps no bottom margin.
  for (wxSizerItem* it : gs->GetChildren())
    if (dynamic_cast<wxRadioBox*>(it->GetWindow())) it->SetFlag(it->GetFlag() & ~wxBOTTOM);

  gs->GetStaticBox()->SetLabel(_("Output to GPS or autopilot"));
  as->GetStaticBox()->SetLabel(wxEmptyString);
  // The second card has no heading: no room for one above its rows.
  if (QWidget* q = as->GetStaticBox()->GetHandle())
    q->setStyleSheet(q->styleSheet() + "QGroupBox { margin-top: 0px; padding-top: 0px; }");
  // Rows on the white of the card (the moved panels were grey), the upload
  // choices over the full width ("Algemeen NMEA 018" was cut off), the
  // talker ID field big enough for its two letters.
  const bool dk = QCoreApplication::instance()->property("ocpnios_dark").toBool();
  const wxColour card = dk ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE;
  std::function<void(wxWindow*)> paint = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (dynamic_cast<wxPanel*>(c)) c->SetBackgroundColour(card);
      paint(c);
    }
  };
  paint(general);
  paint(advanced);
  for (wxSizerItem* it : gs->GetChildren())
    if (dynamic_cast<wxRadioBox*>(it->GetWindow())) it->SetFlag(it->GetFlag() | wxEXPAND);
  for (wxWindow* c : rows[3]->GetChildren())
    if (auto* t = dynamic_cast<wxTextCtrl*>(c)) {
      t->SetMinSize(wxSize(64, 40));
      t->SetMaxSize(wxSize(64, 40));
      t->SetSize(64, 40);
      rows[3]->InvalidateBestSize();
    }

  // Heading over both groups, footer under the first one.
  int gi = 0;
  for (wxSizerItem* it : page->GetChildren()) {
    if (it->GetWindow() == general) break;
    ++gi;
  }
  auto* heading = new wxStaticText(panel, wxID_ANY, _("For all connections"));
  wxFont hf = heading->GetFont();
  hf.SetWeight(wxFONTWEIGHT_SEMIBOLD);
  hf.SetPointSize(hf.GetPointSize() * 12 / 10);
  heading->SetFont(hf);
  heading->GetHandle()->setProperty("ocpnios_nowrap", true);
  page->Insert(gi, heading, 0, wxLEFT | wxRIGHT | wxTOP, 20);
  auto* footer = new wxStaticText(panel, wxID_ANY,
                                  _("Only for a connection with output."));
  wxFont ff = footer->GetFont();
  ff.SetPointSize(ff.GetPointSize() * 85 / 100);
  footer->SetFont(ff);
  const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
  footer->SetForegroundColour(dark ? wxColour(0x8d, 0x8d, 0x93) : wxColour(0x6d, 0x6d, 0x72));
  page->Insert(gi + 2, footer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 30);

  general->Layout();
  advanced->Layout();
  panel->Layout();
  if (g_options) g_options->IosFitPage(panel);
  // Every row text on the left edge of the "magnetic bearing" switch text
  // (the talker ID and filter rows are panels with their own margins).
  wxWindow* ref = rows[0];
  std::vector<wxWindow*> align = {rows[2], rows[3], rows[4]};
  panel->CallAfter([panel, ref, align, general, advanced] {
    for (wxWindow* row : align) {
      // Each text on the left of a row (the netmask row has two).
      std::vector<wxWindow*> texts;
      std::function<void(wxWindow*)> find = [&](wxWindow* w) {
        for (wxWindow* c : w->GetChildren()) {
          if (dynamic_cast<wxStaticText*>(c) || dynamic_cast<wxCheckBox*>(c)) {
            if (c->GetPosition().x < row->GetSize().x / 3) texts.push_back(c);
          } else {
            find(c);
          }
        }
      };
      find(row);
      for (wxWindow* t : texts) {
        if (!t->GetContainingSizer()) continue;
        wxSizerItem* it = t->GetContainingSizer()->GetItem(t);
        const int delta = t->GetScreenPosition().x - ref->GetScreenPosition().x;
        if (!it || delta == 0) continue;
        it->SetFlag(it->GetFlag() | wxLEFT);
        it->SetBorder(std::max(0, it->GetBorder() - delta));
      }
      row->InvalidateBestSize();
      row->Layout();
    }
    // Both cards as wide as the page (master adds "Advanced" without wxEXPAND,
    // the card had the width of its rows).
    for (wxWindow* w : {(wxWindow*)general, (wxWindow*)advanced})
      if (wxSizerItem* it = panel->GetSizer()->GetItem(w)) {
        it->SetFlag((it->GetFlag() | wxEXPAND | wxLEFT | wxRIGHT) &
                    ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
        if (w == advanced)
          if (wxSizerItem* g = panel->GetSizer()->GetItem(general)) it->SetBorder(g->GetBorder());
      }
    for (wxWindow* w : {(wxWindow*)general, (wxWindow*)advanced}) {
      w->InvalidateBestSize();
      w->Layout();
    }
    // The page panel grows with the new rows (heading, footer, priorities
    // row); its height was fixed by OpenCPN's Fit() and the last row had
    // no room (0 pt high).
    const int h = panel->GetSizer()->CalcMin().y;
    panel->SetMinSize(wxSize(-1, h));
    panel->SetSize(panel->GetSize().x, h);
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(panel->GetParent()))
      sw->SetVirtualSize(sw->GetVirtualSize().x, h);
    // Qt had the filter period field hidden while wx had it shown (after the
    // rows moved): show what wx shows.
    std::function<void(wxWindow*)> sync = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsShown() && c->GetHandle() && !c->GetHandle()->isVisibleTo(c->GetHandle()->parentWidget()))
          c->GetHandle()->show();
        sync(c);
      }
    };
    sync(advanced);
    sync(general);
    // The filter period field (OpenCPN's NmeaFilterRow) stayed unpainted
    // after the rows moved; hide and show it again so Qt places it anew.
    for (wxWindow* c : align[0]->GetChildren())
      if (auto* t = dynamic_cast<wxTextCtrl*>(c)) {
        t->Hide();
        t->Show();
        if (QWidget* q = t->GetHandle()) {
          q->setGeometry(t->GetPosition().x, t->GetPosition().y, std::max(48, t->GetSize().x),
                         std::max(36, t->GetSize().y));
          q->raise();
          q->update();
        }
      }
    panel->Layout();
    for (wxWindow* p = panel; p; p = p->GetParent()) {
      p->Layout();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(p)) {
        sw->FitInside();
        break;
      }
    }
  });
  for (wxWindow* p = panel; p; p = p->GetParent()) {
    p->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(p)) {
      sw->FitInside();
      break;
    }
  }
}

// The upload format card: the priorities row below the choices had a top
// margin on top of its own row padding, a wide empty band under "Furuno
// GP4X". Without the margin the row follows the choices as in a list.
void IosTightRadioBox(wxWindow* panel) {
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (auto* rb = dynamic_cast<wxRadioBox*>(c)) {
        for (wxWindow* sib : rb->GetParent()->GetChildren())
          if (auto* b = dynamic_cast<wxButton*>(sib))
            if (wxSizerItem* it = FindItem(rb->GetParent()->GetSizer(), b))
              it->SetFlag(it->GetFlag() & ~wxTOP);
        rb->GetParent()->Layout();
      }
      visit(c);
    }
  };
  visit(panel);
}

// The expand icon next to "Advanced" (libs/gui ExpandableIcon: a desktop
// triangle in a square) as an iOS chevron: right when closed, down when open,
// grey as the other row chevrons. OpenCPN still toggles; the picture follows.
void IosExpandChevrons(wxWindow* root) {
  const wxString a1 = wxGetTranslation("Advanced: ").Strip(wxString::both);
  const wxString a2 = wxGetTranslation("Advanced").Strip(wxString::both);
  auto next_to_advanced = [&](wxWindow* icon) {
    for (wxWindow* w = icon; w && w != root; w = w->GetParent()) {
      wxSizer* row = w->GetContainingSizer();
      if (!row) continue;
      for (wxSizerItem* it : row->GetChildren())
        if (auto* t = dynamic_cast<wxStaticText*>(it->GetWindow())) {
          const wxString l = t->GetLabel().Strip(wxString::both);
          if (!l.empty() && (l.StartsWith(a1) || l.StartsWith(a2))) return true;
        }
    }
    return false;
  };
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      auto* bmp = dynamic_cast<wxStaticBitmap*>(c);
      auto* ql = bmp ? qobject_cast<QLabel*>(bmp->GetHandle()) : nullptr;
      if (ql && !ql->property("ocpnios_chevron").toBool() && next_to_advanced(bmp->GetParent())) {
        ql->setProperty("ocpnios_chevron", true);
        auto collapsed = std::make_shared<bool>(true);  // as ExpandableIcon starts
        auto paint = [ql, collapsed] {
          QImage img = ocpnios_SymbolImage(*collapsed ? "chevron.right" : "chevron.down", 15)
                           .convertToFormat(QImage::Format_ARGB32_Premultiplied);
          {
            QPainter tint(&img);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
            tint.fillRect(img.rect(), dark ? QColor(0x5a, 0x5a, 0x5e) : QColor(0xb4, 0xb4, 0xb8));
          }
          QPixmap pm = QPixmap::fromImage(img);
          pm.setDevicePixelRatio(ql->devicePixelRatioF());
          ql->setPixmap(pm);
          ql->setAlignment(Qt::AlignCenter);
          ql->setStyleSheet("background: transparent;");
          ql->setMinimumSize(28, 28);
        };
        bmp->Bind(wxEVT_LEFT_DOWN, [bmp, collapsed, paint](wxMouseEvent& ev) {
          ev.Skip();  // ExpandableIcon::Toggle sets its own bitmap first
          *collapsed = !*collapsed;
          bmp->CallAfter(paint);
        });
        // Room for the chevron: the icon panel had the size of one text line
        // and cut it off; no grey square behind it.
        bmp->SetMinSize(wxSize(28, 28));
        if (wxWindow* icon = bmp->GetParent()) {
          icon->SetMinSize(wxSize(28, 28));
          // wx paints its own panel colour (grey): the colour of the card.
          const bool dk = QCoreApplication::instance()->property("ocpnios_dark").toBool();
          const wxColour card = dk ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE;
          icon->SetBackgroundColour(card);
          bmp->SetBackgroundColour(card);
          if (icon->GetHandle()) {
            icon->GetHandle()->setAutoFillBackground(false);
            icon->GetHandle()->setStyleSheet("background: transparent;");
          }
          for (wxWindow* p = icon->GetParent(); p; p = p->GetParent()) {
            p->Layout();
            if (p->IsTopLevel()) break;
          }
        }
        paint();
      }
      visit(c);
    }
  };
  visit(root);
}

// 5.14: "Advanced ►" as text with a triangle (ShowAdvanced). On iOS a row:
// the word on the left, a chevron on the right (right when closed, down when
// open), as in the master editor. A tap still toggles it in OpenCPN.
void IosAdvancedRow(wxWindow* panel) {
#ifndef OCPNIOS_OCPN_MASTER
  const wxString word = wxGetTranslation("Advanced ").Strip(wxString::both);
  wxStaticText* st = nullptr;
  for (wxWindow* c : panel->GetChildren())
    if (auto* t = dynamic_cast<wxStaticText*>(c); t && t->GetLabel().StartsWith(word))
      st = t;
  if (!st || !st->GetHandle()) return;
  auto* ql = qobject_cast<QLabel*>(st->GetHandle());
  if (!ql) return;
  // The chevron on the page, at its right edge: on the label it moved into
  // the word when OpenCPN made the label narrower on a tap.
  QWidget* page = panel->GetHandle();
  auto* chevron = new QLabel(page);
  chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
  // Open or closed, read from OpenCPN's triangle before it is taken off (a
  // later call sees the bare word).
  auto open = std::make_shared<bool>(false);
  auto restyle = [st, ql, chevron, word, page, open] {
    const wxString l = st->GetLabel();
    if (l.EndsWith(wxString::FromUTF8("\u25bc"))) *open = true;
    if (l.EndsWith(wxString::FromUTF8("\u25ba"))) *open = false;
    if (l != word) st->SetLabel(word);
    ql->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QImage img = ocpnios_SymbolImage(*open ? "chevron.down" : "chevron.right", 15)
                     .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {  // the grey of the other row chevrons
      QPainter tint(&img);
      tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
      const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
      tint.fillRect(img.rect(), dark ? QColor(0x5a, 0x5a, 0x5e) : QColor(0xb4, 0xb4, 0xb8));
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(ql->devicePixelRatioF());
    chevron->setPixmap(pm);
    chevron->adjustSize();
    const QPoint at = ql->mapTo(page, QPoint(0, 0));
    chevron->move(page->width() - chevron->width() - 32,
                  at.y() + (ql->height() - chevron->height()) / 2);
    chevron->show();
    chevron->raise();
  };
  if (wxSizerItem* it = FindItem(panel->GetSizer(), st)) it->SetFlag(it->GetFlag() | wxEXPAND);
  st->SetMinSize(wxSize(-1, 44));
  ql->setIndent(10);
  st->Bind(wxEVT_LEFT_DOWN, [st, restyle](wxMouseEvent& ev) {
    ev.Skip();  // OpenCPN toggles and sets "Advanced ▼/►"
    st->CallAfter(restyle);
  });
  st->Bind(wxEVT_SIZE, [st, restyle](wxSizeEvent& ev) {
    ev.Skip();
    st->CallAfter(restyle);
  });
  st->Bind(wxEVT_MOVE, [st, restyle](wxMoveEvent& ev) {
    ev.Skip();
    st->CallAfter(restyle);
  });
  panel->Layout();
  restyle();
#else
  (void)panel;
#endif
}

// Reorder the sources of the priorities dialog by dragging, as in an iOS list
// in edit mode (Xander, 24 Nov 2026): a grip (three lines) on the right of each
// source that can move, drag it up or down. The move itself is the dialog's
// own Move Up / Move Down (PriorityDlg::ProcessMove), once per place, so the
// stored priorities are the same as on the desktop. The buttons are hidden.
class PrioDragFilter : public QObject {
public:
  PrioDragFilter(wxTreeCtrl* tree, QTreeView* view, wxButton* up, wxButton* down)
      : QObject(view), m_tree(tree), m_view(view), m_up(up), m_down(down) {
    m_overlay = new Overlay(this, view->viewport());
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlay->setGeometry(view->viewport()->rect());
    m_overlay->show();
    view->viewport()->installEventFilter(this);
    view->viewport()->setProperty("ocpnios_grip_right", 0);
    // Sources on two lines: the connection, then sentence and protocol
    // (fit_width.cpp, DirTreeDelegate); on one line the port fell away.
    view->setProperty("ocpnios_prio_sources", true);
    view->doItemsLayout();
    // With Move Up / Move Down hidden the list got the first focus, and on the
    // iPad the keyboard came up (type-to-search). No focus and no text input.
    view->setFocusPolicy(Qt::NoFocus);
    view->setAttribute(Qt::WA_InputMethodEnabled, false);
    view->viewport()->setAttribute(Qt::WA_InputMethodEnabled, false);
    if (view->hasFocus()) view->clearFocus();
    QGuiApplication::inputMethod()->hide();
    // No kinetic scrolling of its own: the list's QScroller held back the
    // press on the grip and then cancelled the touch to scroll. The list fits
    // in the sheet, which scrolls itself. "ocpnios_kinetic" stays set, so
    // touch_ui.cpp does not grab it again.
    QScroller::ungrabGesture(view->viewport());
    view->viewport()->setProperty("ocpnios_kinetic", true);
    FitColumn();
    // The tree's model outlives the overlay while the dialog is destroyed
    // (QTreeModel::clear sends modelReset from ~QTreeView): guard it.
    QPointer<QWidget> overlay(m_overlay);
    auto refresh = [overlay] {
      if (overlay) overlay->update();
    };
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, refresh);
    if (QAbstractItemModel* m = view->model()) {
      QObject::connect(m, &QAbstractItemModel::modelReset, this, refresh);
      QObject::connect(m, &QAbstractItemModel::rowsInserted, this, refresh);
      QObject::connect(m, &QAbstractItemModel::rowsRemoved, this, refresh);
      QObject::connect(m, &QAbstractItemModel::layoutChanged, this, refresh);
      QObject::connect(m, &QAbstractItemModel::dataChanged, this, refresh);
    }
    QObject::connect(view, &QTreeView::expanded, this, refresh);
    QObject::connect(view, &QTreeView::collapsed, this, refresh);
  }

  // Reorder mode, as an iOS list in edit mode (Xander, 24 Nov 2026: while
  // scrolling the long list a source was dragged by accident). Only in this
  // mode the grips show and only the grip drags; otherwise the list scrolls.
  void SetEdit(bool on) {
    m_edit = on;
    m_view->viewport()->setProperty("ocpnios_grip_right", on ? kGripW : 0);
    // The list's own kinetic scroller cancels a drag on the grip: off while
    // editing, on again after.
    if (on)
      QScroller::ungrabGesture(m_view->viewport());
    else
      QScroller::grabGesture(m_view->viewport(), QScroller::LeftMouseButtonGesture);
    FitColumn();
    m_overlay->update();
  }
  bool Edit() const { return m_edit; }

private:
  class Overlay : public QWidget {
  public:
    Overlay(PrioDragFilter* f, QWidget* parent) : QWidget(parent), m_f(f) {}

  protected:
    void paintEvent(QPaintEvent*) override { m_f->Paint(this); }

  private:
    PrioDragFilter* m_f;
  };

  static constexpr int kGripW = 56;  // tap area on the right

  // The text stops before the grip (it ran under it and was cut by it).
  void FitColumn() {
    if (QHeaderView* h = m_view->header()) {
      h->setStretchLastSection(false);
      h->setSectionResizeMode(0, QHeaderView::Fixed);
      h->resizeSection(0, std::max(100, m_view->viewport()->width() - (m_edit ? kGripW - 8 : 0)));
    }
  }

  // A source: under a category, which is under the root "Priorities".
  bool Movable(const QModelIndex& ix) const {
    if (!ix.isValid() || !ix.parent().isValid() || !ix.parent().parent().isValid())
      return false;
    return m_view->model()->rowCount(ix.parent()) > 1;
  }

  void Paint(QWidget* w) {
    QPainter p(w);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = QCoreApplication::instance()->property("ocpnios_dark").toBool();
    const QColor grip = dark ? QColor(0x63, 0x63, 0x66) : QColor(0xc7, 0xc7, 0xcc);
    QAbstractItemModel* m = m_view->model();
    if (!m || !m_edit) return;
    std::function<void(const QModelIndex&)> visit = [&](const QModelIndex& parent) {
      for (int r = 0; r < m->rowCount(parent); r++) {
        const QModelIndex ix = m->index(r, 0, parent);
        if (Movable(ix)) {
          const QRect rc = m_view->visualRect(ix);
          if (rc.isValid() && rc.bottom() >= 0 && rc.top() <= w->height()) {
            if (m_dragging && ix == m_src)
              p.fillRect(QRect(0, rc.top(), w->width(), rc.height()),
                         dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 20));
            const int x = w->width() - 16 - 18, cy = rc.center().y();
            p.setPen(QPen(grip, 2, Qt::SolidLine, Qt::RoundCap));
            for (int k = -1; k <= 1; k++) p.drawLine(x, cy + 6 * k, x + 18, cy + 6 * k);
          }
        }
        if (m_view->isExpanded(ix)) visit(ix);
      }
    };
    visit(QModelIndex());
    if (m_dragging && m_src.isValid()) {
      // Where the source lands: a blue line between the rows.
      const int y = DropLineY();
      p.setPen(QPen(QColor(0x00, 0x7a, 0xff), 3, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(16, y, w->width() - 16, y);
    }
  }

  // New place of the source among its siblings for the finger at m_y.
  int TargetRow() const {
    const QModelIndex parent = m_src.parent();
    const int n = m_view->model()->rowCount(parent);
    int t = 0;
    for (int r = 0; r < n; r++) {
      if (r == m_src.row()) continue;
      const QRect rc = m_view->visualRect(m_view->model()->index(r, 0, parent));
      if (rc.center().y() < m_y) t++;
    }
    return t;
  }

  int DropLineY() const {
    const QModelIndex parent = m_src.parent();
    const int n = m_view->model()->rowCount(parent);
    const int t = TargetRow();
    // The line goes above the row that ends up below the source.
    int below = -1, k = 0;
    for (int r = 0; r < n; r++) {
      if (r == m_src.row()) continue;
      if (k == t) { below = r; break; }
      k++;
    }
    if (below >= 0) return m_view->visualRect(m_view->model()->index(below, 0, parent)).top();
    int last = n - 1 == m_src.row() ? n - 2 : n - 1;
    return m_view->visualRect(m_view->model()->index(last, 0, parent)).bottom();
  }

  bool Press(const QPoint& pos) {
    if (m_dragging) return true;
    if (!m_edit) return false;
    if (pos.x() < m_view->viewport()->width() - kGripW) return false;
    // The grip lies right of the (narrowed) column: find the row at its height.
    const QModelIndex ix = m_view->indexAt(QPoint(20, pos.y()));
    if (!Movable(ix)) return false;
    m_src = ix;
    m_y = pos.y();
    m_dragging = true;
    // A kinetic scroller of the sheet around the list (window_fit.cpp) took
    // the moves of the finger for itself. Off while dragging, on after.
    for (QWidget* p = m_view->parentWidget(); p; p = p->parentWidget())
      if (QScroller::hasScroller(p)) {
        QScroller::scroller(p)->stop();
        QScroller::ungrabGesture(p);
        m_ungrabbed.push_back(p);
      }
    // The moves of the finger did not all reach the list once the press was
    // taken here; follow them for the whole app until the finger is lifted.
    QCoreApplication::instance()->installEventFilter(this);
    m_overlay->update();
    return true;
  }

  void Release(bool apply) {
    QCoreApplication::instance()->removeEventFilter(this);
    for (const QPointer<QWidget>& p : m_ungrabbed)
      if (p) QScroller::grabGesture(p, QScroller::LeftMouseButtonGesture);
    m_ungrabbed.clear();
    m_dragging = false;
    m_overlay->update();
    if (!apply || !m_src.isValid()) return;
    const int from = m_src.row(), to = TargetRow();
    const int category = m_src.parent().row();
    if (to == from) return;
    wxTreeCtrl* tree = m_tree;
    wxButton* btn = to < from ? m_up : m_down;
    const int steps = std::abs(to - from);
    // After the current Qt event: the moves rebuild the tree.
    tree->CallAfter([tree, btn, category, from, steps] {
      Move(tree, btn, category, from, steps);
    });
  }

  bool eventFilter(QObject* obj, QEvent* ev) override {
    // While dragging (also a filter on the application): every move and the
    // lift, wherever Qt sends them, in the coordinates of the list.
    const bool touch = ev->type() == QEvent::TouchUpdate ||
                       ev->type() == QEvent::TouchEnd ||
                       ev->type() == QEvent::TouchCancel;
    // The list accepts touch events (QAbstractItemView): after the press the
    // finger arrives on it as touch updates, not as mouse moves.
    if (m_dragging && (touch || obj != m_view->viewport())) {
      QPoint global;
      bool end = false;
      switch (ev->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
          global = static_cast<QMouseEvent*>(ev)->globalPosition().toPoint();
          end = ev->type() == QEvent::MouseButtonRelease;
          break;
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
          auto* te = static_cast<QTouchEvent*>(ev);
          if (te->points().isEmpty()) return false;
          global = te->points().first().globalPosition().toPoint();
          end = ev->type() == QEvent::TouchEnd;
          break;
        }
        case QEvent::TouchCancel:
          Release(true);  // at the last place the finger was seen
          return false;
        default:
          return false;
      }
      m_y = m_view->viewport()->mapFromGlobal(global).y();
      m_overlay->update();
      if (end) Release(true);
      return true;
    }
    switch (ev->type()) {
      case QEvent::Resize:
        m_overlay->setGeometry(m_view->viewport()->rect());
        FitColumn();
        return false;
      case QEvent::MouseButtonPress:
        // Not taken: with the press taken here no move or lift followed
        // (simulator, 24 Nov 2026). The list selects the row, which is fine.
        Press(static_cast<QMouseEvent*>(ev)->position().toPoint());
        return false;
      case QEvent::MouseMove:
        if (!m_dragging) return false;
        m_y = static_cast<QMouseEvent*>(ev)->position().toPoint().y();
        m_overlay->update();
        return true;
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
        if (!m_dragging) return false;
        Release(ev->type() == QEvent::MouseButtonRelease);
        return true;
      default:
        return false;
    }
  }

  static void Move(wxTreeCtrl* tree, wxButton* btn, int category, int row, int steps) {
    wxTreeItemIdValue c1, c2;
    wxTreeItemId cat = tree->GetFirstChild(tree->GetRootItem(), c1);
    for (int i = 0; i < category && cat.IsOk(); i++) cat = tree->GetNextChild(tree->GetRootItem(), c1);
    if (!cat.IsOk()) return;
    wxTreeItemId leaf = tree->GetFirstChild(cat, c2);
    for (int i = 0; i < row && leaf.IsOk(); i++) leaf = tree->GetNextChild(cat, c2);
    if (!leaf.IsOk()) return;
    // Selecting sends the selection event; the dialog then knows the source
    // (OnItemSelected). Each move rebuilds the tree and selects it again.
    tree->SelectItem(leaf);
    // wxQt does not always report a selection made in code; the dialog needs
    // it (OnItemSelected sets the index that Move Up / Move Down use).
    wxTreeEvent sel(wxEVT_TREE_SEL_CHANGED, tree, leaf);
    tree->GetEventHandler()->ProcessEvent(sel);
    for (int i = 0; i < steps; i++) {
      wxCommandEvent click(wxEVT_BUTTON, btn->GetId());
      click.SetEventObject(btn);
      btn->ProcessWindowEvent(click);
    }
  }

  wxTreeCtrl* m_tree;
  QTreeView* m_view;
  wxButton* m_up;
  wxButton* m_down;
  Overlay* m_overlay;
  QPersistentModelIndex m_src;
  int m_y = 0;
  bool m_dragging = false;
  bool m_edit = false;
  std::vector<QPointer<QWidget>> m_ungrabbed;
};

}  // namespace

// Text buttons of two OpenCPN dialogs that iOS shows as sheets, as rows of an
// iOS list like the Connections page (Xander, 24 Nov 2026): Data Monitor
// (Start logging, Pause; the icon buttons in one row below them) and the
// priorities dialog (Move Up, Move Down, Refresh, Clear All). Called after
// each fit of a sheet (window_fit.cpp, FitSheetContent); once per dialog.
void ocpnios_ActionRows(wxWindow* root,
                        const std::function<int(wxButton*)>& kind);  // fit_width.cpp

void ocpnios_FitToWidth(wxWindow* root);  // fit_width.cpp

// OpenGL Options: headings ("Texture Settings", "Texture Cache", "Miscellaneous") were
// plain text above loose items, a desktop form. As iOS sections: each heading becomes a
// group box (drawn as a white card with a grey header) holding the items below it.
static void IosOpenGLSections(wxWindow* top) {
  const wxString heads[] = {_("Texture Settings"), _("Texture Cache"), _("Miscellaneous")};
  wxFlexGridSizer* fg = nullptr;
  for (wxWindow* c : top->GetChildren())
    if (auto* st = dynamic_cast<wxStaticText*>(c))
      if (st->GetLabel() == heads[0])
        fg = dynamic_cast<wxFlexGridSizer*>(st->GetContainingSizer());
  if (!fg) return;
  std::vector<wxWindow*> wins;
  for (wxSizerItem* it : fg->GetChildren())
    if (it->IsWindow()) wins.push_back(it->GetWindow());
  for (wxWindow* w : wins) fg->Detach(w);
  fg->Clear(true);  // the remaining items are spacers
  wxStaticBoxSizer* box = nullptr;
  for (wxWindow* w : wins) {
    auto* st = dynamic_cast<wxStaticText*>(w);
    const bool head = st && std::find(std::begin(heads), std::end(heads), st->GetLabel()) !=
                                std::end(heads);
    if (head) {
      auto* sb = new wxStaticBox(top, wxID_ANY, st->GetLabel());
      sb->Lower();  // behind the items that lie on top of it
      box = new wxStaticBoxSizer(sb, wxVERTICAL);
      fg->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
      st->Hide();
      continue;
    }
    if (!box) {
      fg->Add(w, 0, wxEXPAND | wxALL, 5);
      continue;
    }
    // A text in the section ("Size: 0.0 MB") on the left like the other rows.
    box->Add(w, 0, (st ? wxALIGN_LEFT : wxEXPAND) | wxLEFT | wxRIGHT, 6);
  }
  top->Layout();
}

void ocpnios_ListButtons(wxWindow* top) {
  if (!top || !top->GetHandle() ||
      top->GetHandle()->property("ocpnios_listbuttons").toBool())
    return;
  // OpenGL Options (Display > Advanced): "Rebuild Texture Cache" and "Clear Texture
  // Cache" were thick filled buttons. As action rows, clearing in red. The dialog title
  // is not translated in OpenCPN, so it identifies the dialog in every language.
  if (auto* tlw = dynamic_cast<wxTopLevelWindow*>(top))
    if (tlw->GetTitle() == "OpenGL Options") {
      top->GetHandle()->setProperty("ocpnios_listbuttons", true);
      // OpenCPN passes the title untranslated; the catalogue has a translation.
      tlw->SetTitle(_("OpenGL Options"));
      IosOpenGLSections(top);
      const wxString clear = _("Clear Texture Cache");
      ocpnios_ActionRows(top, [&](wxButton* b) { return b->GetLabel() == clear ? 2 : 0; });
      ocpnios_FitToWidth(top);
      return;
    }
  std::vector<wxButton*> buttons;
  std::function<void(wxWindow*)> collect = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (auto* b = dynamic_cast<wxButton*>(c)) buttons.push_back(b);
      collect(c);
    }
  };
  collect(top);
  auto find = [&](std::initializer_list<const char*> ids) -> wxButton* {
    for (wxButton* b : buttons)
      for (const char* id : ids)
        if (b->GetLabel() == wxGetTranslation(id)) return b;
    return nullptr;
  };
  auto row_item = [](wxButton* b) {
    if (wxSizer* s = b->GetContainingSizer())
      if (wxSizerItem* it = s->GetItem(b)) {
        it->SetFlag(wxEXPAND);  // as wide as the list above
        it->SetProportion(0);
      }
  };

  wxButton* up = find({"Move Up"});
  wxButton* down = find({"Move Down"});
  wxButton* refresh = find({"Refresh"});
  wxButton* clear = find({"Clear All"});
  if (up && down && refresh && clear) {
    top->GetHandle()->setProperty("ocpnios_listbuttons", true);
    // Reorder by dragging (PrioDragFilter); Move Up / Move Down stay as hidden
    // buttons, the drag presses them.
    wxTreeCtrl* tree = nullptr;
    std::function<void(wxWindow*)> find_tc = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (auto* t = dynamic_cast<wxTreeCtrl*>(c)) tree = t;
        find_tc(c);
      }
    };
    find_tc(top);
    if (tree)
      if (auto* view = qobject_cast<QTreeView*>(tree->GetHandle())) {
        auto* drag = new PrioDragFilter(tree, view, up, down);
        up->Hide();
        down->Hide();
        // "Edit" on the right above the list ("Done" while editing).
        wxSizer* box = tree->GetContainingSizer();
        wxSizer* outer = nullptr;
        std::function<void(wxSizer*)> find_outer = [&](wxSizer* sz) {
          for (wxSizerItem* it : sz->GetChildren())
            if (wxSizer* sub = it->GetSizer()) {
              for (wxSizerItem* k : sub->GetChildren())
                if (k->GetSizer() == box) outer = sz;
              if (!outer) find_outer(sub);
            }
        };
        if (box && top->GetSizer()) find_outer(top->GetSizer());
        if (outer) {
          auto* edit = new wxButton(top, wxID_ANY, _("Reorder"));
          edit->GetHandle()->setStyleSheet(
              "QPushButton { background: transparent; color: #007aff; border: none;"
              " font-size: 17px; padding: 6px 16px; }");
          {  // room for the bold "Done" as well
            QFont bf = edit->GetHandle()->font();
            bf.setBold(true);
            bf.setPixelSize(17);
            auto qs = [](const wxString& t) { return QString::fromUtf8(t.utf8_str().data()); };
            const int w = std::max(QFontMetrics(bf).horizontalAdvance(qs(_("Done"))),
                                   QFontMetrics(bf).horizontalAdvance(qs(_("Reorder")))) + 36;
            edit->SetMinSize(wxSize(w, 40));
          }
          auto* row = new wxBoxSizer(wxHORIZONTAL);
          row->AddStretchSpacer(1);
          row->Add(edit, 0, wxALIGN_CENTER_VERTICAL);
          int idx = 0;
          for (wxSizerItem* it : outer->GetChildren()) {
            bool here = it->GetSizer() && it->GetSizer()->GetItem(box) != nullptr;
            if (here) break;
            ++idx;
          }
          outer->Insert(idx, row, 0, wxEXPAND | wxRIGHT, 10);
          QPointer<QObject> guard(drag);
          edit->Bind(wxEVT_BUTTON, [edit, drag, guard](wxCommandEvent&) {
            if (!guard) return;
            drag->SetEdit(!drag->Edit());
            edit->SetLabel(drag->Edit() ? _("Done") : _("Reorder"));
            QFont f = edit->GetHandle()->font();
            f.setBold(drag->Edit());
            edit->GetHandle()->setFont(f);
          });
          drag->SetEdit(false);  // scrolls as any list
        }
      }
    for (wxButton* b : {refresh, clear}) row_item(b);
    RowButton(refresh, RowKind::kAction, kFirst);
    RowButton(clear, RowKind::kDestructive, kLast);
    top->Layout();
    // Same left and right edge as the card of the priority list (its box).
    wxWindow* box = nullptr;
    std::function<void(wxWindow*)> find_box = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (dynamic_cast<wxStaticBox*>(c)) box = c;
        find_box(c);
      }
    };
    find_box(top);
    if (box) {
      const int inset = box->GetScreenPosition().x - refresh->GetScreenPosition().x;
      if (inset > 0 && inset < 60)
        for (wxButton* b : {refresh, clear})
          if (wxSizerItem* it = b->GetContainingSizer()->GetItem(b)) {
            it->SetFlag(wxEXPAND | wxLEFT | wxRIGHT);
            it->SetBorder(inset);
          }
      top->Layout();
    }
    return;
  }

  wxButton* log = find({"Start logging", "Stop logging"});
  wxButton* pause = find({"Pause", "Resume"});
  if (!log || !pause) return;
  wxSizer* row = log->GetContainingSizer();
  if (!row || pause->GetContainingSizer() != row) return;
  top->GetHandle()->setProperty("ocpnios_listbuttons", true);
  // The icon buttons (copy, filter, menu) share the row; the reflow for small
  // screens had put them under each other.
  std::vector<wxWindow*> icons;
  for (wxSizerItem* it : row->GetChildren())
    if (auto* b = dynamic_cast<wxButton*>(it->GetWindow());
        b && b != log && b != pause && b->GetLabel().empty())
      icons.push_back(b);
  row->Clear(false);  // detaches the buttons, keeps them
  auto* column = new wxBoxSizer(wxVERTICAL);
  auto* card = new wxBoxSizer(wxVERTICAL);
  card->Add(log, wxSizerFlags().Expand());
  card->Add(pause, wxSizerFlags().Expand());
  column->Add(card, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP));
  auto* bar = new wxBoxSizer(wxHORIZONTAL);
  bar->AddStretchSpacer(1);
  for (wxWindow* icon : icons) {
    bar->Add(icon, wxSizerFlags().Center().Border(wxLEFT | wxRIGHT, 20));
  }
  bar->AddStretchSpacer(1);
  column->Add(bar, wxSizerFlags().Expand().Border());
  row->Add(column, wxSizerFlags(1).Expand());
  RowButton(log, RowKind::kAction, kFirst);
  RowButton(pause, RowKind::kAction, kLast);
  top->Layout();
}

// iOS: scan for sources (the 5.14 first-start wizard does not run on iOS) and
// open the Data Monitor (it lives in the menu bar, which iOS hides).
// panel is the TopPanel, evt_add_connection its m_evt_add_connection.
void ocpnios_AddSourceButtons(wxWindow* panel, wxSizer* vbox,
                              EventVar& evt_add_connection) {
  for (wxWindow* c : panel->GetChildren())
    if (auto* grid = dynamic_cast<wxGrid*>(c)) WifiList(grid);
#ifdef OCPNIOS_OCPN_MASTER
  for (wxWindow* c : panel->GetChildren())
    if (auto* grid = dynamic_cast<wxGrid*>(c)) {
      IosIconColumns(grid);
      // The grid paints in its grid window; new rows get the renderer there
      // and are drawn once more.
      grid->GetGridWindow()->Bind(wxEVT_PAINT, [grid](wxPaintEvent& ev) {
        ev.Skip();
        if (IosIconColumns(grid)) grid->CallAfter([grid] { grid->Refresh(); });
      });
    }
#endif
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
  auto* monitor = new wxButton(panel, wxID_ANY, _("Data Monitor"));
  monitor->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
    if (wxWindow* w = wxWindow::FindWindowByName(kDataMonitorWindowName)) {
      w->Show();
      w->Raise();
    }
  });
  // "Find sources" and "Data Monitor" as two rows of one card.
  auto* card = new wxBoxSizer(wxVERTICAL);
  card->Add(scan, wxSizerFlags().Expand());
  card->Add(monitor, wxSizerFlags().Expand());
  vbox->AddSpacer(panel->GetCharHeight());
  vbox->Add(card, wxSizerFlags().Border(wxLEFT | wxRIGHT).Expand());
  RowButton(scan, RowKind::kNav, kFirst);
  RowButton(monitor, RowKind::kNav, kLast);

  // "Add new connection" as the last row under the connections, in the same
  // card: no gap between the list and the row.
  wxGrid* list = nullptr;
  for (wxWindow* c : panel->GetChildren())
    if (auto* g = dynamic_cast<wxGrid*>(c)) list = g;
  if (wxButton* add = FindButton(panel, wxGetTranslation("Add new connection..."))) {
    wxSizerItem* list_item = list ? FindItem(vbox, list) : nullptr;
    if (wxSizerItem* it = FindItem(vbox, add)) {
      it->SetFlag(wxEXPAND | wxLEFT | wxRIGHT);
      if (list_item) it->SetBorder(list_item->GetBorder());
    }
    if (list_item) list_item->SetFlag(wxEXPAND | wxLEFT | wxRIGHT | wxTOP);
    // Square corners under the list, which has square corners itself.
    RowButton(add, RowKind::kAdd, list ? kMiddle : kOnly, !list);
  }
  // "Adjust Nav data priorities" in the General card: created after this
  // function returns.
  panel->CallAfter([panel] {
    IosGeneralGroups(panel);
    wxButton* prio =
        FindButton(panel, wxGetTranslation("Adjust Nav data priorities..."));
    if (!prio || !prio->IsShown()) return;
    if (wxSizerItem* it = FindItem(prio->GetParent()->GetSizer(), prio))
      it->SetFlag(it->GetFlag() | wxEXPAND);
    RowButton(prio, RowKind::kNav, kOnly);
    panel->Layout();
  });
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

#ifdef OCPNIOS_CONN_EDIT_NAV  // app/CMakeLists.txt
// ---------------------------------------------------------------------------
// ConnectionEditDialog (gui/src/connection_edit.cpp)
// ---------------------------------------------------------------------------

void ocpnios_FitToWidth(wxWindow* root);  // fit_width.cpp

namespace {

// The kind of network connection as one segmented control "TCP | UDP | GPSD |
// Signal K", as the one row of radio buttons on the desktop. The 5.14 editor
// puts GPSD and Signal K on a second row in portrait, which read as a second,
// separate choice (Xander, 24 nov 2026). The label goes above the row so the
// four segments get the full width. Only the layout changes; the buttons and
// their handlers (OnNetProtocolSelected, default ports) stay the same.
// "< Back" of the filter page per editor (the Cancel button handler asks it
// first; no member for it in the core header).
std::map<wxWindow*, std::function<bool()>>& IosFilterBack() {
  static std::map<wxWindow*, std::function<bool()>> m;
  return m;
}

// State of the "Filter sentences" page of the connection editor (see
// ConnectionEditDialog::IosAddNavButtons).
struct IosFilterPage {
  wxButton* row = nullptr;
  bool sub = false;
  bool want_in = false, want_out = false;
  wxString title, cancel, ok;
  std::vector<wxWeakRef<wxWindow>> hidden;
  std::function<void()> post;
  std::function<bool()> back;
};

void IosOneProtocolRow(wxWindow* dlg, wxStaticText* label, wxRadioButton* tcp,
                       wxRadioButton* udp, wxRadioButton* gpsd,
                       wxRadioButton* signalk, wxGridSizer* grid,
                       wxSizer* box, wxWindow* data_label) {
  if (!label || !tcp || !udp || !gpsd || !signalk || !grid || !box) return;
  wxSizer* row = tcp->GetContainingSizer();
  wxSizer* row2 = gpsd->GetContainingSizer();
  if (!row) return;
  if (row2 && row2 != row) {
    row2->Detach(gpsd);
    row2->Detach(signalk);
    row->Add(gpsd, 0, wxALL, 5);
    row->Add(signalk, 0, wxALL, 5);
  }
  // Out of the two-column grid: label, row, and the spacers before "Data
  // Protocol" (the grid keeps its pairs).
  grid->Detach(label);
  grid->Detach(row);
  if (row2 && row2 != row) grid->Remove(row2);  // empty now; deletes it
  while (!grid->GetChildren().empty()) {
    wxSizerItem* first = grid->GetChildren().front();
    if (!first->IsSpacer()) break;
    grid->Remove(0);
  }
  auto* col = new wxBoxSizer(wxVERTICAL);
  col->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
  col->Add(row, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);
  int idx = 0;
  for (wxSizerItem* it : box->GetChildren()) {
    if (it->GetSizer() == grid) break;
    ++idx;
  }
  box->Insert(idx, col, 0, wxEXPAND | wxALL, 5);
  (void)data_label;
  // The rest of the grid (data protocol, address, port) in one column, label
  // above its field, as in an iOS form and the master editor. Next to each
  // other the address field covered its label, and the empty spacer rows
  // between the pairs left large gaps.
  if (auto* fg = dynamic_cast<wxFlexGridSizer*>(grid)) {
    std::vector<wxWindow*> wins;
    for (wxSizerItem* it : fg->GetChildren())
      if (it->GetWindow()) wins.push_back(it->GetWindow());
    if (wins.size() % 2 == 0) {
      fg->Clear(false);  // only spacers are deleted
      fg->SetCols(1);
      if (fg->IsColGrowable(1)) fg->RemoveGrowableCol(1);
      if (!fg->IsColGrowable(0)) fg->AddGrowableCol(0);
      for (size_t i = 0; i < wins.size(); i += 2) {
        wins[i]->SetMinSize(wxDefaultSize);
        wins[i + 1]->SetMinSize(wxSize(8 * wins[i + 1]->GetCharWidth(), -1));
        wins[i + 1]->SetMaxSize(wxDefaultSize);
        fg->Add(wins[i], 0, wxLEFT | wxRIGHT | wxTOP, 5);
        fg->Add(wins[i + 1], 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
      }
    }
  }
  dlg->Layout();
}

// The settings form of the editor came out with large empty bands: the
// section-grid look (fit_width.cpp, ocpnios_SectionGrids) gives a form grid
// 16 between its rows and 28 above and below, and the protocol buttons 16
// above and below. For a label above its field that left gaps under the
// segment bar and between each label and its field. Close them up.
void IosTightenForm(wxSizer* box, wxSizer* grid, wxSizer* proto_row) {
  if (auto* fg = dynamic_cast<wxFlexGridSizer*>(grid)) fg->SetVGap(0);
  if (box)
    for (wxSizerItem* it : box->GetChildren())
      if (it->GetSizer() == grid) it->SetBorder(5);
  if (proto_row)
    for (wxSizerItem* it : proto_row->GetChildren())
      if (it->GetWindow()) {
        it->SetFlag(it->GetFlag() & ~(wxTOP | wxBOTTOM));
      }
}

// A two-column grid of label | field (and switch | spacer) pairs in one
// column: label above a full-width field, a switch over the full width (its
// text was squeezed into the narrow first column and wrapped or clipped).
void IosOneColumn(wxSizer* sz) {
  auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
  if (!fg || fg->GetCols() != 2) return;
  // Windows and sub-sizers in order; spacers (empty second cells) dropped.
  struct Cell {
    wxWindow* w;
    wxSizer* s;
    int flag, border, prop;
  };
  std::vector<Cell> cells;
  for (wxSizerItem* it : fg->GetChildren())
    if (it->GetWindow() || it->GetSizer())
      cells.push_back({it->GetWindow(), it->GetSizer(), it->GetFlag(),
                       it->GetBorder(), it->GetProportion()});
  for (const Cell& c : cells) {
    if (c.w) fg->Detach(c.w);
    else fg->Detach(c.s);
  }
  fg->Clear(false);  // the spacers
  fg->SetCols(1);
  if (fg->IsColGrowable(1)) fg->RemoveGrowableCol(1);
  if (!fg->IsColGrowable(0)) fg->AddGrowableCol(0);
  for (size_t i = 0; i < cells.size(); i++) {
    const Cell& c = cells[i];
    if (c.s) {
      fg->Add(c.s, c.prop, c.flag, c.border);
      continue;
    }
    auto* label = dynamic_cast<wxStaticText*>(c.w);
    wxWindow* next = i + 1 < cells.size() ? cells[i + 1].w : nullptr;
    if (label && label->GetLabel().empty()) continue;  // an empty filler cell
    if (label && next && !dynamic_cast<wxStaticText*>(next)) {
      // label above its field
      label->SetMinSize(wxDefaultSize);
      next->SetMinSize(wxSize(8 * next->GetCharWidth(), -1));
      next->SetMaxSize(wxDefaultSize);
      fg->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
      fg->Add(next, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
      i++;
      continue;
    }
    if (auto* cb = dynamic_cast<wxCheckBox*>(c.w)) {
      // Its text had been wrapped for the narrow first column (options_ios.cpp,
      // IosFitPage); the row layout wraps it again for the full width.
      if (cb->GetName().StartsWith("ocpnios:")) cb->SetLabel(cb->GetName().Mid(8));
      cb->SetMinSize(wxDefaultSize);
      cb->InvalidateBestSize();
      fg->Add(cb, 0, wxEXPAND | wxALL, 5);
      continue;
    }
    fg->Add(c.w, c.prop, c.flag, c.border);
  }
}

const char* const kFieldNoteName = "ocpnios_fieldnote";

// Lay out from the panel of the field up to the editor, so the card grows or
// shrinks with the note, and the page scrolls over the new height.
void RelayoutUp(wxWindow* from, wxWindow* dlg) {
  for (wxWindow* w = from; w; w = w->GetParent()) {
    w->InvalidateBestSize();
    w->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(w)) sw->FitInside();
    if (w == dlg) break;
  }
  if (wxWindow* page = dlg->GetParent()) {
    page->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
  }
}

// Remove the red note under a field (see ShowFieldNote), if there is one.
void ClearFieldNote(wxWindow* dlg) {
  wxWindow* note = dlg->FindWindow(kFieldNoteName);
  if (!note) return;
  if (wxSizer* sz = note->GetContainingSizer()) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    int idx = 0;
    for (wxSizerItem* it : sz->GetChildren()) {
      if (it->GetWindow() == note) break;
      ++idx;
    }
    sz->Detach(note);
    if (fg && fg->GetCols() == 2 && idx > 0) sz->Remove(idx - 1);
  }
  wxWindow* parent = note->GetParent();
  note->Destroy();
  RelayoutUp(parent, dlg);
}

// A red note under a field, as in an iOS form, instead of saving nothing.
void ShowFieldNote(wxWindow* dlg, wxWindow* field, const wxString& text) {
  ClearFieldNote(dlg);
  wxSizer* sz = field->GetContainingSizer();
  if (!sz) return;
  int idx = 0;
  for (wxSizerItem* it : sz->GetChildren()) {
    if (it->GetWindow() == field) break;
    ++idx;
  }
  auto* note = new wxStaticText(field->GetParent(), wxID_ANY, text);
  note->SetName(kFieldNoteName);
  note->SetForegroundColour(wxColour(0xff, 0x3b, 0x30));  // iOS red
  auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
  if (fg && fg->GetCols() == 2) sz->Insert(++idx, 0, 0);  // label column
  sz->Insert(idx + 1, note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
  RelayoutUp(field->GetParent(), dlg);
  // The 5.14 iOS layout moves the address field to the left edge; the note
  // follows the field, not the grid column.
  note->Move(field->GetPosition().x + 5, note->GetPosition().y);
}

}  // namespace

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
#ifndef OCPNIOS_OCPN_MASTER
    IosOneProtocolRow(this, m_stNetProto, m_rbNetProtoTCP, m_rbNetProtoUDP,
                      m_rbNetProtoGPSD, m_rbNetProtoSignalK, gSizerNetProps,
                      sbSizerConnectionProps, m_stNetDataProtocol);
    if (m_tNetComment) IosOneColumn(m_tNetComment->GetContainingSizer());
    if (m_cbInput) IosOneColumn(m_cbInput->GetContainingSizer());
    Layout();
    // The switches are now on rows of their own: as iOS rows (text left,
    // switch right), as on the other settings pages (fit_width.cpp).
    ocpnios_FitToWidth(this);
    IosTightenForm(sbSizerConnectionProps, gSizerNetProps,
                   m_rbNetProtoTCP ? m_rbNetProtoTCP->GetContainingSizer() : nullptr);
    Layout();
    Bind(wxEVT_SHOW, [this](wxShowEvent& ev) {
      ev.Skip();
      if (!ev.IsShown()) return;
      CallAfter([this] {
        IosTightenForm(sbSizerConnectionProps, gSizerNetProps,
                       m_rbNetProtoTCP ? m_rbNetProtoTCP->GetContainingSizer() : nullptr);
        Layout();
      });
    });
    // --- No "Advanced" toggle (Xander, 24 Nov 2026) -------------------------
    // OpenCPN hides some fields behind "Advanced". On UDP the toggle seemed to
    // do nothing: the panel grew, but the page around it kept its scroll range,
    // so the new fields could not be reached. On TCP the row was missing after
    // GPSD, Signal K or the internal GPS had been shown: those call
    // m_collapse_box->Show(false), TCP only ShowItems(true), and the row itself
    // stayed hidden. iOS: always "advanced", no toggle. The fields follow the
    // connection: the Signal K token in the form, the output options when
    // output is on, and the sentence filters on a page of their own
    // ("Filter sentences >").
    m_advanced = true;
    auto st = std::make_shared<IosFilterPage>();
    st->row = new wxButton(this, wxID_ANY, _("Filter sentences"));
    {
      int idx = 0;
      for (wxSizerItem* it : sbSizerConnectionProps->GetChildren()) {
        if (it->GetSizer() == sbSizerInFilter) break;
        ++idx;
      }
      sbSizerConnectionProps->Insert(idx, st->row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
    }
    RowButton(st->row, RowKind::kNav, kOnly);
    // The filter page as an iOS form: the two choices under each other (side
    // by side "Behalve deze" was cut off), the sentence field over the full
    // width, and "Edit" as a blue row instead of a "..." button.
    for (wxRadioButton* rb : {m_rbIAccept, m_rbOAccept})
      if (auto* bs = dynamic_cast<wxBoxSizer*>(rb->GetContainingSizer())) bs->SetOrientation(wxVERTICAL);
    // Both filter cards with the same edges (the input one had a border, the
    // output one none), and every row of them on one left edge.
    for (wxSizerItem* it : sbSizerConnectionProps->GetChildren())
      if (it->GetSizer() == sbSizerInFilter || it->GetSizer() == sbSizerOutFilter) {
        it->SetFlag(wxEXPAND | wxLEFT | wxRIGHT | wxTOP);
        it->SetBorder(5);
      }
    for (wxWindow* w : {(wxWindow*)m_rbIAccept, (wxWindow*)m_rbIIgnore, (wxWindow*)m_tcInputStc,
                        (wxWindow*)m_btnInputStcList, (wxWindow*)m_rbOAccept, (wxWindow*)m_rbOIgnore,
                        (wxWindow*)m_tcOutputStc, (wxWindow*)m_btnOutputStcList})
      if (wxSizerItem* it = w->GetContainingSizer()->GetItem(w)) {
        it->SetFlag((it->GetFlag() & ~wxALL) | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM);
        it->SetBorder(5);
      }
    for (wxButton* b : {m_btnInputStcList, m_btnOutputStcList}) {
      auto* bs = dynamic_cast<wxBoxSizer*>(b->GetContainingSizer());
      if (!bs) continue;
      bs->SetOrientation(wxVERTICAL);
      for (int i = (int)bs->GetItemCount() - 1; i >= 0; i--)
        if (bs->GetItem((size_t)i)->IsSpacer()) bs->Remove(i);
      for (wxSizerItem* it : bs->GetChildren()) it->SetFlag(it->GetFlag() | wxEXPAND);
      b->SetLabel(_("Edit"));
      RowButton(b, RowKind::kAction, kOnly);
    }
    st->post = [this, st] {
      const bool net = m_rbTypeNet && m_rbTypeNet->GetValue();
      const bool tcpudp = net && (m_rbNetProtoTCP->GetValue() || m_rbNetProtoUDP->GetValue());
      const bool n0183 = (DataProtocol)m_choiceNetDataProtocol->GetSelection() ==
                         DataProtocol::PROTO_NMEA0183;
      const bool gpsd = net && m_rbNetProtoGPSD->GetValue();
      st->want_in = (tcpudp && n0183 && m_cbInput->IsChecked()) || gpsd;
      st->want_out = tcpudp && n0183 && m_cbOutput->IsChecked();
      m_collapse_box->ShowItems(false);
      if (m_StaticTextSKServerStatus && m_StaticTextSKServerStatus->GetLabel().empty())
        m_StaticTextSKServerStatus->Hide();
      if (st->sub) {
        ShowInFilter(st->want_in);
        ShowOutFilter(st->want_out);
        st->row->Hide();
      } else {
        ShowInFilter(false);
        ShowOutFilter(false);
        st->row->Show(st->want_in || st->want_out);
        // Output options only with output on, as rows.
        m_stPrecision->Show(st->want_out);
        m_choicePrecision->Show(st->want_out);
      }
      // Data protocol and APB precision: label above, the choice as a full
      // field like the address (as a value on the right of the label it was
      // cut off: "NMEA 01...").
      for (wxChoice* c : {m_choiceNetDataProtocol, m_choicePrecision}) {
        QWidget* q = c ? c->GetHandle() : nullptr;
        if (!q || !q->property("ocpnios_formvalue").toBool()) continue;
        q->setProperty("ocpnios_formvalue", false);
        q->style()->unpolish(q);
        q->style()->polish(q);
        c->SetMaxSize(wxDefaultSize);
        if (wxSizerItem* it = c->GetContainingSizer()->GetItem(c))
          it->SetFlag((it->GetFlag() | wxEXPAND) & ~(wxALIGN_RIGHT | wxALIGN_CENTER_HORIZONTAL));
      }
      Layout();
      RelayoutUp(this, this);
    };
    auto later = [this, st](wxCommandEvent& ev) {
      ev.Skip();
      CallAfter([st] { st->post(); });
    };
    Bind(wxEVT_CHECKBOX, later);
    Bind(wxEVT_RADIOBUTTON, later);
    Bind(wxEVT_CHOICE, later);
    Bind(wxEVT_SHOW, [this, st](wxShowEvent& ev) {
      ev.Skip();
      if (ev.IsShown()) CallAfter([st] { st->post(); });
    });
    // The page and back.
    auto enter = [this, st] {
      st->sub = true;
      st->title = m_iosNavTitle->GetLabel();
      st->cancel = m_btnCancel->GetLabel();
      std::set<wxWindow*> keep = {m_btnCancel, m_btnOK, m_iosNavTitle, m_sbConnEdit,
                                  sbSizerInFilter->GetStaticBox(),
                                  sbSizerOutFilter->GetStaticBox(),
                                  m_rbIAccept, m_rbIIgnore, m_tcInputStc, m_btnInputStcList,
                                  m_rbOAccept, m_rbOIgnore, m_tcOutputStc, m_btnOutputStcList};
      st->hidden.clear();
      for (wxWindow* c : GetChildren())
        if (c->IsShown() && !keep.count(c)) {
          c->Hide();
          st->hidden.push_back(c);
        }
      m_iosNavTitle->SetLabel(_("Filter sentences"));
      m_btnCancel->SetLabel(wxString::FromUTF8("\u2039 ") + _("Back"));
      // Done stays in place (empty) so the title stays centred.
      st->ok = m_btnOK->GetLabel();
      m_btnOK->SetLabel(wxEmptyString);
      m_btnOK->Disable();
      st->post();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(GetParent())) sw->Scroll(0, 0);
    };
    st->row->Bind(wxEVT_BUTTON, [this, enter](wxCommandEvent&) { CallAfter(enter); });
    st->back = [this, st]() -> bool {
      if (!st->sub) return false;
      st->sub = false;
      for (const wxWeakRef<wxWindow>& w : st->hidden)
        if (w) w->Show();
      st->hidden.clear();
      m_iosNavTitle->SetLabel(st->title);
      m_btnCancel->SetLabel(st->cancel);
      m_btnOK->SetLabel(st->ok);
      m_btnOK->Enable();
      // OpenCPN decides again what this connection shows.
      if (m_rbTypeNet->GetValue())
        SetNMEAFormForNetProtocol();
      else
        SetNMEAFormForSerialProtocol();
      st->post();
      return true;
    };
    IosFilterBack()[this] = [st] { return st->back(); };
    Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent& ev) {
      ev.Skip();
      if (ev.GetEventObject() == this) IosFilterBack().erase(this);
    });
    CallAfter([st] { st->post(); });
#endif
    // A network connection without address or port was dropped without a
    // word when "Done" was tapped (ConnectionParams::GetValidPort; OpenCPN
    // only posts a notification). Say so under the field and stay here.
    m_btnOK->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& ev) {
#ifndef OCPNIOS_OCPN_MASTER
      if (m_rbTypeNet && m_rbTypeNet->GetValue()) {
        if (m_tNetAddress && m_tNetAddress->GetValue().Strip(wxString::both).empty()) {
          ShowFieldNote(this, m_tNetAddress,
                        wxGetTranslation("Required field address is missing"));
          return;
        }
        if (m_tNetPort && wxAtoi(m_tNetPort->GetValue()) <= 0) {
          ShowFieldNote(this, m_tNetPort,
                        wxGetTranslation("Required field port is missing"));
          return;
        }
      }
#endif
      OnOKClick();
    });
#ifndef OCPNIOS_OCPN_MASTER
    for (wxTextCtrl* t : {m_tNetAddress, m_tNetPort})
      if (t)
        t->Bind(wxEVT_TEXT, [this](wxCommandEvent& ev) {
          ev.Skip();
          ClearFieldNote(this);
        });
#endif
    m_btnCancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](wxCommandEvent& ev) {
#ifndef OCPNIOS_OCPN_MASTER
      // On the "Filter sentences" page this is "< Back".
      auto fb = IosFilterBack().find(this);
      if (fb != IosFilterBack().end() && fb->second()) return;
#endif
      ClearFieldNote(this);
      OnCancelClick();
    });
    // Switching the protocol (TCP/UDP/GPSD/Signal K) calls LayoutDialog(),
    // which ends with Fit(): the panel took the width of its contents, ran
    // off the right edge on the iPhone ("Done" disappeared) and became
    // narrower than the page on the iPad. Back to the width of the page.
    Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
      ev.Skip();
      wxWindow* page = GetParent();
      if (!page || !IsShown()) return;
      const int w = page->GetClientSize().x;
      if (w <= 0 || ev.GetSize().x == w) return;
      CallAfter([this, page] {
        SetSize(page->GetClientSize().x, GetSize().y);
        Layout();
        if (g_options) g_options->IosFitPage(this);
        Layout();
      });
    });
  }
}
#endif  // OCPNIOS_CONN_EDIT_NAV

// ---------------------------------------------------------------------------
// OpenCPN master: the rewritten connection editor (gui/src/connection_edit.cpp,
// AbstractConnectionEditDlg). Its classes are local to that file, so the editor
// is adapted from the outside, once, right after it is created
// (connections_dlg.cpp, EditPanel). Same iOS form as the 5.14 editor above.
// ---------------------------------------------------------------------------

namespace {

bool IosDark() {
  return QCoreApplication::instance() &&
         QCoreApplication::instance()->property("ocpnios_dark").toBool();
}

void NavButtonLook(wxButton* b) {
  QWidget* q = b->GetHandle();
  if (!q) return;
  q->setProperty("ocpnios_nav", true);  // touch_ui.cpp
  q->style()->unpolish(q);
  q->style()->polish(q);
  b->InvalidateBestSize();
  b->SetMinSize(wxSize(std::max(64, q->sizeHint().width()),
                       std::max(44, q->sizeHint().height())));
}

// Labels left, inputs filling the rest of the row: the editor sets a desktop
// minimum width on the address and port fields and does not expand them.
void FullWidthInputs(wxSizer* sz) {
  if (!sz) return;
  auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
  if (fg && (fg->GetCols() == 1 || fg->GetCols() == 2)) {
    const int col = fg->GetCols() - 1;  // inputs: second column, or the only one
    if (!fg->IsColGrowable(col)) fg->AddGrowableCol(col);
    int i = 0;
    for (wxSizerItem* it : fg->GetChildren()) {
      wxWindow* w = it->GetWindow();
      if ((col == 0 || i % 2 == 1) && w &&
          (dynamic_cast<wxTextCtrl*>(w) || dynamic_cast<wxChoice*>(w) ||
           dynamic_cast<wxComboBox*>(w))) {
        it->SetFlag(it->GetFlag() | wxEXPAND);
        w->SetMinSize(wxSize(8 * w->GetCharWidth(), w->GetMinSize().y));
        // Qt keeps its own minimum (the desktop width of the help text); the
        // field then stuck out past its panel and was cut off.
        if (QWidget* q = w->GetHandle()) q->setMinimumWidth(0);
      }
      ++i;
    }
  }
  for (wxSizerItem* it : sz->GetChildren())
    if (it->IsSizer()) FullWidthInputs(it->GetSizer());
}

bool IsInput(wxWindow* w) {
  return dynamic_cast<wxTextCtrl*>(w) || dynamic_cast<wxChoice*>(w) ||
         dynamic_cast<wxComboBox*>(w);
}

// The grid still placed the fields a little past the right edge of their
// panel (the label column comes out wider than its labels); keep each field
// inside its panel after every layout of that panel.
void KeepInputsInside(wxWindow* panel) {
  for (wxWindow* c : panel->GetChildren()) {
    if (!IsInput(c) || !c->IsShown()) continue;
    const int room = panel->GetClientSize().x - c->GetPosition().x - 8;
    if (room > 40 && c->GetSize().x > room) c->SetSize(room, -1);
  }
}

void KeepInputsInsideAlways(wxWindow* w) {
  bool inputs = false;
  for (wxWindow* c : w->GetChildren()) inputs = inputs || IsInput(c);
  if (inputs)
    w->Bind(wxEVT_SIZE, [w](wxSizeEvent& ev) {
      ev.Skip();
      w->CallAfter([w] { KeepInputsInside(w); });
    });
  for (wxWindow* c : w->GetChildren()) KeepInputsInsideAlways(c);
}

void ForEachChoice(wxWindow* w, const std::function<void(wxWindow*)>& fn) {
  for (wxWindow* c : w->GetChildren()) {
    if (dynamic_cast<wxChoice*>(c)) fn(c);
    ForEachChoice(c, fn);
  }
}

// After another layout pass the choices and the text fields of the settings
// grid ended up with different left edges (choices keep the width IosFitPage
// gave them, the text fields a fixed one): give every input in the second
// column the same left and right edge, and a note under a field the same left.
void AlignInputColumns(wxWindow* w) {
  std::vector<wxWindow*> col;
  for (wxWindow* c : w->GetChildren()) {
    auto* g = dynamic_cast<wxFlexGridSizer*>(c->GetContainingSizer());
    if (!g || g->GetCols() != 2 || !c->IsShownOnScreen()) continue;
    int idx = 0;
    for (wxSizerItem* it : g->GetChildren()) {
      if (it->GetWindow() == c) break;
      ++idx;
    }
    if (idx % 2 == 1) col.push_back(c);
  }
  int left = INT_MAX, right = 0;
  for (wxWindow* c : col)
    if (IsInput(c)) {
      left = std::min(left, c->GetPosition().x);
      right = std::max(right, c->GetPosition().x + c->GetSize().x);
    }
  if (left < right)
    for (wxWindow* c : col) {
      if (IsInput(c)) {
        c->SetMaxSize(wxDefaultSize);
        c->SetSize(left, c->GetPosition().y, right - left, -1);
      } else if (dynamic_cast<wxStaticText*>(c)) {
        c->Move(left, c->GetPosition().y);
      }
    }
  for (wxWindow* c : w->GetChildren()) AlignInputColumns(c);
}

// The editor is reused (edit after new, another connection type) and a second
// fit left the choices a little left of the text fields and the expert switch
// short of the right edge. Line them up again: every input gets the left and
// right edge of the text fields, and every switch ends at that right edge.
void AlignStackedForm(wxWindow* dlg) {
  for (wxWindow* c : dlg->GetChildren()) {
    auto* sw = dynamic_cast<wxScrolledWindow*>(c);
    if (!sw) continue;
    std::vector<wxWindow*> inputs, switches;
    std::function<void(wxWindow*)> collect = [&](wxWindow* w) {
      for (wxWindow* k : w->GetChildren()) {
        if (!k->IsShown()) continue;
        if (IsInput(k))
          inputs.push_back(k);
        else if (dynamic_cast<wxCheckBox*>(k))
          switches.push_back(k);
        collect(k);
      }
    };
    collect(sw);
    int left = INT_MAX, right = 0;
    for (wxWindow* k : inputs)
      if (dynamic_cast<wxTextCtrl*>(k)) {
        const int x = k->GetScreenPosition().x;
        left = std::min(left, x);
        right = std::max(right, x + k->GetSize().x);
      }
    if (left >= right) continue;
    for (wxWindow* k : inputs) {
      const int px = k->GetParent()->GetScreenPosition().x;
      k->SetSize(left - px, k->GetPosition().y, right - left, -1);
    }
    for (wxWindow* k : switches) {
      const int x = k->GetScreenPosition().x;
      if (right - x > 80) k->SetSize(right - x, -1);
    }
  }
}

// The settings grid (label | field) in one column, as in an iOS form: the
// label above a field of full width, a switch on the right of its label. In
// two columns the fields were too narrow for the connection types and hints
// ("Apparaat dat UDP ze...").
void StackFormGrid(wxFlexGridSizer* g) {
  std::vector<wxWindow*> items;
  for (wxSizerItem* it : g->GetChildren()) {
    if (!it->GetWindow()) return;  // only plain label/control pairs
    items.push_back(it->GetWindow());
  }
  if (items.empty() || items.size() % 2) return;
  for (wxWindow* w : items) g->Detach(w);
  if (g->IsColGrowable(1)) g->RemoveGrowableCol(1);
  g->SetCols(1);
  if (!g->IsColGrowable(0)) g->AddGrowableCol(0);
  for (size_t i = 0; i < items.size(); i += 2) {
    wxWindow* label = items[i];
    wxWindow* ctrl = items[i + 1];
    if (dynamic_cast<wxCheckBox*>(ctrl) && ctrl->GetLabel().empty()) {
      // A switch with its text, like "Receive input on this port" below
      // (IosFitPage: text left, switch right), instead of a separate label.
      ctrl->SetLabel(label->GetLabel());
      label->Hide();
      // Room on the right: the grid column is a little wider than the fields
      // (KeepInputsInside) and the switch was clipped at the edge.
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(ctrl, 1, wxALIGN_CENTER_VERTICAL);
      row->AddSpacer(24);
      g->Add(row, 0, wxEXPAND | wxALL, 5);
    } else {
      g->Add(label, 0, wxLEFT | wxRIGHT | wxTOP, 5);
      g->Add(ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    }
  }
}

void StackFormGrids(wxWindow* w) {
  std::vector<wxFlexGridSizer*> grids;
  std::function<void(wxWindow*)> find = [&](wxWindow* p) {
    for (wxWindow* c : p->GetChildren()) {
      auto* g = dynamic_cast<wxFlexGridSizer*>(c->GetContainingSizer());
      if (g && g->GetCols() == 2 &&
          std::find(grids.begin(), grids.end(), g) == grids.end())
        grids.push_back(g);
      find(c);
    }
  };
  find(w);
  for (wxFlexGridSizer* g : grids) StackFormGrid(g);
}

// The text fields of the visible settings grid in order: address, port,
// comment (gui/src/conn_edit_commons.cpp, NetGrid).
std::vector<wxTextCtrl*> GridTextFields(wxWindow* dlg) {
  wxFlexGridSizer* grid = nullptr;
  std::function<void(wxWindow*)> find = [&](wxWindow* w) {
    if (grid) return;
    if (dynamic_cast<wxTextCtrl*>(w) && w->GetParent()->IsShownOnScreen())
      grid = dynamic_cast<wxFlexGridSizer*>(w->GetContainingSizer());
    for (wxWindow* c : w->GetChildren()) find(c);
  };
  find(dlg);
  std::vector<wxTextCtrl*> fields;
  if (grid)
    for (wxSizerItem* it : grid->GetChildren())
      if (auto* t = dynamic_cast<wxTextCtrl*>(it->GetWindow()))
        fields.push_back(t);
  return fields;
}

// The description (user comment) is only shown in expert mode; in the 5.14
// editor on iOS it is always there. Show it and its label again.
void ShowComment(wxWindow* dlg) {
  std::vector<wxTextCtrl*> fields = GridTextFields(dlg);
  if (fields.size() < 3) return;
  wxTextCtrl* comment = fields[2];
  wxSizer* grid = comment->GetContainingSizer();
  wxWindow* prev = nullptr;
  for (wxSizerItem* it : grid->GetChildren()) {
    if (it->GetWindow() == comment) break;
    if (it->GetWindow()) prev = it->GetWindow();
  }
  if (auto* label = dynamic_cast<wxStaticText*>(prev)) label->Show();
  comment->Show();
}

void FullWidthInputsIn(wxWindow* w) {
  FullWidthInputs(w->GetSizer());
  for (wxWindow* c : w->GetChildren()) FullWidthInputsIn(c);
}

// The editor checks the address and port when Done is tapped and reports a
// missing value in a desktop message box with an error icon. On iOS the text
// goes under the field, in red, as in an iOS form; the box is not shown.
class FieldErrorFilter : public QObject {
public:
  FieldErrorFilter(wxWindow* dlg, std::function<void()> refit)
      : QObject(dlg->GetHandle()), m_dlg(dlg), m_refit(std::move(refit)) {
    ForEachChoice(dlg, [this](wxWindow* c) {
      c->Bind(wxEVT_CHOICE, [this](wxCommandEvent& ev) {
        ev.Skip();
        ClearNote();
      });
    });
  }

protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::Show) return false;
    auto* mb = qobject_cast<QMessageBox*>(obj);
    if (!mb || !m_dlg->IsShownOnScreen()) return false;
    const wxString text = wxString::FromUTF8(mb->text().toUtf8().constData());
    wxTextCtrl* field = FieldFor(text);
    if (!field) return false;
    ShowNote(field, text);
    mb->setWindowOpacity(0);
    QPointer<QMessageBox> g(mb);
    QTimer::singleShot(0, mb, [g] {
      if (g) g->done(QMessageBox::Ok);
    });
    return false;
  }

private:
  wxWindow* m_dlg;
  std::function<void()> m_refit;
  wxStaticText* m_note = nullptr;
  bool m_spacer = false;  // two-column grid: a spacer under the label

  wxTextCtrl* FieldFor(const wxString& text) const {
    size_t which;
    if (text == wxGetTranslation("Required field address is missing"))
      which = 0;
    else if (text == wxGetTranslation("Required field port is missing") ||
             text == wxGetTranslation("Invalid port number"))
      which = 1;
    else
      return nullptr;
    std::vector<wxTextCtrl*> fields = GridTextFields(m_dlg);
    return which < fields.size() ? fields[which] : nullptr;
  }

  void ShowNote(wxTextCtrl* field, const wxString& text) {
    ClearNote();
    wxSizer* grid = field->GetContainingSizer();
    if (!grid) return;
    int idx = 0;
    for (wxSizerItem* it : grid->GetChildren()) {
      if (it->GetWindow() == field) break;
      ++idx;
    }
    m_note = new wxStaticText(field->GetParent(), wxID_ANY, text);
    m_note->SetForegroundColour(wxColour(0xff, 0x3b, 0x30));  // iOS red
    auto* fg = dynamic_cast<wxFlexGridSizer*>(grid);
    m_spacer = fg && fg->GetCols() == 2;
    if (m_spacer) grid->Insert(++idx, 0, 0);  // under the label column
    grid->Insert(idx + 1, m_note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
    field->Bind(wxEVT_TEXT, [this](wxCommandEvent& ev) {
      ev.Skip();
      ClearNote();
    });
    Relayout(field->GetParent());
  }

  void ClearNote() {
    if (!m_note) return;
    if (wxSizer* grid = m_note->GetContainingSizer()) {
      int idx = 0;
      for (wxSizerItem* it : grid->GetChildren()) {
        if (it->GetWindow() == m_note) break;
        ++idx;
      }
      grid->Detach(m_note);
      if (m_spacer && idx > 0) grid->Remove(idx - 1);
    }
    wxWindow* panel = m_note->GetParent();
    m_note->Destroy();
    m_note = nullptr;
    Relayout(panel);
  }

  // The panel keeps its size, so its own sizer is laid out first; then the
  // editor is fitted as when it is shown.
  void Relayout(wxWindow* panel) {
    for (wxWindow* w = panel; w && w != m_dlg; w = w->GetParent()) {
      w->InvalidateBestSize();
      w->Layout();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(w)) sw->FitInside();
    }
    m_dlg->Layout();
    m_dlg->CallAfter(m_refit);
  }
};

}  // namespace

void ocpnios_ConnEditLayout(wxWindow* dlg) {
  auto* ok = dynamic_cast<wxButton*>(dlg->FindWindow(wxID_OK));
  auto* cancel = dynamic_cast<wxButton*>(dlg->FindWindow(wxID_CANCEL));
  wxSizer* top = dlg->GetSizer();
  if (!ok || !cancel || !top) return;

  // "Cancel | title | Done" at the top, as in an iOS edit screen, instead of
  // OK and Cancel below a long panel. The buttons keep their handlers.
  // The row is only detached, not deleted: the button handlers call into it
  // (ButtonSizer keeps the OK and Cancel callbacks).
  wxSizer* row = ok->GetContainingSizer();
  if (row) {
    row->Detach(ok);
    row->Detach(cancel);
    top->Detach(row);
  }
  wxString title;
  for (wxWindow* c : dlg->GetChildren())
    if (auto* box = dynamic_cast<wxStaticBox*>(c)) {
      title = box->GetLabel();
      box->SetLabel(wxEmptyString);
      break;
    }
  ok->SetLabel(_("Done"));
  wxFont bf = ok->GetFont();
  bf.SetWeight(wxFONTWEIGHT_BOLD);
  ok->SetFont(bf);
  ok->SetDefault();
  NavButtonLook(cancel);
  NavButtonLook(ok);
  // Below the bar a large bold title, as on the other settings pages
  // (options_ios.cpp, IosCreateHeader); between the buttons it did not fit.
  auto* label = new wxStaticText(dlg, wxID_ANY, title);
  wxFont tf = label->GetFont();
  tf.SetWeight(wxFONTWEIGHT_BOLD);
  tf.SetPointSize(tf.GetPointSize() * 14 / 10);
  label->SetFont(tf);
  label->GetHandle()->setProperty("ocpnios_nowrap", true);
  auto* bar = new wxBoxSizer(wxHORIZONTAL);
  bar->Add(cancel, 0, wxALIGN_CENTER_VERTICAL);
  bar->AddStretchSpacer(1);
  bar->Add(ok, 0, wxALIGN_CENTER_VERTICAL);
  top->Insert(0, bar, 0, wxALL | wxEXPAND, 5);
  top->Insert(1, label, 0, wxLEFT | wxBOTTOM, 15);

  // The connection types (Network, Internal GPS) stacked in a column: the
  // wrap sizer kept its desktop size and showed only the first one. Type
  // buttons and settings on the card itself, without a grey panel.
  const wxColour card = IosDark() ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE;
  for (wxWindow* c : dlg->GetChildren()) {
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(c)) {
      std::function<void(wxWindow*)> paint = [&](wxWindow* w) {
        if (dynamic_cast<wxPanel*>(w)) w->SetBackgroundColour(card);
        for (wxWindow* k : w->GetChildren()) paint(k);
      };
      paint(sw);
      sw->SetScrollRate(0, 10);  // vertical only, fields fit the width
      continue;
    }
    auto* panel = dynamic_cast<wxPanel*>(c);
    if (!panel || !panel->GetSizer()) continue;
    std::vector<wxWindow*> radios;
    for (wxSizerItem* it : panel->GetSizer()->GetChildren())
      if (dynamic_cast<wxRadioButton*>(it->GetWindow()))
        radios.push_back(it->GetWindow());
    if (radios.empty()) continue;
    auto* column = new wxBoxSizer(wxVERTICAL);
    for (wxWindow* r : radios) column->Add(r, 0, wxALL, 5);
    panel->GetSizer()->Clear(false);
    panel->SetSizer(column);  // deletes the wrap sizer
    panel->SetBackgroundColour(card);
    panel->SetMinSize(wxDefaultSize);
    panel->InvalidateBestSize();
  }
  StackFormGrids(dlg);
  FullWidthInputsIn(dlg);
  KeepInputsInsideAlways(dlg);

  // "New connection" when the editor is opened with "Add new connection...",
  // otherwise the editor's own "Edit connection" (as in the 5.14 editor).
  auto is_new = std::make_shared<bool>(false);
  if (wxWindow* parent = dlg->GetParent()) {
    const wxString add = wxGetTranslation("Add new connection...");
    std::function<void(wxWindow*)> find = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (auto* b = dynamic_cast<wxButton*>(c);
            b && (b->GetLabel() == add || b->GetName() == "ocpnios_add_connection"))
          b->Bind(wxEVT_BUTTON, [is_new](wxCommandEvent& ev) {
            *is_new = true;
            ev.Skip();
          });
        find(c);
      }
    };
    find(parent);
  }

  // Fit the editor like the other settings pages each time it is shown
  // (options_ios.cpp, IosFitPage): wrapped labels, switches, cards.
  auto fit = [dlg, label, title, is_new] {
    ShowComment(dlg);
    IosExpandChevrons(dlg);
    // No horizontal bar (the fields fit the width). The vertical one stays:
    // finger scrolling (touch_ui.cpp) needs its range, and without it the
    // switches below the fields could not be reached.
    for (wxWindow* c : dlg->GetChildren())
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(c))
        sw->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    FullWidthInputsIn(dlg);  // hints are set again for each view
    dlg->Layout();
    if (g_options) g_options->IosFitPage(dlg);
    FullWidthInputsIn(dlg);  // IosFitPage locks choices at their full width
    // The title stays on one line (IosFitPage may have wrapped it).
    label->SetLabel(*is_new ? wxGetTranslation("Configure new connection")
                            : title);
    dlg->Layout();
    // Once more after Qt has removed the scroll bar: until then the visible
    // area was narrower than the width the fields were laid out for.
    dlg->CallAfter([dlg] {
      dlg->Layout();
      for (wxWindow* c : dlg->GetChildren())
        if (auto* sw = dynamic_cast<wxScrolledWindow*>(c)) sw->FitInside();
      std::function<void(wxWindow*)> keep = [&](wxWindow* w) {
        KeepInputsInside(w);
        for (wxWindow* c : w->GetChildren()) keep(c);
      };
      keep(dlg);
      AlignInputColumns(dlg);
      AlignStackedForm(dlg);
    });
  };
  if (dlg->GetHandle())
    QCoreApplication::instance()->installEventFilter(
        new FieldErrorFilter(dlg, fit));
  dlg->Bind(wxEVT_SHOW, [dlg, fit, is_new](wxShowEvent& ev) {
    ev.Skip();
    if (ev.IsShown())
      dlg->CallAfter(fit);
    else
      *is_new = false;
  });
  // Expert mode and the connection type show other fields and labels (the
  // label column got wider and the fields no longer lined up): fit again.
  dlg->Bind(wxEVT_CHECKBOX, [dlg, fit](wxCommandEvent& ev) {
    ev.Skip();
    dlg->CallAfter(fit);
  });
  // The connection type choice does not pass its event on to the editor
  // (conn_edit_commons.cpp skips a copy), so bind to the choices themselves.
  ForEachChoice(dlg, [dlg, fit](wxWindow* c) {
    c->Bind(wxEVT_CHOICE, [dlg, fit](wxCommandEvent& ev) {
      ev.Skip();
      dlg->CallAfter(fit);
    });
  });
  dlg->Layout();
}

// ---------------------------------------------------------------------------
// gui/src/welcome.cpp (OpenCPN master): the welcome message as a sheet with
// "Cancel | Welcome to OpenCPN | Agree" (window_fit.cpp), as in 5.14, instead
// of a desktop message box.
// ---------------------------------------------------------------------------

bool ocpnios_ShowNavWarning(wxWindow* parent, const wxString& message) {
  // The text has line breaks for a desktop box; keep only the paragraphs. As
  // HTML, like the 5.14 AlertDialog, so that it wraps and the dialog becomes
  // a sheet rather than a short alert (window_fit.cpp, HasHtmlText).
  wxString text = message;
  text.Trim(false).Trim(true);
  text.Replace("&", "&amp;");
  text.Replace("<", "&lt;");
  text.Replace("\n\n", "\x01");
  text.Replace("\n", " ");
  text.Replace("\x01", "</p><p>");
  wxDialog dlg(parent, wxID_ANY, _("Welcome to OpenCPN"));
  auto* vbox = new wxBoxSizer(wxVERTICAL);
  auto* html = new wxHtmlWindow(&dlg, wxID_ANY, wxDefaultPosition,
                                wxDefaultSize, wxHW_SCROLLBAR_AUTO);
  html->SetPage("<html><body><p>" + text + "</p></body></html>");
  vbox->Add(html, 1, wxALL | wxEXPAND, 10);
  auto* buttons = new wxStdDialogButtonSizer();
  buttons->AddButton(new wxButton(&dlg, wxID_OK, _("Agree")));
  buttons->AddButton(new wxButton(&dlg, wxID_CANCEL, _("Cancel")));
  buttons->Realize();
  vbox->Add(buttons, 0, wxALL | wxEXPAND, 10);
  dlg.SetSizerAndFit(vbox);
  return dlg.ShowModal() == wxID_OK;
}

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
