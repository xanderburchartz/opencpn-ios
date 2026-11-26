// iOS layout of the OpenCPN Options dialog: category list like the Settings app,
// pages with a back button and an iOS-style header (Cancel/Apply/Done), iOS look
// and pages fitted to a phone screen; also the magnetic variation row and the
// tide dataset names. Most are members of class options
// (gui/include/gui/options.h); they live here so that gui/src/options.cpp only
// carries the hooks that call them.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <functional>
#include <vector>

#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>

#include <QCoreApplication>
#include <QTimer>
#include <QElapsedTimer>
#include <QApplication>
#include <QColor>
#include <QtWidgets/QWidget>
#include <QtWidgets/QStyle>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QAbstractScrollArea>
#include <QFontMetrics>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QGroupBox>

#include "model/config_vars.h"  // g_bAutoHideToolbar
#include "options.h"

class QAbstractItemView;
void ocpnios_MakeNavList(QAbstractItemView* view);  // app/ios/nav_list.cpp
void ocpnios_BuildStatusPage(wxScrolledWindow* page);  // app/ios/status_panel.cpp
wxString ocpnios_StatusPageTitle();                   // app/ios/status_panel.cpp
void ocpnios_BuildToolbarPage(wxScrolledWindow* page);  // app/ios/toolbar_ios.cpp
wxString ocpnios_ToolbarPageTitle();                   // app/ios/toolbar_ios.cpp
void ocpnios_RefreshToolbarPage(wxWindow* shownPage);  // app/ios/toolbar_ios.cpp
void ocpnios_SetAutoHideCheckbox(wxCheckBox* cb);      // app/ios/toolbar_ios.cpp
int ocpnios_ChartBarWanted();                          // app/ios/chart_bar_ios.cpp
void ocpnios_ChartBarSetWanted(bool wanted);           // app/ios/chart_bar_ios.cpp
int ocpnios_CompassWanted();                           // app/ios/map_controls_ios.cpp
void ocpnios_SetCompassWanted(bool wanted);            // app/ios/map_controls_ios.cpp

#if defined(__OCPN__IOS__) && defined(OCPN_OPTIONS_USE_LISTBOOK)
// iOS: list mode = category list filling the dialog (like the Settings app);
// page mode = only the selected page, with a back button.
void options::IosShowList(bool list) {
  if (list)
    IosGoLevel(0);
  else
    IosOpenCategory();
}

wxNotebook* options::IosCurrentNotebook() {
  auto* nb = dynamic_cast<wxNotebook*>(m_pListbook->GetCurrentPage());
  return (nb && nb->GetPageCount() > 1) ? nb : nullptr;
}

void options::IosOpenCategory() {
  ocpnios_RefreshToolbarPage(m_pListbook->GetCurrentPage());
  // "Show chart bar" is the user's choice; g_bShowChartBar itself is off while there is
  // only one chart here (chart_bar_ios.cpp).
  // Auto-hide of the toolbar is one setting, shown here and in Options > Toolbar
  // (toolbar_ios.cpp): show the current value, not the one from when Options was built,
  // otherwise Done wrote the old value back.
  if (pToolbarAutoHideCB) {
    pToolbarAutoHideCB->SetValue(g_bAutoHideToolbar);
    ocpnios_SetAutoHideCheckbox(pToolbarAutoHideCB);
  }
  // Same for the compass box (map_controls_ios.cpp keeps g_bShowCompassWin off).
  if (pShowCompassWin && ocpnios_CompassWanted() >= 0) {
    pShowCompassWin->SetValue(ocpnios_CompassWanted() == 1);
    if (!pShowCompassWin->GetClientData()) {
      pShowCompassWin->SetClientData(pShowCompassWin);
      pShowCompassWin->Bind(wxEVT_CHECKBOX, [](wxCommandEvent& e) {
        e.Skip();
        ocpnios_SetCompassWanted(e.IsChecked());
      });
    }
  }
  if (pShowChartBar && ocpnios_ChartBarWanted() >= 0) {
    pShowChartBar->SetValue(ocpnios_ChartBarWanted() == 1);
    // A change is the user's choice right away: while the bar is off because there is
    // one chart, switching it off would otherwise not register.
    if (!pShowChartBar->GetClientData()) {
      pShowChartBar->SetClientData(pShowChartBar);
      pShowChartBar->Bind(wxEVT_CHECKBOX, [](wxCommandEvent& e) {
        e.Skip();
        ocpnios_ChartBarSetWanted(e.IsChecked());
      });
    }
  }
  wxNotebook* nb = IosCurrentNotebook();
  if (!nb) {
    IosGoLevel(2);
    return;
  }
  m_iosSubList->Clear();
  for (size_t i = 0; i < nb->GetPageCount(); i++)
    // A translation with a leading space was misaligned in the list.
    m_iosSubList->Append(wxString(nb->GetPageText(i)).Strip(wxString::both));
  IosGoLevel(1);
}

void options::IosOpenSubPage(int idx) {
  wxNotebook* nb = IosCurrentNotebook();
  if (nb && idx >= 0 && idx < (int)nb->GetPageCount()) nb->SetSelection(idx);
  IosGoLevel(2);
}

void options::IosBack() {
  if (m_iosLevel == 2 && IosCurrentNotebook())
    IosGoLevel(1);
  else
    IosGoLevel(0);
}

// Hide the whole header row (Back/Cancel/Apply/Done) and the large title while a
// connection is being edited; the edit panel has its own titled Cancel/Done bar.
void options::ShowOKButtons(bool _show) {
  m_OK_Cancel_Apply_buttons->Show(_show);
  if (m_iosBackButton) m_iosBackButton->Show(_show && m_iosLevel > 0);
  // No Cancel and no Apply: changes count at once (IosDirectApply).
  if (m_CancelButton) m_CancelButton->Hide();
  if (m_OKButton) m_OKButton->Show(_show);
  if (m_ApplyButton) m_ApplyButton->Hide();
  if (m_iosTitle) m_iosTitle->Show(_show);
  Layout();
}

void options::IosGoLevel(int level) {
  m_iosLevel = level;
  m_iosListMode = (level == 0);
  const int cat = m_pListbook->GetSelection();
  const wxString catName = cat >= 0 ? m_pListbook->GetPageText(cat) : wxString();
  wxNotebook* nb = IosCurrentNotebook();
  wxString back = _("Options");
  wxString title = _("Options");
  if (level == 1) {
    title = catName;
  } else if (level == 2) {
    if (nb) {
      // Long (translated) names made the button two lines high; then just "Back",
      // as iOS does. Measured against the room the header leaves (half the
      // window width, the right half holds Apply and Done) instead of a fixed 16
      // characters: on iPad and for names that fit on iPhone the button now names the
      // previous page (HIG review 26 Nov 2026, item 9; "< Back" and "< Options"
      // alternated).
      back = catName;
      back.Replace("\n", " ");
      if (m_iosBackButton) {
        const QFontMetrics fm(m_iosBackButton->GetHandle()->font());
        const int room = GetClientSize().x / 2;
        const wxString label = wxString::FromUTF8("\u2039 ") + back;
        if (fm.horizontalAdvance(QString::fromUtf8(label.utf8_str().data())) + 32 > room)
          back = _("Back");
      } else if (catName.length() > 16) {
        back = _("Back");
      }
      const int sp = nb->GetSelection();
      title = sp >= 0 ? wxString(nb->GetPageText(sp)).Strip(wxString::both) : catName;
    } else {
      title = catName;
    }
  }
  // Category names may contain a hard line break for the list; as a title, use
  // one line.
  title.Replace("\n", " ");
  back.Replace("\n", " ");
  if (m_iosBackButton) {
    m_iosBackButton->SetLabel(wxString::FromUTF8("\u2039 ") + back);
    // Recompute the minimum width from the new text; otherwise the width of the
    // previous label stayed and the new one was clipped.
    if (QWidget* q = m_iosBackButton->GetHandle()) {
      q->adjustSize();
      m_iosBackButton->InvalidateBestSize();
      m_iosBackButton->SetMinSize(wxSize(std::max(64, q->sizeHint().width()),
                                         std::max(44, q->sizeHint().height())));
    }
    m_iosBackButton->Show(level > 0);
  }
  // As the iOS Settings app: no Cancel and no Apply, a change counts at once
  // (IosDirectApply below); Done closes. Xander 26 nov 2026 asked whether
  // Cancel/Apply/Done "wel iOS" is; Apply does not exist on iOS.
  if (m_CancelButton) m_CancelButton->Hide();
  if (m_ApplyButton) m_ApplyButton->Hide();
  if (m_iosTitle) m_iosTitle->SetLabel(title);
  // Hide the notebook tab bar: selection goes through the list.
  if (nb) {
    if (auto* tw = qobject_cast<QTabWidget*>(nb->GetHandle()))
      tw->tabBar()->setVisible(false);
  }
  if (m_iosSubList) m_iosSubList->SetSelection(wxNOT_FOUND);
  Layout();
  IosApplyLayout();
}

bool ocpnios_AppIsWindowed();  // app/ios/app_window.mm

// Settings in a single iOS style: categories as a grouped list with iOS symbols in
// coloured squares (app/ios/nav_list.cpp), static boxes on the pages as iOS sections
// (white rounded card, grey header above), no borders around pages and tabs.
void ocpnios_SetNavSymbols(QAbstractItemView* view,
                           const std::vector<std::pair<std::string, unsigned>>& syms);

QString ocpnios_DarkSwitchStyle();  // app/ios/fit_width.cpp
QString ocpnios_ButtonStyle(bool dark);  // app/ios/fit_width.cpp

void options::IosApplyLook() {
  // iOS: extra page under User Interface for the status display (status_panel.cpp).
  // Once; IosApplyLook runs again after a colour scheme change.
  if (m_pageUI != (size_t)-1) {
    auto* nb = dynamic_cast<wxNotebook*>(m_pListbook->GetPage(m_pageUI));
    const wxString title = ocpnios_StatusPageTitle();
    bool have = false;
    for (size_t i = 0; nb && i < nb->GetPageCount(); i++)
      if (nb->GetPageText(i) == title) have = true;
    if (nb && !have) ocpnios_BuildStatusPage(AddPage(m_pageUI, title));
  }
  // iOS: own category "Toolbar" (toolbar_ios.cpp), at the end of the list.
  int toolbarPage = -1;
  {
    const wxString title = ocpnios_ToolbarPageTitle();
    for (size_t i = 0; i < m_pListbook->GetPageCount(); i++)
      if (m_pListbook->GetPageText(i) == title) toolbarPage = (int)i;
    if (toolbarPage < 0) {
      const size_t p = CreatePanel(title);
      ocpnios_BuildToolbarPage(AddPage(p, title));
      toolbarPage = (int)p;
    }
  }
  if (auto* tv = dynamic_cast<QAbstractItemView*>(m_pListbook->GetListView()->GetHandle())) {
    std::vector<std::pair<std::string, unsigned>> syms(m_pListbook->GetPageCount(),
                                                       {"gearshape", 0x8e8e93});
    auto set = [&](int page, const char* name, unsigned color) {
      if (page >= 0 && page < (int)syms.size()) syms[page] = {name, color};
    };
    set(m_pageDisplay, "display", 0x007aff);
    set(m_pageCharts, "map", 0x34c759);
    set(m_pageConnections, "antenna.radiowaves.left.and.right", 0xff9500);
    set(m_pageShips, "ferry", 0xff3b30);
    set(m_pageUI, "slider.horizontal.3", 0x8e8e93);
    set(m_pagePlugins, "puzzlepiece.extension", 0xaf52de);
    set(toolbarPage, "sidebar.left", 0x5856d6);
    ocpnios_SetNavSymbols(tv, syms);
  }
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  // Behind the list, the dialog background (otherwise stayed light in dark mode).
  {
    // Header colour: light (#f2f2f7), or black in dark mode. The wx colour of the
    // dialog stayed light.
    const bool darkBg = QCoreApplication::instance() &&
                        QCoreApplication::instance()->property("ocpnios_dark").toBool();
    const QString ls = QString("QAbstractItemView { background: %1; border: none;"
                               " padding-top: 12px; }")
                           .arg(darkBg ? "#000000" : "#f2f2f7");
    for (wxWindow* w : {static_cast<wxWindow*>(m_pListbook->GetListView()),
                        static_cast<wxWindow*>(m_iosSubList)})
      if (w && w->GetHandle()) w->GetHandle()->setStyleSheet(ls);
  }
  const QString card = dark ? "#1c1c1e" : "#ffffff";
  const QString head = dark ? "#8d8d93" : "#6d6d72";
  if (QWidget* q = GetHandle()) {
    QString ss = q->styleSheet();
    const int cut = ss.indexOf("/*ocpnios-look*/");
    if (cut >= 0) ss.truncate(cut);
    ss += QString(
              "/*ocpnios-look*/"
              "QGroupBox { background-color: %1; border: none; border-radius: 10px;"
              "  margin-top: 30px; padding: 10px 6px 6px 6px; }"
              "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
              // Header aligned with the headers above the other cards (fit_width.cpp
              // puts the box 16 pt from the edge; header there 28 pt).
              "  left: 6px; top: 4px; color: %2; }"
              "QTabWidget::pane { border: none; }"
              "QScrollArea { border: none; }"
              // Buttons as in the other sheets (fit_width.cpp, ocpnios_SheetLook):
              // blue text on a card, no border. Header buttons are left alone.
              "QPushButton { background-color: %1; color: #007aff; border: none;"
              "  border-radius: 10px; padding: 10px 16px; }"
              "QPushButton:pressed { background-color: %3; }"
              "QPushButton:disabled { color: %2; }"
              "QPushButton[ocpnios_nav=\"true\"] { border: none; background: transparent;"
              "  color: #007aff; font-size: 18px; padding: 12px 14px; }"
              "QPushButton[ocpnios_nav=\"true\"]:pressed { color: #66aaff; }"
              // Lists and text boxes as cards, without the desktop border.
              "QTreeView, QListView, QTableView, QTextEdit { background: %1;"
              "  border: none; border-radius: 10px; }"
              "QHeaderView::section { background: %1; color: %2; border: none;"
              "  border-bottom: 1px solid %3; padding: 6px 8px; }")
              .arg(card, head, dark ? "#2c2c2e" : "#d1d1d6",
                   dark ? "#2c2c2e" : "#e3e3e8", dark ? "#636366" : "#ffffff");
    // Dark mode: muted red accent colour (as fit_width.cpp, ocpnios_SheetStyle).
    if (dark) {
      ss.replace("#007aff", "#d06060");
      ss.replace("#66aaff", "#e08080");
      ss += ocpnios_DarkSwitchStyle();  // app/ios/fit_width.cpp
    }
    ss += ocpnios_ButtonStyle(dark);  // app/ios/fit_width.cpp
    q->setStyleSheet(ss);
  }
}

void options::IosApplyLayout() {
  if (!m_pListbook) return;
  {
    // Colour scheme changed while Options was closed: reapply the style.
    const int dark = QCoreApplication::instance() &&
                     QCoreApplication::instance()->property("ocpnios_dark").toBool();
    if (dark != m_iosLookDark) {
      m_iosLookDark = dark;
      IosApplyLook();
    }
  }
  // Windowed mode (iPadOS 26): the window controls (●●●) sit at the left of the
  // header; "Cancel" / "‹ Back" was hidden underneath.
  if (m_iosHead && m_iosHead->GetItemCount() > 0) {
    const int inset = ocpnios_AppIsWindowed() ? 70 : 0;
    for (size_t i = 0; i < 2 && i < m_iosHead->GetItemCount(); i++) {
      wxSizerItem* it = m_iosHead->GetItem(i);
      it->SetFlag((it->GetFlag() & ~wxLEFT) | (inset ? wxLEFT : 0));
      it->SetBorder(inset);
    }
  }
  wxListView* lv = m_pListbook->GetListView();
  wxWindow* pg = m_pListbook->GetCurrentPage();
  const wxSize sz = m_pListbook->GetClientSize();
  auto place = [&](wxWindow* w, bool show) {
    if (!w) return;
    if (show) {
      w->SetSize(0, 0, sz.x, sz.y);
      w->Show();
      w->Raise();
    } else {
      w->Hide();
    }
  };
  place(lv, m_iosLevel == 0);
  place(m_iosSubList, m_iosLevel == 1);
  place(pg, m_iosLevel == 2);
  // Connections only follows the page size on a wxEVT_SIZE of Options; after the
  // placement above an empty area (~200 pt) remained at the bottom.
  if (m_iosLevel == 2 && pg && pg == m_pNMEAForm && comm_dialog) {
#ifdef OCPNIOS_OCPN_MASTER  // master removed ConnectionsDlg::OnResize (1cb501077)
    comm_dialog->SetSize(pg->GetClientSize());
    comm_dialog->Layout();
#else
    comm_dialog->OnResize(pg->GetClientSize());
#endif
  }
  if (m_iosLevel == 2 && pg) {
    wxNotebook* nb = IosCurrentNotebook();
    wxWindow* sub = nb ? nb->GetCurrentPage() : pg;
    if (sub) IosFitPage(sub);
  }
}

// Options pages are designed for a wide desktop screen: on a phone, long texts next
// to an input ran off the right edge and the input disappeared. Checkbox, radio
// button and label texts are wrapped at word boundaries until they fit; if something
// is next to them (row in a sizer), they get just over half the width. The original
// text is kept, so this can be redone after rotation.

void ocpnios_CenterRowItems(wxWindow* root);  // app/ios/fit_width.cpp
void ocpnios_SectionGrids(wxWindow* root);     // app/ios/fit_width.cpp
void ocpnios_FixControls(wxWindow* root);      // app/ios/fit_width.cpp
void ocpnios_EdgeMargins(wxWindow* root);      // app/ios/fit_width.cpp

// Buttons on the Options pages were thick filled blue buttons; iOS Settings shows an
// action as a row with blue text ("Add Dataset", "Inland ECDIS Manual") and a removal
// in red (Xander, 24 Nov 2026). See fit_width.cpp, ocpnios_ActionRows.
void ocpnios_ActionRows(wxWindow* root,
                        const std::function<int(wxButton*)>& kind);  // fit_width.cpp
static void IosActionRows(wxWindow* page) {
  const wxString newLabel = _("New...");
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  ocpnios_ActionRows(page, [&](wxButton* b) {
    const int id = b->GetId();
    if (b->GetLabel() == newLabel && b->GetParent() && b->GetParent() != page) {
      // MMSI: the panel around the list and "New" kept the grey page colour; as a
      // filled button that did not show, as a row it was a grey band in the card.
      b->GetParent()->SetBackgroundColour(dark ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE);
    }
    if (id == ID_BUTTONADD || id == ID_TCDATAADD || id == ID_GROUPINSERTDIR ||
        id == ID_GROUPNEWGROUP || b->GetLabel() == newLabel)
      return 1;  // add
    if (id == ID_BUTTONDELETE || id == ID_TCDATADEL || id == ID_GROUPREMOVEDIR ||
        id == ID_GROUPDELETEGROUP)
      return 2;  // destructive
    return 0;
  });
}

// The sizer item that holds `target`, anywhere below `root`.
static wxSizerItem* IosFindSizerItem(wxSizer* root, wxSizer* target) {
  if (!root || !target) return nullptr;
  for (wxSizerItem* it : root->GetChildren()) {
    if (!it->IsSizer()) continue;
    if (it->GetSizer() == target) return it;
    if (wxSizerItem* r = IosFindSizerItem(it->GetSizer(), target)) return r;
  }
  return nullptr;
}

// Lists on Tides and Chart Files filled the page down to the bottom, with a large empty
// white area under the entries and the buttons (Xander, 24 Nov 2026). As in iOS: the
// list as high as its entries (at least one row), the card ending under the last row.
static void IosListHeights(wxWindow* page, wxListCtrl* tides, wxScrolledWindow* dirs,
                           wxSizer* dirsBox) {
  auto shrink = [&](wxWindow* list, wxSizer* box, int h) {
    list->SetMinSize(wxSize(list->GetMinSize().x, h));
    list->SetMaxSize(wxSize(-1, h));
    if (wxSizerItem* it = IosFindSizerItem(page->GetSizer(), box)) it->SetProportion(0);
  };
  if (tides && tides->GetParent() == page && tides->GetContainingSizer()) {
    int h = 44;
    if (auto* tv = dynamic_cast<QAbstractItemView*>(tides->GetHandle())) {
      const int rows = tv->model() ? tv->model()->rowCount() : 0;
      const int rowH = rows > 0 ? std::max(tv->sizeHintForRow(0), 30) : 44;
      h = std::max(1, rows) * rowH + 12;
    }
    shrink(tides, tides->GetContainingSizer(), std::min(h, 400));
  }
  if (dirs && dirs->GetParent() == page && dirsBox) {
    // Chart directories: no fixed height. The list follows its own content (best
    // size from its sizer), so it is empty when there are none and grows when a
    // directory is added, without the page having to be fitted again.
    // The list as high as its rows; again whenever a directory row is added or removed
    // (the page is not fitted again then).
    auto fit = [page, dirs]() {
      const int content = dirs->GetSizer() ? dirs->GetSizer()->CalcMin().y : 0;
      const int h = std::min(content > 0 ? content + 4 : 0, 400);
      if (dirs->GetMinSize().y == h && dirs->GetMaxSize().y == h) return;
      dirs->SetMinSize(wxSize(dirs->GetMinSize().x, h));
      dirs->SetMaxSize(wxSize(-1, h));
      page->Layout();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
    };
    // wx creates the rows in the viewport of the scroll area (or the widget itself).
    std::vector<QWidget*> inners;
    if (QWidget* q = dirs->GetHandle()) {
      inners.push_back(q);
      if (auto* sa = qobject_cast<QAbstractScrollArea*>(q)) inners.push_back(sa->viewport());
    }
    for (QWidget* inner : inners) {
      if (!inner->property("ocpnios_dirs_fit").toBool()) {
        inner->setProperty("ocpnios_dirs_fit", true);
        struct Watch : QObject {
          std::function<void()> fn;
          bool eventFilter(QObject*, QEvent* e) override {
            if (e->type() == QEvent::ChildAdded || e->type() == QEvent::ChildRemoved)
              QTimer::singleShot(0, this, fn);
            return false;
          }
        };
        auto* w = new Watch;
        w->setParent(inner);
        w->fn = fit;
        inner->installEventFilter(w);
      }
    }
    if (wxSizerItem* it = IosFindSizerItem(page->GetSizer(), dirsBox)) it->SetProportion(0);
    if (wxSizerItem* it = dirsBox->GetItem(dirs)) it->SetProportion(0);
    fit();
  }
  page->Layout();
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
}

// MMSI Properties: seven columns of desktop width scrolled sideways on a phone. Columns
// that do not fit are hidden, most important first (MMSI, ship name, track mode); all
// values stay in the edit window of an entry.
static void IosMmsiColumns(wxWindow* page) {
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      visit(c);
      auto* lc = dynamic_cast<MMSIListCtrl*>(c);
      auto* tv = lc ? dynamic_cast<QTreeView*>(lc->GetHandle()) : nullptr;
      if (!tv || !tv->header() || tv->viewport()->width() < 100) continue;
      QHeaderView* h = tv->header();
      const QFontMetrics fm(h->font());
      const int room = tv->viewport()->width();
      const int order[] = {mlMMSI, mlShipName, mlTrackMode, mlIgnore, mlMOB, mlFollower, mlVDM};
      std::vector<int> width(h->count(), 0);
      int used = 0;
      bool full = false;  // after the first column that does not fit, hide the rest
      for (int col : order) {
        if (col >= h->count()) continue;
        const QString text = h->model()->headerData(col, Qt::Horizontal).toString();
        int need = fm.horizontalAdvance(text) + 24;
        if (col == mlMMSI) need = std::max(need, fm.horizontalAdvance("999999999") + 24);
        const bool fits = !full && used + need <= room;
        if (!fits) full = true;
        h->setSectionHidden(col, !fits);
        if (fits) {
          width[col] = need;
          used += need;
        }
      }
      // What is left goes to the ship name.
      if (mlShipName < h->count() && width[mlShipName]) width[mlShipName] += room - used;
      h->setStretchLastSection(false);
      for (int col = 0; col < h->count(); col++)
        if (width[col]) h->resizeSection(col, width[col]);
      tv->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      // As high as its rows (at least one), like the lists on Tides and Chart Files;
      // again when an entry is added or removed.
      auto fitHeight = [page, lc, tv]() {
        const int rows = tv->model() ? tv->model()->rowCount() : 0;
        const int rowH = rows > 0 ? std::max(tv->sizeHintForRow(0), 30) : 44;
        const int listH = std::min(tv->header()->height() + std::max(1, rows) * rowH + 8, 400);
        if (lc->GetMinSize().y == listH) return;
        lc->SetMinSize(wxSize(lc->GetMinSize().x, listH));
        lc->SetMaxSize(wxSize(-1, listH));
        if (wxSizer* cs = lc->GetContainingSizer())
          if (wxSizerItem* it = cs->GetItem(lc)) it->SetProportion(0);
        if (wxWindow* panel = lc->GetParent()) {
          panel->Layout();
          if (wxSizer* ps = panel->GetContainingSizer())
            if (wxSizerItem* it = ps->GetItem(panel)) it->SetProportion(0);
        }
        page->Layout();
        if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
      };
      if (tv->model() && !tv->property("ocpnios_mmsi_fit").toBool()) {
        tv->setProperty("ocpnios_mmsi_fit", true);
        auto later = [tv, fitHeight]() { QTimer::singleShot(0, tv, fitHeight); };
        QObject::connect(tv->model(), &QAbstractItemModel::rowsInserted, tv, later);
        QObject::connect(tv->model(), &QAbstractItemModel::rowsRemoved, tv, later);
        QObject::connect(tv->model(), &QAbstractItemModel::modelReset, tv, later);
      }
      fitHeight();
    }
  };
  visit(page);
}

void options::IosFitPage(wxWindow* page) {
  const int pageW = page->GetClientSize().x;
  if (pageW < 100) return;
  IosActionRows(page);
  ocpnios_SectionGrids(page);    // "header | items" as iOS sections (fit_width.cpp)
  ocpnios_CenterRowItems(page);  // centre text next to a field or button
  const int fieldW = pageW * 28 / 100;  // inputs next to a text
  auto inSharedRow = [](wxWindow* c) {
    wxSizer* row = c->GetContainingSizer();
    if (auto* bs = dynamic_cast<wxBoxSizer*>(row))
      return bs->GetOrientation() == wxHORIZONTAL && bs->GetItemCount() > 1;
    if (auto* fg = dynamic_cast<wxFlexGridSizer*>(row)) return fg->GetCols() > 1;
    return false;
  };
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      // Inputs next to a text no wider than ~28% (choices: see above).
      if ((dynamic_cast<wxTextCtrl*>(c) || dynamic_cast<wxSpinCtrl*>(c)) &&
          !dynamic_cast<wxComboBox*>(c) && inSharedRow(c) && c->GetSize().x > fieldW &&
          c->GetScreenPosition().x - page->GetScreenPosition().x +
                  c->GetSize().x > pageW - 2 * GetCharWidth()) {
        // Only if the field really runs off screen.
        c->SetMinSize(wxSize(fieldW, -1));
        c->SetMaxSize(wxSize(fieldW, -1));
        c->SetSize(fieldW, -1);
      }
      const bool isText = dynamic_cast<wxCheckBox*>(c) ||
                          dynamic_cast<wxRadioButton*>(c) ||
                          dynamic_cast<wxStaticText*>(c);
      // Do not wrap a header title (connection_edit.cpp).
      const bool noWrap =
          c->GetHandle() && c->GetHandle()->property("ocpnios_nowrap").toBool();
      if (isText && c->IsShown() && !noWrap) {
        wxString orig = c->GetName().StartsWith("ocpnios:")
                            ? c->GetName().Mid(8)
                            : c->GetLabel();
        if (!c->GetName().StartsWith("ocpnios:")) c->SetName("ocpnios:" + orig);
        orig.Replace("\n", " ");
        // Space: up to the right edge of the page, or just over half if something
        // else is in the same row.
        int budget = pageW - (c->GetScreenPosition().x - page->GetScreenPosition().x) -
                     4 * GetCharWidth();
        // Something next to it: the rest after an input plus margins.
        // Only if something follows on the right; otherwise a label after the field
        // ("degrees (-W, +E)") wrapped needlessly.
        if (inSharedRow(c)) {
          bool last = false;
          if (wxSizer* row = c->GetContainingSizer()) {
            wxSizerItem* lastShown = nullptr;
            for (wxSizerItem* it : row->GetChildren())
              if (it->IsShown() && !it->IsSpacer()) lastShown = it;
            last = lastShown && lastShown->GetWindow() == c &&
                   !dynamic_cast<wxFlexGridSizer*>(row);
          }
          if (!last) budget -= fieldW + 6 * GetCharWidth();
        }
        if (dynamic_cast<wxCheckBox*>(c) || dynamic_cast<wxRadioButton*>(c))
          budget -= 70;  // switch/radio and spacing
        if (budget > 80 && c->GetTextExtent(orig).x > budget) {
          wxString out, line;
          for (const wxString& word : wxSplit(orig, ' ', '\0')) {
            wxString trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && c->GetTextExtent(trial).x > budget) {
              out += line + "\n";
              line = word;
            } else {
              line = trial;
            }
          }
          out += line;
          if (out != c->GetLabel()) {
            c->SetLabel(out);
            c->InvalidateBestSize();
          }
        } else if (c->GetLabel() != orig &&
                   c->GetLabel().Contains("\n") && !orig.Contains("\n")) {
          c->SetLabel(orig);
          c->InvalidateBestSize();
        }
      }
      visit(c);
    }
  };
  // A few passes: after wrapping the layout shifts and there is more or less
  // space (measured: virtual width 870, after the second pass 476, screen 410).
  for (int round = 0; round < 4; round++) {
    visit(page);
    page->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
    if (page->GetVirtualSize().x <= pageW + 2) break;
  }
  // Sliders (User Interface: scale factors) have a fixed width of half the screen;
  // next to their label they ran off the right edge on iPhone. Make them as wide as
  // the space up to the right edge, at least 120 pt.
  std::function<void(wxWindow*)> fitSliders = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (dynamic_cast<wxSlider*>(c) && c->GetHandle() && page->GetHandle()) {
        const int cx = c->GetHandle()->mapTo(page->GetHandle(), QPoint(0, 0)).x();
        const int room = pageW - cx - 2 * GetCharWidth();
        const int nw = std::max(120, room);
        if (c->GetSize().x > room || c->GetMinSize().x > room) {
          c->SetMinSize(wxSize(nw, -1));
          c->SetMaxSize(wxSize(nw, -1));
          c->SetSize(nw, -1);
        }
      }
      fitSliders(c);
    }
  };
  fitSliders(page);
  page->Layout();
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
  fitSliders(page);  // after Layout the sliders are at their final x
  // Choices only after the passes: during the first pass they still follow an
  // unwrapped label (measured x=268 instead of 157 pt) and there seems to be no room.
  std::function<void(wxWindow*)> fitChoices = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      // Choices with a fixed, too small width ("Paper Cha"): widen until their
      // longest item fits, as long as there is room on the right.
      // A value in an iOS form row that is drawn on the right itself
      // (fit_width.cpp, FormValueText): fills the column, no width of its own.
      if (c->GetHandle() && c->GetHandle()->property("ocpnios_formtext").toBool()) {
        fitChoices(c);
        continue;
      }
      if (dynamic_cast<wxChoice*>(c) || dynamic_cast<wxComboBox*>(c)) {
        // Compute it ourselves: longest item in the real Qt font + arrow and padding
        // (~70 pt). wx and Qt returned a size too small (fixed initial size, style not
        // counted). Target = min(needed, space to the right edge), also set as max,
        // so the 28% limit below does not override it.
        int longest = 0;
        QFontMetrics fm(c->GetHandle() ? c->GetHandle()->font() : QFont());
        if (auto* ch = dynamic_cast<wxItemContainerImmutable*>(c))
          for (unsigned int k = 0; k < ch->GetCount(); k++)
            longest = std::max(
                {longest, c->GetTextExtent(ch->GetString(k)).x,
                 fm.horizontalAdvance(QString::fromUtf8(ch->GetString(k).utf8_string().c_str()))});
        const int best = longest + 70;
        // Position via Qt relative to the page: GetScreenPosition returned a
        // stale x here (266 instead of 157 pt).
        const int cx = (c->GetHandle() && page->GetHandle())
                           ? c->GetHandle()->mapTo(page->GetHandle(), QPoint(0, 0)).x()
                           : c->GetScreenPosition().x - page->GetScreenPosition().x;
        // Whatever is to the right in the same row (e.g. "Choose Font...") keeps its
        // own width: subtract it from the available space.
        int rightOf = 0;
        if (c->GetHandle()) {
          const QRect cr = c->GetHandle()->geometry();
          for (wxWindow* sib : c->GetParent()->GetChildren()) {
            if (sib == c || !sib->IsShown() || !sib->GetHandle()) continue;
            const QRect sr = sib->GetHandle()->geometry();
            if (sr.left() >= cr.left() + cr.width() / 2 && sr.top() < cr.bottom() &&
                sr.bottom() > cr.top())
              rightOf += sib->GetBestSize().x + GetCharWidth();
          }
        }
        int room = pageW - cx - 2 * GetCharWidth() - rightOf;
        // In a static box (iOS card) also subtract the card's right margin; otherwise
        // the language choice was 404 pt and the box 12 pt wider than the page.
        // In wx the items are not children of the box but lie on top of it, so check by
        // position: is the centre of the choice inside a static box of the same parent.
        if (QWidget* cq = c->GetHandle())
          if (QWidget* par = cq->parentWidget())
            for (QGroupBox* gb :
                 par->findChildren<QGroupBox*>(Qt::FindDirectChildrenOnly))
              if (gb->isVisible() && gb->geometry().contains(cq->geometry().center())) {
                room -= 24;
                break;
              }
        // Right-aligned (WPL action under AIS): grows to the left, so the space is not
        // what lies to its right ("APRS position repo").
        if (wxSizer* cs = c->GetContainingSizer())
          if (wxSizerItem* si = cs->GetItem(c))
            if (si->GetFlag() & wxALIGN_RIGHT) room = std::max(room, pageW * 45 / 100);
        // Value in an iOS form row (fit_width.cpp): next to the label, within the
        // card edges; truncate the value rather than the label.
        if (c->GetHandle() && c->GetHandle()->property("ocpnios_formvalue").toBool())
          room = pageW - c->GetHandle()->property("ocpnios_formlabel").toInt() - 2 * 30 - 40;
        int nw = std::max(std::min(best, room), std::min(c->GetSize().x, room));
        if (c->GetHandle() && c->GetHandle()->property("ocpnios_formvalue").toBool())
          nw = std::min(best, room);
        if (room >= 6 * GetCharWidth() && nw != c->GetSize().x) {
          c->SetMinSize(wxSize(nw, -1));
          c->SetMaxSize(wxSize(nw, -1));
          c->SetSize(nw, -1);
        }
      }
      fitChoices(c);
    }
  };
  fitChoices(page);
  page->Layout();
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) sw->FitInside();
  // Still too wide: force the layout to the screen width; no horizontal
  // scrolling (unexpected on a phone).
  if (page->GetVirtualSize().x > pageW + 2 && page->GetSizer()) {
    const int h = page->GetSizer()->GetMinSize().y;
    page->GetSizer()->SetDimension(0, 0, pageW, h);
    page->SetVirtualSize(pageW, h);
  }
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) {
    int vx, vy;
    sw->GetViewStart(&vx, &vy);
    if (vx) sw->Scroll(0, vy);
  }
  for (wxWindow* c : page->GetChildren())
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(c)) {
      sw->Layout();
      sw->FitInside();
    }
  IosListHeights(page, tcDataSelected, m_scrollWinChartList, activeSizer);
  IosMmsiColumns(page);
  // Last: size buttons, fields and rows to their content (fit_width.cpp); otherwise
  // the rules above made a field narrower than its value (2147483646).
  ocpnios_FixControls(page);
  // Keep buttons and switches off the screen edge (o-charts shop panel).
  ocpnios_EdgeMargins(page);
}
#endif

#if defined(__OCPN__IOS__) && defined(OCPN_OPTIONS_USE_LISTBOOK)
// iOS: part of CreateControls, before the listbook is added to itemBoxSizer2.
void options::IosCreateHeader(options* itemDialog1, wxBoxSizer* itemBoxSizer2,
                              int border_size) {
  // iOS: back button above the page (only visible in page mode).
  m_iosBackButton = new wxButton(itemDialog1, wxID_ANY,
                                 wxString::FromUTF8("\u2039 ") + _("Options"),
                                 wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
  m_iosBackButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { IosBack(); });
  m_iosBackButton->Hide();
  // Header like an iOS navigation bar: back button left, title next to it.
  m_iosTitle = new wxStaticText(itemDialog1, wxID_ANY, "");
  {
    wxFont tf = m_iosTitle->GetFont();
    tf.SetWeight(wxFONTWEIGHT_BOLD);
    m_iosTitle->SetFont(tf);
  }
  // iOS-style header: "‹ Back" or "Cancel" on the left, "Apply" and "Done" on the
  // right; below it a large bold title. Replaces OK/Cancel/Apply at the bottom.
  // The buttons are created further down and inserted here (m_iosHead).
  m_iosHead = new wxBoxSizer(wxHORIZONTAL);
  m_iosHead->Add(m_iosBackButton, 0, wxALIGN_CENTER_VERTICAL);
  m_iosHead->AddStretchSpacer(1);
  // Just below the status bar; buttons are 44 pt high (touch_ui.cpp, ocpnios_nav).
  itemBoxSizer2->Add(m_iosHead, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border_size + 8);
  {
    wxFont tf = m_iosTitle->GetFont();
    tf.SetPointSize(tf.GetPointSize() * 14 / 10);
    m_iosTitle->SetFont(tf);
  }
  itemBoxSizer2->Add(m_iosTitle, 0, wxLEFT | wxTOP, 3 * border_size);
  // No close button (window_fit.cpp): Cancel is in the header.
  if (GetHandle()) {
    GetHandle()->setProperty("ocpnios_noclose", true);
    // Full-screen sheet, also in landscape (window_fit.cpp).
    GetHandle()->setProperty("ocpnios_sheet", 2);
  }
}

// iOS: part of CreateControls, right after the listbook is added.
void options::IosSetupNavLists() {
  // A tap on a category opens its page, also if it is the current one (no
  // PAGE_CHANGED then). Reapply our layout after every resize, because wxListbook
  // redistributes the space between list and page on resize.
  // Qt's clicked signal fires on every tap, also on the already selected category
  // (wx sends no event then).
  if (auto* tv = dynamic_cast<QAbstractItemView*>(
          m_pListbook->GetListView()->GetHandle())) {
    ocpnios_MakeNavList(tv);  // chevron and separator (app/ios/nav_list.cpp)
    QObject::connect(tv, &QAbstractItemView::clicked, tv,
                     [this](const QModelIndex&) {
                       if (m_iosListMode)
                         CallAfter([this]() { IosShowList(false); });
                     });
  }
  m_pListbook->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
    e.Skip();
    CallAfter([this]() { IosApplyLayout(); });
  });
  // List of the sections (tabs) of a category. Tabs with scroll arrows are awkward
  // on a phone; as in the iOS Settings app: another list, then the page.
  m_iosSubList = new wxListBox(m_pListbook, wxID_ANY);
  m_iosSubList->Hide();
  if (auto* sv = dynamic_cast<QAbstractItemView*>(m_iosSubList->GetHandle())) {
    ocpnios_MakeNavList(sv);
    QObject::connect(sv, &QAbstractItemView::clicked, sv,
                     [this](const QModelIndex& ix) {
                       const int row = ix.row();
                       if (m_iosLevel == 1)
                         CallAfter([this, row]() { IosOpenSubPage(row); });
                     });
  }
}

// iOS: part of CreateControls, after OK/Cancel/Apply are created.
void options::IosMoveButtonsToHeader(int border_size) {
  // iOS: move the buttons to the header (see m_iosHead).
  m_OK_Cancel_Apply_buttons->Detach(m_OKButton);
  m_OK_Cancel_Apply_buttons->Detach(m_CancelButton);
  m_OK_Cancel_Apply_buttons->Detach(m_ApplyButton);
  m_OKButton->SetLabel(_("Done"));
  {
    wxFont bf = m_OKButton->GetFont();
    bf.SetWeight(wxFONTWEIGHT_BOLD);
    m_OKButton->SetFont(bf);
  }
  // Cancel and Apply stay in the dialog (OpenCPN uses them) but are never shown: a
  // change counts at once, as in the iOS Settings app; Done closes.
  m_OK_Cancel_Apply_buttons->Add(m_CancelButton);
  m_OK_Cancel_Apply_buttons->Add(m_ApplyButton);
  m_CancelButton->Hide();
  m_ApplyButton->Hide();
  m_iosHead->Add(m_OKButton, 0, wxALIGN_CENTER_VERTICAL);
  // Direct apply: a switch, choice, radio button, slider or spin box that the user
  // changed is applied a moment later, as Apply did (OnApplyClick), without closing.
  // Only after a touch on the dialog: building a page may change controls from code.
  // Typed text counts on Done (or when a switch changes after it).
  {
    auto* lastTouch = new QElapsedTimer;  // lives as long as Options (deleted below)
    auto* pending = new QTimer;
    pending->setSingleShot(true);
    pending->setInterval(700);
    QObject::connect(pending, &QTimer::timeout, [this]() {
      if (!IsShown()) return;
      // Own ship at real scale with sizes still 0 or out of range: wait for Done, where
      // OpenCPN checks them (ApplyChanges). Applying at once showed its error message
      // right after choosing the icon type, before the sizes could be typed.
      if (m_pShipIconType && m_pShipIconType->GetSelection() > 0) {
        double len = 0, beam = 0, offx = 0, offy = 0;
        long minmm = 0;
        m_pOSLength->GetValue().ToDouble(&len);
        m_pOSWidth->GetValue().ToDouble(&beam);
        m_pOSGPSOffsetX->GetValue().ToDouble(&offx);
        m_pOSGPSOffsetY->GetValue().ToDouble(&offy);
        m_pOSMinSize->GetValue().ToLong(&minmm);
        if (len <= 0 || beam <= 0 || std::fabs(offx) > beam / 2.0 || offy < 0 || offy > len ||
            minmm <= 0 || minmm > 100)
          return;
      }
      wxLogMessage("ocpnios: Options applied after a change");
      wxCommandEvent ev(wxEVT_BUTTON, m_ApplyButton ? m_ApplyButton->GetId() : wxID_APPLY);
      OnApplyClick(ev);
    });
    if (QWidget* q = GetHandle()) {
      struct TouchFilter : QObject {
        QElapsedTimer* t;
        explicit TouchFilter(QElapsedTimer* tt, QObject* parent) : QObject(parent), t(tt) {}
        bool eventFilter(QObject* o, QEvent* e) override {
          const auto ty = e->type();
          if (ty == QEvent::MouseButtonRelease || ty == QEvent::TouchEnd ||
              ty == QEvent::KeyRelease)
            t->start();
          return false;
        }
      };
      qApp->installEventFilter(new TouchFilter(lastTouch, q));
    }
    auto changed = [this, lastTouch, pending](wxCommandEvent& e) {
      e.Skip();
      // A choice can show or hide rows (own ship at real scale, range rings). OpenCPN
      // then lays out the page but does not resize its scroll area: the new rows ran
      // over the ones below and the page did not scroll (iPad, Eigen Schip, 26 nov 2026).
      CallAfter([this]() {
        wxNotebook* nb = IosCurrentNotebook();
        wxWindow* page = nb ? nb->GetCurrentPage() : m_pListbook->GetCurrentPage();
        if (auto* sw = dynamic_cast<wxScrolledWindow*>(page)) {
          sw->Layout();
          sw->FitInside();
          sw->Refresh();
        }
      });
      wxLogDebug("ocpnios: Options change, touch %lld ms ago",
                 lastTouch->isValid() ? (long long)lastTouch->elapsed() : -1LL);
      if (lastTouch->isValid() && lastTouch->elapsed() < 1500 && IsShown())
        pending->start();
    };
    Bind(wxEVT_CHECKBOX, changed);
    Bind(wxEVT_RADIOBUTTON, changed);
    Bind(wxEVT_CHOICE, changed);
    Bind(wxEVT_COMBOBOX, changed);
    Bind(wxEVT_SLIDER, changed);
    Bind(wxEVT_SPINCTRL, [changed](wxSpinEvent& e) { changed(e); });
    Bind(wxEVT_SPINCTRLDOUBLE, [changed](wxSpinDoubleEvent& e) { changed(e); });
    Bind(wxEVT_DESTROY, [this, lastTouch, pending](wxWindowDestroyEvent& e) {
      e.Skip();
      if (e.GetEventObject() != this) return;
      pending->stop();
      pending->deleteLater();
      delete lastTouch;
    });
  }
  // Same style as the header of other dialogs (window_fit.cpp): blue text without
  // a frame (style rule in touch_ui.cpp).
  for (wxButton* b : {m_OKButton, m_iosBackButton})
    if (b && b->GetHandle()) {
      QWidget* q = b->GetHandle();
      q->setProperty("ocpnios_nav", true);
      // Already polished (wx queries the size on creation): polish again.
      q->style()->unpolish(q);
      q->style()->polish(q);
      b->InvalidateBestSize();
      b->SetMinSize(wxSize(std::max(64, q->sizeHint().width()), std::max(44, q->sizeHint().height())));
    }
}
#endif

#ifdef __OCPN__IOS__
// iOS: part of CreatePanel_Units (compact layout). As a row in the same form
// (label left, field and unit right) instead of a separate box with a 100 pt
// indent and a large empty band below. First complete the "Show magnetic" row
// (it ends with one empty cell in the left column).
void options::IosAddMagVarRow(wxScrolledWindow* panelUnits,
                              wxFlexGridSizer* unitsSizer,
                              int group_item_spacing) {
  unitsSizer->Add(new wxStaticText(panelUnits, wxID_ANY, ""));
  itemStaticTextUserVar =
      new wxStaticText(panelUnits, wxID_ANY, _("Assumed magnetic variation"));
  unitsSizer->Add(itemStaticTextUserVar, labelFlags);
  wxBoxSizer* magVarSizer = new wxBoxSizer(wxHORIZONTAL);
  pMagVar = new wxTextCtrl(panelUnits, ID_OPTEXTCTRL, "", wxDefaultPosition,
                           wxSize(GetCharWidth() * 7, -1), wxTE_RIGHT);
  magVarSizer->Add(pMagVar, 0, wxALIGN_CENTRE_VERTICAL);
  // Short unit ("degrees (-W, +E)" wrapped onto two lines and reached the card edge).
  itemStaticTextUserVar2 =
      new wxStaticText(panelUnits, wxID_ANY, wxString::FromUTF8("\u00b0 (-W, +E)"));
  magVarSizer->Add(itemStaticTextUserVar2, 0, wxLEFT | wxALIGN_CENTRE_VERTICAL,
                   group_item_spacing);
  unitsSizer->Add(magVarSizer, inputFlags);
}

// iOS: show tide datasets by file name instead of the full app container path
// (unreadably long). The full path is restored on save (options::ApplyChanges).
static std::map<wxString, wxString> s_iosTcFull;
wxString ocpnios_TcShow(const wxString& full) {
  wxString shown = wxFileName(full).GetFullName();
  if (shown.empty()) return full;
  auto it = s_iosTcFull.find(shown);
  if (it != s_iosTcFull.end() && it->second != full) return full;  // duplicate name
  s_iosTcFull[shown] = full;
  return shown;
}
wxString ocpnios_TcFull(const wxString& shown) {
  auto it = s_iosTcFull.find(shown);
  return it != s_iosTcFull.end() ? it->second : shown;
}
#endif
