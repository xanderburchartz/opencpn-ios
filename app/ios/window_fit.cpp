// Vensters op iOS passend maken en naar voren halen.
//
// Op iOS is elk wx-topvenster (wxDialog, wxFrame) een eigen Qt-topvenster in hetzelfde
// scherm. OpenCPN maakt dialogen op desktopmaat (bv. Opties 793x757 pt op een scherm van
// 440 pt breed) en zet ze deels buiten beeld (x=-353). Bovendien komt een niet-modaal
// venster (Opties via Show()) niet boven het hoofdvenster: het is "exposed" maar
// onzichtbaar. Gemeten in lldb op de iPhone 17 Pro Max-simulator (8 nov 2026).
//
// Deze event-filter op QApplication grijpt in bij elk Show-event van een topvenster:
//  - breedte en hoogte niet groter dan het beschikbare schermdeel;
//  - positie zo dat het venster helemaal in beeld staat;
//  - daarna (na de huidige event-ronde) raise() en activateWindow().
// Menu's, tooltips en andere popups blijven ongemoeid.
// Uitzetten met env OCPNIOS_NO_WINDOW_FIT.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFontMetrics>
#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QGridLayout>
#include <QMenu>
#include <QVBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QLineF>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizeGrip>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QAbstractButton>
#include <QComboBox>
#include <QTabBar>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSlider>
#include <QAbstractItemView>
#include <QInputMethod>
#include <QLabel>
#include <QProgressBar>
#include <QFocusEvent>
#include <QAbstractSpinBox>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

#include <wx/app.h>
#include <wx/display.h>
#include <wx/aui/floatpane.h>
#include <wx/button.h>
#include <wx/bookctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/radiobut.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/progdlg.h>
#include <wx/wrapsizer.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/window.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "tooltip.h"  // OpenCPN-tooltips verbergen

#include <algorithm>
#include <vector>
#include <cstdlib>

void ocpnios_FitToWidth(wxWindow* root);  // fit_width.cpp
void ocpnios_RelayoutOverlays();  // frame_ios.cpp
void ocpnios_ListButtons(wxWindow* top);  // connections_ios.cpp
void ocpnios_RoundWindow(QWidget* w, double radius);  // action_sheet.mm
QRect ocpnios_ToolbarRect();  // toolbar_ios.cpp
void ocpnios_ShowMenuSheet(const QList<QAction*>& menus, QWidget* anchor,
                           const QString& cancel);  // action_sheet.mm
void ocpnios_AlertLook(QWidget* dialog);  // fit_width.cpp
void ocpnios_AlertStyle(QWidget* dialog);  // fit_width.cpp
void ocpnios_HideStartupBusy();            // startup_busy.mm
QRect ocpnios_AppWindowRect();            // app_window.mm
bool ocpnios_AppIsWindowed();             // app_window.mm
bool ocpnios_AppIsOnMac();                // app_window.mm

namespace {

// Kopbalk van een venster (zie EnsureSheetHeader).
const char* const kHeaderName = "ocpnios_sheet_header";
const char* const kSheetProp = "ocpnios_sheet";  // 0 = geen, 1 = blad, 2 = schermvullend
// Hoog genoeg voor tikvlakken van 44 pt plus ruimte onder de statusbalk (Xander, 10 nov
// 2026: knoppen te hoog en te klein, bijna de iPad-statusbalk geraakt).
constexpr int kHeaderHeight = 64;

bool IsManagedWindow(QWidget* w) {
  if (!w || !w->isWindow()) return false;
  switch (w->windowType()) {
    case Qt::Popup:
    case Qt::ToolTip:
    case Qt::SplashScreen:
    case Qt::SubWindow:
    case Qt::Desktop:
      return false;
    default:
      return true;
  }
}

// Bruikbaar schermdeel. availableGeometry() van het scherm loopt op iOS onder de
// statusbalk en het eiland door. Het hoofdvenster (QMainWindow van wxFrame) houdt daar
// wel rekening mee: van de bovenkant van de menubalk tot de onderkant van het
// centrale widget. Gebruik dat als het er is.
bool IsMainWindow(QWidget* w);

QRect UsableArea(QWidget* w) {
  QScreen* screen = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
  QRect avail = screen ? screen->availableGeometry() : QRect();
  // Venstermodus (iPadOS 26): alleen het eigen app-venster (app_window.mm).
  const QRect appWin = ocpnios_AppWindowRect();
  if (appWin.isValid() && avail.isValid() &&
      (appWin.width() < avail.width() || appWin.height() < avail.height()))
    avail = avail.intersected(appWin);
  bool haveMain = false;
  for (QWidget* top : QApplication::topLevelWidgets()) {
    auto* mw = qobject_cast<QMainWindow*>(top);
    if (!mw || mw == w || !mw->isVisible() || !mw->centralWidget()) continue;
    // Alleen het hoofdvenster van OpenCPN; een ander los wxFrame (bv. een
    // instrumentpaneel) gaf hier een gebied van 401 pt (Over half zo hoog, 10 nov 2026).
    if (!IsMainWindow(mw)) continue;
    // menuWidget(), niet menuBar(): menuBar() maakt een lege menubalk aan als er geen is.
    QWidget* mb = mw->menuWidget();
    QWidget* first = mb && mb->isVisible() ? mb : mw->centralWidget();
    const int top_y = first->mapToGlobal(QPoint(0, 0)).y();
    QWidget* c = mw->centralWidget();
    int bottom_y = c->mapToGlobal(QPoint(0, c->height())).y();
    // Schermvullend blad (Instellingen): ook over de statusregel (SOG/COG) heen; die liep
    // als vreemde strook onder het blad door (Xander, 10 nov 2026).
    if (w->property(kSheetProp).toInt() == 2)
      bottom_y = mw->mapToGlobal(QPoint(0, mw->height() - mw->contentsMargins().bottom())).y();
    if (bottom_y - top_y > 100) {
      QRect r(avail.left(), top_y, avail.width(), bottom_y - top_y);
      avail = avail.isValid() ? avail.intersected(r) : r;
    }
    // Liggend: links of rechts zit het eiland. De kaart loopt daar onder door, een
    // venster niet (Dashboard-voorkeuren: eiland over tabblad en lijst, 10 nov 2026).
    if (QWindow* qw = mw->windowHandle()) {
      const QMargins m = qw->safeAreaMargins();
      if (m.left() > 0 || m.right() > 0)
        avail.adjust(m.left(), 0, -m.right(), 0);
    }
    haveMain = true;
    break;
  }
  // Zonder hoofdvenster (opstartvensters): statusbalk en thuisbalk zelf vrijhouden.
  // Op de iPad viel de kopbalk van "Safe Restart" anders onder de statusbalk
  // (10 nov 2026). Alleen dan: de veilige zone van een venster dat (nog) deels buiten
  // beeld staat is onbetrouwbaar en maakte bladen half zo hoog (zelfde dag).
  if (!haveMain && avail.isValid()) {
    if (QWindow* qw = w->windowHandle()) {
      const QMargins m = qw->safeAreaMargins();
      if (w->y() <= avail.top() && avail.top() < m.top())
        avail.adjust(0, m.top(), 0, -m.bottom());
    }
  }
  return avail;
}

// Melding van Qt zelf (wxMessageBox) met een lange tekst: Help in de
// Weerrouterings-instellingen was hoger dan het scherm, de OK-knop viel eronder en je
// kon niet meer terug (Xander, 11 nov 2026). De tekst gaat dan in een schuifvlak en de
// melding past op het scherm, in het midden.
void FitMessageBox(QMessageBox* mb) {
  // iOS-meldingen hebben geen pictogram; het desktop-uitroepteken en het rode kruis
  // (wxICON_WARNING/ERROR, bijv. de poortwaarschuwing in de verbindingseditor) vallen weg.
  if (mb->icon() != QMessageBox::NoIcon) mb->setIcon(QMessageBox::NoIcon);
  const QRect avail = UsableArea(mb);
  if (!avail.isValid()) return;
  const int maxH = avail.height() - 32;
  auto* label = mb->findChild<QLabel*>("qt_msgbox_label");
  auto* grid = qobject_cast<QGridLayout*>(mb->layout());
  // QMessageBox zet bij tonen zelf een vaste maat (updateSize); daarom op de werkelijke
  // hoogte letten, niet alleen op sizeHint.
  const int h = std::max(mb->height(), mb->sizeHint().height());
  if (label && grid && h > maxH &&
      !qobject_cast<QScrollArea*>(label->parentWidget() ? label->parentWidget()->parentWidget()
                                                        : nullptr)) {
    auto* sa = new QScrollArea(mb);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    label->setWordWrap(true);
    grid->replaceWidget(label, sa);
    sa->setWidget(label);
    // Ruimte voor knoppen en marges (~140 pt). Qt rekent de melding daarna zelf opnieuw
    // uit (LayoutRequest) met dit vaste schuifvlak.
    sa->setFixedHeight(std::max(120, maxH - 140));
    sa->setMinimumWidth(std::min(avail.width() - 120, 420));
    sa->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
  }
  // Midden op het scherm, binnen het bruikbare deel.
  QPointer<QMessageBox> g(mb);
  QTimer::singleShot(0, mb, [g]() {
    if (!g) return;
    const QRect a = UsableArea(g);
    if (!a.isValid()) return;
    // Niet breder dan het scherm met een rand ernaast; met kader zoals een melding.
    if (g->width() > a.width() - 32) g->setFixedWidth(a.width() - 32);
    if (!g->property("ocpnios_mbframe").toBool()) {
      g->setProperty("ocpnios_mbframe", true);
      g->setStyleSheet(g->styleSheet() + "QMessageBox { border: 1px solid #a0a0a8; }");
    }
    QRect r = g->geometry();
    r.moveCenter(a.center());
    if (r.top() < a.top()) r.moveTop(a.top());
    g->move(r.topLeft());
  });
}

// Instrumentpaneel (soort 4) als iOS-kaart met afgeronde hoeken. Via de laag van het
// UIView (action_sheet.mm): een QWidget-masker gaf op de iPhone kleine, hoekige trapjes
// (simulator 11 nov 2026).
void RoundPanel(QWidget* w) { ocpnios_RoundWindow(w, 10); }

// Groot scherm (iPad, iPad-app op de Mac): ook de korte kant minstens 700 pt.
bool LargeScreen(const QRect& avail) { return std::min(avail.width(), avail.height()) >= 700; }

// Breedte van een blad. iPad: als een iOS-blad in het midden, hooguit 720 pt, ook
// liggend; schermvullend oogde als een desktopvenster (Xander 26 nov 2026: "bij kantelen
// zijn de menuschermen op de iPad helemaal schermvullend"). iPhone: de volle breedte.
int SheetWidth(const QRect& avail, int sheet) {
  if (sheet == 3 || !LargeScreen(avail)) return avail.width();
  return std::min(avail.width() - 40, 720);
}

// Ruimte boven een groot blad, zodat de kaart erachter zichtbaar blijft zoals bij iOS.
int SheetTopGap(const QRect& avail) { return LargeScreen(avail) ? 24 : 8; }

void FitToScreen(QWidget* w) {
  const QRect avail = UsableArea(w);
  if (!avail.isValid()) return;
  QRect g = w->geometry();
  // iOS-blad (zie EnsureSheetHeader): volle breedte, groot venster ook volle hoogte.
  const int sheet = w->property(kSheetProp).toInt();
  if (sheet >= 1 && sheet <= 3) {
    // wx/Qt geven het venster bij openen een maximale maat gelijk aan het scherm van
    // dat moment (staand 440 breed); liggend bleef het blad daardoor 440 breed (gemeten
    // 10 nov 2026). Grens opheffen.
    if (w->maximumWidth() < avail.width() || w->maximumHeight() < avail.height()) {
      w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
      if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(w)) ww->SetMaxSize(wxDefaultSize);
    }
    g.setWidth(SheetWidth(avail, sheet));
    // wx zet bij Fit() een minimum gelijk aan de desktopmaat (Tactics: 450 pt op 440).
    if (w->minimumWidth() > avail.width() || w->minimumHeight() > avail.height()) {
      // Ook de hoogte: VDR viel anders over de statusregel; wat niet past schuift
      // (UpdateSheetScroll).
      const int mw = std::min(w->minimumWidth(), avail.width());
      const int mh = std::min(w->minimumHeight(), avail.height());
      w->setMinimumSize(mw, mh);
      if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(w)) ww->SetMinSize(wxSize(mw, mh));
    }
  }
  if (sheet == 2) g.setHeight(avail.height() - SheetTopGap(avail));
  if (sheet == 3) {
    // Paneel onderin: zo hoog als de inhoud (de kopbalk zit als lege regel in de
    // sizer), hooguit de helft van het scherm (liggend 70%); wat niet past schuift.
    // Liggend niet schermbreed maar hooguit 520 pt, zodat de kaart zichtbaar blijft
    // (GRIB liggend dekte de kaart bijna helemaal af, 10 nov 2026).
    const bool landscape = avail.width() > avail.height();
    if (landscape) g.setWidth(std::min(avail.width(), 520));
    // Een zwevend AUI-paneel staat staand hooguit op 60%: de Navico-stuurautomaat
    // (koersboog, twee rijen standen, koersknoppen, melding) heeft op de iPhone ruim de
    // helft nodig (13 nov 2026).
    const int maxPct = landscape ? 70 : 50;
    if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(w))
      if (ww->GetSizer()) {
        // Bij een wxFrame (QMainWindow, zoals het zwevende AUI-venster) zit de kopbalk
        // niet in de sizer maar is hij de menuWidget: erbij tellen. Zonder kreeg de
        // Navico-stuurautomaat 66 pt te weinig en werd de koersboog platgedrukt.
        const bool aui = dynamic_cast<wxAuiFloatingFrame*>(ww) != nullptr;
        const int extra = qobject_cast<QMainWindow*>(w) ? kHeaderHeight : 0;
        const int want = std::min(ww->GetSizer()->GetMinSize().y + extra + 4,
                                  avail.height() * (aui && !landscape ? 60 : maxPct) / 100);        if (w->minimumHeight() > want) {
          w->setMinimumHeight(0);
          ww->SetMinSize(wxSize(ww->GetMinSize().x, -1));
        }
        g.setHeight(want);
      } else if (dynamic_cast<wxAuiFloatingFrame*>(ww)) {
        // Zwevend AUI-paneel: geen eigen sizer; de hoogte van het paneel erin plus de
        // kopbalk (bij een QMainWindow de menuWidget).
        int inner = 0;
        for (wxWindow* c : ww->GetChildren())
          if (!c->IsTopLevel() && c->IsShown()) inner = std::max(inner, c->GetBestSize().y);
        if (inner > 0) {
          const int extra = qobject_cast<QMainWindow*>(w) ? kHeaderHeight : 0;
          g.setHeight(std::min(inner + extra + 4,
                               avail.height() * (landscape ? 70 : 60) / 100));
          if (w->minimumHeight() > g.height()) w->setMinimumHeight(0);
        }
      }
  }
  int width = std::min(g.width(), avail.width());
  const int height = std::min(g.height(), avail.height());
  const int x = std::clamp(g.x(), avail.left(), avail.left() + avail.width() - width);
  int y = std::clamp(g.y(), avail.top(), avail.top() + avail.height() - height);
  int xx = x;
  if (sheet == 3) {
    // Paneel onderin. Meer panelen tegelijk (Stuurautomaat en pypilot) lagen precies
    // over elkaar (iPad, 10 nov 2026). Nu een stapel: het eerst geopende onderaan, een
    // later geopend paneel erboven; liggend eerst naast elkaar zolang dat past.
    const bool landscape = avail.width() > avail.height();
    const qint64 mine = w->property("ocpnios_shownseq").toLongLong();
    std::vector<QWidget*> earlier;
    for (QWidget* o : QApplication::topLevelWidgets()) {
      if (o == w || !o->isVisible() || o->property(kSheetProp).toInt() != 3) continue;
      const qint64 seq = o->property("ocpnios_shownseq").toLongLong();
      if (mine > 0 && seq > mine) continue;
      earlier.push_back(o);
    }
    std::sort(earlier.begin(), earlier.end(), [](QWidget* a, QWidget* b) {
      return a->property("ocpnios_shownseq").toLongLong() <
             b->property("ocpnios_shownseq").toLongLong();
    });
    int beside = 0, below = 0;
    for (QWidget* o : earlier) {
      if (landscape && beside + o->width() + width <= avail.width())
        beside += o->width();
      else
        below = std::max(below, avail.top() + avail.height() - o->y());
    }
    y = std::max(avail.top(), avail.top() + avail.height() - height - below);
    if (landscape) xx = avail.left() + (below > 0 ? 0 : beside);
  }
  // Blad: in het midden; een groot blad (soort 2) met een kier bovenaan, zodat het als
  // blad over de kaart ligt en niet als schermvullend venster.
  if (sheet == 1 || sheet == 2) xx = avail.left() + (avail.width() - width) / 2;
  if (sheet == 2) y = avail.top() + SheetTopGap(avail);
  // Zwevend paneel dat vrij moet staan (Navico-stuurautomaat): de eerste keer onderin,
  // rechts van de werkbalk; past het daar niet, dan onder de werkbalk. Daarna houdt het
  // de plek waar de gebruiker het heen sleept.
  if (sheet == 4 && w->property("ocpnios_place_clear").toBool() &&
      !w->property("ocpnios_placed").toBool()) {
    w->setProperty("ocpnios_placed", true);
    const QRect tb = ocpnios_ToolbarRect();
    xx = avail.left() + 8;
    y = avail.top() + avail.height() - height - 8;
    if (tb.isValid()) {
      // Rechts van de werkbalk; op een smal scherm (iPhone staand) zo smal als de ruimte
      // daar, zodat de werkbalk met MOB bereikbaar blijft.
      const int room = avail.left() + avail.width() - 8 - (tb.right() + 8);
      xx = tb.right() + 8;
      if (width > room && room >= 260) {
        width = room;
        if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(w)) ww->SetMinSize(wxSize(-1, -1));
        if (w->minimumWidth() > width) w->setMinimumWidth(width);
      }
    }
  }
  // Melding (ocpnios_alert): in het midden, ook na verschalen van het app-venster.
  if (sheet == 0 && w->property("ocpnios_alert").toBool()) {
    xx = avail.left() + (avail.width() - width) / 2;
    y = avail.top() + (avail.height() - height) / 2;
  }
  const QRect fitted(xx, y, width, height);
  if (fitted != w->geometry()) w->setGeometry(fitted);
  // Afgeronde hoeken voor bladen en panelen onderin, zoals iOS-bladen (Xander 26 nov
  // 2026: "dan zouden het wel afgeronde hoeken moeten zijn", "van elk veld dat zweeft
  // afgeronde hoeken"). Via de laag van het venster, zoals RoundPanel.
  if (sheet >= 1 && sheet <= 3 && !w->property("ocpnios_rounded").toBool()) {
    w->setProperty("ocpnios_rounded", true);
    QPointer<QWidget> guard(w);
    QTimer::singleShot(0, w, [guard]() { if (guard) ocpnios_RoundWindow(guard, 12); });
  }
}

// Kolomkoppen van lijsten (wxListCtrl): OpenCPN geeft kolommen een breedte in tekens
// van de desktopletter; met de grotere iOS-letter werden koppen afgekapt ("/olgmodu",
// "cheepsnaa" in MMSI Eigenschappen, iPad, 10 nov 2026). Elke kolom minstens zo breed
// als zijn kop; breder mag (de gebruiker of OpenCPN), smaller niet.
void WidenHeaderSections(QHeaderView* h) {
  if (!h || h->orientation() != Qt::Horizontal || !h->model()) return;
  const QFontMetrics fm(h->font());
  for (int i = 0; i < h->count(); ++i) {
    if (h->isSectionHidden(i) || h->sectionSize(i) == 0) continue;
    const QString text = h->model()->headerData(i, Qt::Horizontal).toString();
    if (text.isEmpty()) continue;
    const int need = fm.horizontalAdvance(text) + 24;
    if (h->sectionSize(i) < need) h->resizeSection(i, need);
  }
}

// Voortgangsvenster: binnen beeld en in het midden. wx maakt het breder als de melding
// langer wordt (Fit() in wxGenericProgressDialog::UpdateMessage) en laat de linkerkant
// staan; liggend op de iPad viel het zo rechts buiten beeld (Xander, 10 nov 2026).
void CenterProgress(QWidget* w) {
  const QRect avail = UsableArea(w);
  if (!avail.isValid()) return;
  const int width = std::min(w->width(), avail.width() - 16);
  const int height = std::min(w->height(), avail.height());
  if (w->minimumWidth() > width) w->setMinimumWidth(width);
  const int x = avail.left() + (avail.width() - width) / 2;
  const int y = avail.top() + (avail.height() - height) / 2;
  const QRect want(x, y, width, height);
  if (want != w->geometry()) w->setGeometry(want);
}

// Voortgangsmelding: OpenCPN zet hier het volledige pad van de kaart of map in
// ("/Users/.../Documents/Kaarten"). Een iOS-voortgangsmelding noemt alleen wat er
// gebeurt; de bestandsnaam is genoeg. De tekst wordt bij elke Update() opnieuw gezet,
// dus bij het tekenen inkorten tot het laatste deel van het pad.
class ProgressPathFilter : public QObject {
public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::Paint) {
      if (auto* l = qobject_cast<QLabel*>(o)) {
        const QString t = l->text();
        const int slash = t.lastIndexOf('/');
        if (slash >= 0 && slash < t.size() - 1) l->setText(t.mid(slash + 1));
      }
    }
    return false;
  }
};

// Voortgangsvenster in de vorm van een iOS-melding (Xander, 26 nov 2026: "vlak daarvoor
// kwam nog een andere OpenCPN melding ... en dat was niet in deze stijl", het bijwerken
// van de kaartdatabase op de Mac). Zelfde opmaak als ocpnios_AlertStyle: afgerond,
// lichtgrijs, geen kader. De venstertitel staat vet in het midden (iOS tekent geen
// titelbalk), de melding eronder in het midden, een dun blauw balkje, en zonder de
// regels met verstreken, geschatte en resterende tijd. Geldt voor alle
// wxGenericProgressDialog's (kaartdatabase, SENC, downloads, GPX-import).
void ProgressLook(QWidget* dialog, wxGenericProgressDialog* pd) {
  if (dialog->property("ocpnios_progresslook").toBool()) return;
  dialog->setProperty("ocpnios_progresslook", true);
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  ocpnios_AlertStyle(dialog);
  dialog->setAttribute(Qt::WA_StyledBackground, true);
  dialog->setStyleSheet(
      dialog->styleSheet() +
      QString(" QDialog#ocpnios_alert_dialog QProgressBar { border: none; background: %1;"
              "   border-radius: 2px; min-height: 4px; max-height: 4px; }"
              " QDialog#ocpnios_alert_dialog QProgressBar::chunk { background: %2;"
              "   border-radius: 2px; }")
          .arg(dark ? "#48484a" : "#d1d1d6", dark ? "#d06060" : "#007aff"));
  for (QProgressBar* bar : dialog->findChildren<QProgressBar*>()) bar->setTextVisible(false);

  wxSizer* top = pd->GetSizer();
  if (!top) return;
  // Tijdregels weg: dat is het enige rooster (wxFlexGridSizer) in het venster.
  for (wxSizerItem* item : top->GetChildren()) {
    if (item->IsSizer() && dynamic_cast<wxFlexGridSizer*>(item->GetSizer()))
      item->Show(false);
  }
  // Melding over de hele breedte, tekst in het midden; paden worden bestandsnamen.
  for (wxSizerItem* item : top->GetChildren()) {
    auto* st = item->IsWindow() ? dynamic_cast<wxStaticText*>(item->GetWindow()) : nullptr;
    if (!st) continue;
    item->SetFlag(item->GetFlag() | wxEXPAND);
    if (auto* ql = qobject_cast<QLabel*>(st->GetHandle())) {
      ql->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
      ql->setWordWrap(true);
      ql->installEventFilter(new ProgressPathFilter(ql));
    }
  }
  // Titel bovenaan, vet, zoals de kop van een iOS-melding.
  const wxString title = pd->GetTitle();
  if (!title.empty()) {
    auto* head = new wxStaticText(pd, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                                  wxALIGN_CENTRE_HORIZONTAL);
    wxFont f = head->GetFont();
    f.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    head->SetFont(f);
    if (auto* ql = qobject_cast<QLabel*>(head->GetHandle()))
      ql->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    top->Insert(0, head, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);
  }
  // Smal zoals een iOS-melding (270 pt). wx maakt het venster bij een langere melding
  // breder (Fit() in UpdateMessage, het label groeit mee met het volledige pad); een
  // maximale breedte op het Qt-venster houdt het smal, de sizer legt het label dan
  // binnen die breedte neer (wxEXPAND).
  const QRect avail = UsableArea(dialog);
  const int maxw = avail.isValid() ? std::min(300, avail.width() - 32) : 300;
  for (wxSizerItem* item : top->GetChildren()) {
    if (auto* st = item->IsWindow() ? dynamic_cast<wxStaticText*>(item->GetWindow()) : nullptr)
      st->SetMinSize(wxSize(1, -1));
  }
  dialog->setMaximumWidth(maxw);
  dialog->setMinimumWidth(std::min(270, maxw));
  pd->Fit();
  pd->Layout();
}

// Kopbalk (10 nov 2026, op verzoek van Xander, vervangt het ronde sluitkruisje): iOS
// tekent geen titelbalk. Elk venster krijgt bovenin een balk zoals een iOS-blad:
// links "Annuleren", in het midden de titel, rechts "Gereed" (vet). Die knoppen
// drukken de eigen OK/Sluiten- en Annuleren-knoppen van het wx-venster in; de originelen
// worden verborgen. Zonder eigen knop sluit "Gereed" het venster (QWidget::close(), wat
// wxQt een wxCloseEvent maakt). Zie EnsureSheetHeader.

void ElideTitle(QWidget* header);

void PlaceHeader(QWidget* dialog, QWidget* header) {
  header->setGeometry(0, 0, dialog->width(), kHeaderHeight);
  // Venstermodus: links de vensterknopjes (●●●) vrijhouden ("Annuleren" lag eronder).
  if (QLayout* l = header->layout()) {
    const QMargins m = l->contentsMargins();
    const int left = (dialog->x() < 40 && ocpnios_AppIsWindowed()) ? 76 : 6;
    if (m.left() != left) l->setContentsMargins(left, m.top(), m.right(), m.bottom());
  }
  header->raise();
  ElideTitle(header);
}

void ElideTitle(QWidget* header) {
  // Lange titel rechts afkappen met "…" ("Beheer routes, tracks en markeringspunten"
  // werd aan beide kanten afgesneden, 10 nov 2026).
  if (header->layout()) header->layout()->activate();  // titelbreedte bijwerken
  if (auto* title = header->findChild<QLabel*>("ocpnios_sheet_title")) {
    const QString full = title->property("ocpnios_full").toString();
    const int room = std::max(40, title->width() - 4);
    title->setText(title->fontMetrics().elidedText(full, Qt::ElideRight, room));
  }
}

// De titel in de kopbalk volgt de titel van het venster. De GRIB-balk zet zijn titel
// pas na het openen van een bestand; de kopbalk bleef "Geen geldig GRIB-bestand" tonen
// (iPhone-simulator, 11 nov 2026).
class TitleFollower : public QObject {
 public:
  TitleFollower(QWidget* dialog, QWidget* header) : QObject(dialog), m_header(header) {}
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() == QEvent::WindowTitleChange && m_header) {
      auto* w = static_cast<QWidget*>(obj);
      if (auto* title = m_header->findChild<QLabel*>("ocpnios_sheet_title")) {
        title->setProperty("ocpnios_full", w->windowTitle());
        title->setText(w->windowTitle());
        ElideTitle(m_header);
      }
    }
    return false;
  }

 private:
  QPointer<QWidget> m_header;
};

// Het hoofdvenster van OpenCPN (wx-topvenster) krijgt geen sluitknop en telt niet als
// zwevend venster. Niet op maat herkennen: tijdens draaien is het even kleiner dan het
// scherm, en kreeg het zo een sluitknop die de app afsluit (simulator, 8 nov 2026).
bool IsMainWindow(QWidget* w) {
  wxWindow* top = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
  if (top && top->GetHandle()) return top->GetHandle()->window() == w;
  QScreen* screen = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
  if (!screen) return false;
  const QRect s = screen->geometry();
  return w->width() >= s.width() - 2 && w->height() >= s.height() * 9 / 10;
}

// Veilige zones (eiland, statusbalk, thuisbalk, afgeronde hoeken) voor het hoofdvenster:
// boven en onder houdt het venster ruimte vrij (anders valt de menubalk staand onder de
// statusbalk, en verdween de tekst van de statusregel: OpenCPN geeft die een vaste
// hoogte). Links en rechts loopt de kaart door tot de rand, wat liggend ~125 pt breedte
// oplevert; menubalk en statusregel krijgen daar zelf binnenmarges zodat hun tekst
// binnen de veilige zone blijft.
// Overlays on the chart (toolbar, compass, zoom bar, chart bar, scale bar) are placed by
// OpenCPN, which asks ocpnios_OverlayInsetLeft/Right below how far to stay from the edge.
// s_safeArea/s_orient: last values seen, to re-lay out after a change.
QMargins s_safeArea;
int s_orient = -1;

void ApplySafeArea(QWidget* w) {
  QWindow* qw = w->windowHandle();
  auto* mw = qobject_cast<QMainWindow*>(w);
  if (!qw || !mw) return;
  const QMargins s = qw->safeAreaMargins();
  // Liggend zit het eiland links bij InvertedLandscape (toestel linksom gedraaid) en
  // rechts bij Landscape (gemeten in de simulator, 8 nov 2026).
  const Qt::ScreenOrientation o =
      qw->screen() ? qw->screen()->orientation() : Qt::PrimaryOrientation;
  // The chart runs to the screen edge on both sides, also under the island; overlays
  // keep clear of it themselves (see OverlayInset).
  const int left = 0;
  // Venstermodus (iPadOS 26) zonder menubalk: linksboven liggen de vensterknopjes (●●●)
  // over de werkbalk; bovenin een strook vrijhouden.
  // Gemeten (iPad-simulator, 10 nov 2026): de knopjes reiken tot ~64 pt onder de bovenrand.
  const int top = (ocpnios_AppIsWindowed() && !ocpnios_AppIsOnMac() && !mw->menuWidget())
                      ? std::max(s.top(), 66)
                                                                   : s.top();
  w->setContentsMargins(left, top, 0, s.bottom());
  if (s != s_safeArea || int(o) != s_orient) {
    s_safeArea = s;
    s_orient = int(o);
    // After rotating, the new safe area can arrive after OpenCPN has placed the bars
    // (zoom bar in the middle in portrait, build 5). Place them again now, and once more
    // after the rotation animation, when orientation and safe area have settled.
    QTimer::singleShot(0, []() { ocpnios_RelayoutOverlays(); });
    QTimer::singleShot(500, []() { ocpnios_RelayoutOverlays(); });
  }
  // Menubalk en statusregel: tekst buiten de afgeronde hoeken houden. contentsMargins
  // wordt door QMenuBar genegeerd, daarom een stylesheet-padding.
  if (QWidget* mb = mw->menuWidget()) {
    // Venstermodus (iPadOS 26): linksboven staan de vensterknopjes (●●●) over de
    // menubalk; "Navigeren" lag eronder (simulator, 10 nov 2026). Ruimte vrijhouden.
    const int ctl = ocpnios_AppIsWindowed() ? 76 : 0;
    mb->setStyleSheet(QString("QMenuBar { padding-left: %1px; padding-right: %2px; }")
                          .arg(std::max(ctl, s.left() - left))
                          .arg(s.right()));
  }
  if (QWidget* sb = mw->findChild<QStatusBar*>(QString(), Qt::FindDirectChildrenOnly))
    sb->setContentsMargins(std::max(0, s.left() - left), 0, s.right(), 0);
}

// Maat die een venster vroeg toen het getoond werd (vóór inkorten).
const char* const kWantedSizeProp = "ocpnios_wanted_size";

// Een zwevend venster boven de kaart: een dialoog, of een los venster met titel (bv.
// AIS Target Query, Data Monitor, Tide). Het hoofdvenster en titelloze hulpvensters
// tellen niet mee.
bool IsFloatingWindow(QWidget* w) {
  if (!IsManagedWindow(w) || !w->isVisible()) return false;
  const Qt::WindowType type = w->windowType();
  if (type == Qt::Dialog) return true;
  if (type != Qt::Window && type != Qt::Tool) return false;
  return !IsMainWindow(w) && !w->windowTitle().isEmpty();
}

// Qt op iOS stapelt topvensters op niveau (QIOSWindow::windowLevel): gewone vensters
// (Qt::Window) delen het niveau met het hoofdvenster, Qt::Tool ligt erboven, menu's
// (Qt::Popup) daar weer boven. Een los wx-venster (wxFrame, bv. AIS Target Query, Data
// Monitor) is een Qt::Window en raakte bij draaien achter het hoofdvenster, ook na
// raise(); de AIS-doelenlijst (Qt::Tool) niet (simulator, 8 nov 2026). Daarom krijgt het
// platformvenster (QWindow) van zo'n los venster het type Qt::Tool. Het widget zelf
// blijft ongewijzigd, zodat wx er niets van merkt.
// Ook niet-modale dialogen (Qt::Dialog, laag 0 op iOS): de route-eigenschappen vanuit de
// Route & Mark Manager (Tool, laag 10) openden er anders onder (8 nov 2026). Modale
// vensters liggen op iOS al hoger (40), menu's op 30.
void RaiseWindowLevel(QWidget* w) {
  const Qt::WindowType type = w->windowType();
  if (type == Qt::Dialog) {
    if (w->isModal()) return;
  } else if (type != Qt::Window || IsMainWindow(w) || w->windowTitle().isEmpty()) {
    return;
  }
  QWindow* qw = w->windowHandle();
  if (!qw || qw->type() == Qt::Tool) return;
  qw->setFlags((qw->flags() & ~Qt::WindowType_Mask) | Qt::Tool);
}

// Een dialoog die liggend werd geopend, heeft van wx een minimumbreedte gekregen
// (tekst op één regel). Na rechtop draaien kon Qt hem daardoor niet smaller maken en
// viel de OK-knop buiten beeld (simulator, 9 nov 2026). Minimum verlagen, lange
// teksten opnieuw afbreken op de nieuwe breedte en wx opnieuw laten indelen.
void ShrinkToArea(QWidget* top) {
  const QRect avail = UsableArea(top);
  if (!avail.isValid()) return;
  if (top->minimumWidth() <= avail.width() && top->minimumHeight() <= avail.height() &&
      top->width() <= avail.width())
    return;
  wxWindow* ww = wxWindow::QtRetrieveWindowPointer(top);
  const int maxText = avail.width() - 120;  // ruimte voor pictogram en marges
  if (ww && top->windowType() == Qt::Dialog && maxText > 100) {
    for (wxWindow* c : ww->GetChildren()) {
      auto* st = dynamic_cast<wxStaticText*>(c);
      if (!st || st->GetSize().x <= maxText) continue;
      wxString label = st->GetLabel();
      label.Replace("\n", " ");
      st->SetLabel(label);
      st->Wrap(maxText);
    }
  }
  const int minW = std::min(top->minimumWidth(), avail.width());
  const int minH = std::min(top->minimumHeight(), avail.height());
  if (ww) ww->SetMinSize(wxSize(minW, minH));
  top->setMinimumSize(minW, minH);
  if (top->width() > avail.width()) top->resize(avail.width(), top->height());
  if (ww) {
    if (ww->GetSizer()) ww->GetSizer()->Layout();
    ww->Layout();
  }
}

// Blad hoger dan het scherm (Autopilot Route: onderste keuzelijst viel weg; een
// wx-dialoog kan zelf niet scrollen). De wx-kinderen gaan op Qt-niveau in een
// scrollvlak onder de kopbalk. wx merkt daar niets van: het scrollvlak begint op (0,0)
// van het venster, dus posities binnen de inhoud blijven gelijk aan die in het venster.
// touch_ui.cpp vindt het scrollvlak zelf (dichtstbijzijnde QScrollArea): vinger 1:1.
const char* const kSheetScrollName = "ocpnios_sheet_scroll";

void UpdateSheetScroll(QWidget* dialog) {
  if (qobject_cast<QMainWindow*>(dialog)) return;
  if (dialog->property(kSheetProp).toInt() < 1) return;
  wxWindow* ww = wxWindow::QtRetrieveWindowPointer(dialog);
  if (!ww || !ww->GetSizer()) return;
  const int needH = ww->GetSizer()->GetMinSize().y;
  auto* sa = dialog->findChild<QScrollArea*>(kSheetScrollName, Qt::FindDirectChildrenOnly);
  if (!sa) {
    if (needH <= dialog->height() + 2) return;  // past: niets doen
    sa = new QScrollArea(dialog);
    sa->setObjectName(kSheetScrollName);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setWidgetResizable(false);
    sa->setStyleSheet("QScrollArea { background: transparent; }");
    sa->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget;
    content->setObjectName("ocpnios_sheet_content");
    sa->setWidget(content);
    QWidget* header = dialog->findChild<QWidget*>(kHeaderName, Qt::FindDirectChildrenOnly);
    // Kopie van de lijst: setParent haalt het kind eruit, en anders werd elk tweede
    // element overgeslagen (NMEA en Options van Autopilot Route bleven onzichtbaar).
    const QObjectList kids = dialog->children();
    for (QObject* o : kids) {
      auto* c = qobject_cast<QWidget*>(o);
      if (!c || c == sa || c == header || c->isWindow() || qobject_cast<QSizeGrip*>(c)) continue;
      const QRect g = c->geometry();
      const bool vis = c->isVisibleTo(dialog);
      c->setParent(content);
      c->setGeometry(g);
      if (vis) c->show();
    }
    sa->show();
  }
  QWidget* content = sa->widget();
  sa->setGeometry(0, 0, dialog->width(), dialog->height());
  const int contentH = std::max(dialog->height(), needH);
  content->resize(dialog->width(), contentH);
  // wx deelt in op de vensterhoogte en knijpt dan blokken samen (Autopilot Route:
  // "Active Route Windows" 0 pt hoog, keuzelijst buiten het blok Options). Op de volle
  // inhoudshoogte indelen; ook na wx' eigen indeling bij een maatwijziging (daarom na
  // deze event-ronde).
  ww->GetSizer()->SetDimension(0, 0, dialog->width(), contentH);
  QPointer<QWidget> guard(dialog);
  QTimer::singleShot(0, dialog, [guard, ww, contentH]() {
    if (!guard || !ww->GetSizer()) return;
    ww->GetSizer()->SetDimension(0, 0, guard->width(), contentH);
  });
  if (QWidget* header = dialog->findChild<QWidget*>(kHeaderName, Qt::FindDirectChildrenOnly))
    header->raise();
}

// Tabbladen die niet naast elkaar passen kregen kleine schuifpijltjes (Xander: "totaal
// niet zoals apps op een iPhone werken"). Dan in plaats van de tabbalk één keuzeknop
// over de volle breedte, zoals een iOS-uitklapknop. Past het weer (liggend), dan terug.
const char* const kTabPickerName = "ocpnios_tab_picker";

void UpdateTabPickers(QWidget* top) {
  for (QTabWidget* tw : top->findChildren<QTabWidget*>()) {
    QTabBar* bar = tw->tabBar();
    if (!bar || tw->count() < 2) continue;
    // De keuzeknop ligt als kind van de tabbalk over de hele balk heen. Een hoekwidget
    // van QTabWidget toont Qt niet als de tabbalk verborgen is (gemeten 10 nov 2026).
    auto* combo = bar->findChild<QComboBox*>(kTabPickerName, Qt::FindDirectChildrenOnly);
    int need = 0;
    for (int i = 0; i < tw->count(); i++) need += bar->tabRect(i).width();
    if (need <= tw->width() - 4) {
      if (combo) combo->hide();
      continue;
    }
    if (!combo) {
      combo = new QComboBox(bar);
      combo->setObjectName(kTabPickerName);
      for (int i = 0; i < tw->count(); i++) combo->addItem(tw->tabText(i).remove('&'));
      combo->setCurrentIndex(tw->currentIndex());
      QObject::connect(combo, &QComboBox::activated, tw,
                       [tw](int i) { tw->setCurrentIndex(i); });
      QObject::connect(tw, &QTabWidget::currentChanged, combo, [combo](int i) {
        if (combo->currentIndex() != i) combo->setCurrentIndex(i);
      });
    }
    combo->setGeometry(0, 0, std::max(100, tw->width()), bar->height());
    combo->raise();
    combo->show();
  }
}

// Inhoud van een blad passend maken voor de huidige breedte (en na draaien opnieuw).
const char* const kFittedWidthProp = "ocpnios_fitted_width";
void FitSheetContent(QWidget* top, bool force = false) {
  const int sheetKind = top->property(kSheetProp).toInt();
  if (sheetKind < 1 || sheetKind == 4) return;
  // Instellingen past zijn pagina's zelf aan (IosFitPage in options.cpp).
  if (top->property("ocpnios_noclose").toBool()) return;
  // Na draaien altijd opnieuw (force): wx deelt na de eerste ronde nog een keer in,
  // en een keuzelijst stond dan op de plek van de oude, smallere maat (10 nov 2026).
  if (!force && top->property(kFittedWidthProp).toInt() == top->width()) return;
  wxWindow* ww = wxWindow::QtRetrieveWindowPointer(top);
  if (!ww) return;
  top->setProperty(kFittedWidthProp, top->width());
  ocpnios_FitToWidth(ww);
  // Text buttons as iOS list rows (Data Monitor, priorities).
  ocpnios_ListButtons(ww);
  UpdateTabPickers(top);
  UpdateSheetScroll(top);
}

// Bladen en meldingen boven instrumentpanelen houden. Een paneel (radarvenster, Dashboard)
// dat na een blad opent, kwam er anders overheen te liggen (radar_pi: PPI over zijn
// eigen instellingenvenster, 10 nov 2026).
// Volgorde waarin vensters zijn geopend: het laatst geopende blad hoort bovenop. Twee
// schermvullende bladen tegelijk (radarmelding en stuurautomaat) werden in willekeurige
// volgorde naar voren gehaald; je zag het ene en tikte in het andere (simulator, 10 nov
// 2026). Raise in deze volgorde, en daarna opnieuw tekenen.
static qint64 g_showSeq = 0;
qint64 ShownSeq(QWidget* w) { return w->property("ocpnios_shownseq").toLongLong(); }

void RaiseSheetsAbovePanels() {
  std::vector<QWidget*> sheets;
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if (!top->isVisible() || !IsManagedWindow(top)) continue;
    const int k = top->property(kSheetProp).toInt();
    if ((k >= 1 && k <= 3) || (top->windowType() == Qt::Dialog && top->isModal()))
      sheets.push_back(top);
  }
  std::stable_sort(sheets.begin(), sheets.end(),
                   [](QWidget* a, QWidget* b) { return ShownSeq(a) < ShownSeq(b); });
  for (QWidget* top : sheets) {
    top->raise();
    top->update();
  }
}

void RaiseFloatingWindows(bool refit) {
  QWidget* last = nullptr;
  // Eerst panelen, dan bladen: zo liggen bladen bovenop.
  std::vector<QWidget*> order;
  for (QWidget* top : QApplication::topLevelWidgets())
    if (IsFloatingWindow(top) && top->property(kSheetProp).toInt() == 4) order.push_back(top);
  {
    std::vector<QWidget*> rest;
    for (QWidget* top : QApplication::topLevelWidgets())
      if (IsFloatingWindow(top) && top->property(kSheetProp).toInt() != 4) rest.push_back(top);
    std::stable_sort(rest.begin(), rest.end(),
                     [](QWidget* a, QWidget* b) { return ShownSeq(a) < ShownSeq(b); });
    order.insert(order.end(), rest.begin(), rest.end());
  }
  for (QWidget* top : order) {
    if (refit) {
      // Eerst terug naar de maat die het venster bij het openen vroeg: anders blijft
      // een venster dat liggend werd ingekort, na terugdraaien te klein (knoppen
      // buiten beeld; gezien bij het veilige-modus-venster, 8 nov 2026).
      // Een venster dat zijn plugin zelf indeelt (ocpnios_floating, mayara_pi) niet
      // terugzetten: de plugin deelt het na draaien opnieuw in.
      if (!top->property("ocpnios_floating").toBool()) {
        const QSize wanted = top->property(kWantedSizeProp).toSize();
        if (wanted.isValid()) top->resize(wanted);
        ShrinkToArea(top);
      }
      FitToScreen(top);
      FitSheetContent(top, true);
    }
    top->raise();
    last = top;
  }
  // Let op (simulator, 8 nov 2026): na draaien blijft een open wxFrame (bv. Data
  // Monitor) toch achter de kaart, ook met activateWindow(). Nog niet opgelost. Het
  // QWindow verbergen en weer tonen hielp ook niet en sloot modale opstartvensters.
  if (refit && last) last->activateWindow();
}

// Tekst van een wx-knop zonder sneltoetsteken, voor vergelijken.
wxString PlainLabel(wxWindow* w) {
  wxString l = w->GetLabel();
  l.Replace("&", "");
  l.Replace("...", "");
  return l.Strip(wxString::both);
}

void CollectButtons(wxWindow* w, std::vector<wxButton*>& out) {
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    if (auto* b = dynamic_cast<wxButton*>(c)) out.push_back(b);
    CollectButtons(c, out);
  }
}

bool LabelIs(wxButton* b, std::initializer_list<const char*> names) {
  const wxString l = PlainLabel(b);
  for (const char* n : names)
    if (l.IsSameAs(n, false) || l.IsSameAs(wxGetTranslation(n), false)) return true;
  return false;
}

// Heeft het venster invoervelden? Dan is het een instellingenvenster (blad), geen
// melding (Polar-voorkeuren: klein, maar met een schakelaar).
bool HasInputs(wxWindow* w) {
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    if (dynamic_cast<wxCheckBox*>(c) || dynamic_cast<wxRadioButton*>(c) ||
        dynamic_cast<wxTextCtrl*>(c) || dynamic_cast<wxChoice*>(c) ||
        dynamic_cast<wxComboBox*>(c) || dynamic_cast<wxSpinCtrl*>(c) ||
        dynamic_cast<wxSpinCtrlDouble*>(c) || dynamic_cast<wxListBox*>(c) ||
        dynamic_cast<wxSlider*>(c) || dynamic_cast<wxListCtrl*>(c) ||
        dynamic_cast<wxTreeCtrl*>(c))  // Prioriteiten (Verbindingen), 10 nov 2026
      return true;
    if (HasInputs(c)) return true;
  }
  return false;
}

// Tekstveld met een getal of een positie: toetsenbord met cijfers en leestekens, zonder
// woordsuggesties en hoofdletter. Herkend aan de inhoud, want OpenCPN gebruikt voor
// getallen en posities gewone tekstvelden.
void SetNumericKeyboard(QLineEdit* le) {
  static const QRegularExpression kNumber(
      QString::fromUtf8("^[-+]?[0-9][0-9 .,°'\"′″]*[NSEWOnsewo]?$"));
  const bool numeric = kNumber.match(le->text().trimmed()).hasMatch();
  const Qt::InputMethodHints mine =
      Qt::ImhPreferNumbers | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase;
  Qt::InputMethodHints h = le->inputMethodHints();
  if (numeric) {
    h |= mine;
    le->setProperty("ocpnios_numkbd", true);
  } else if (le->property("ocpnios_numkbd").toBool()) {
    h &= ~mine;  // veld is van inhoud veranderd: weer het gewone toetsenbord
    le->setProperty("ocpnios_numkbd", false);
  }
  if (h != le->inputMethodHints()) le->setInputMethodHints(h);
}

// Veld in een schuifvlak tot boven de bovenste 45% van het scherm schuiven (het
// toetsenbord met zijn balk beslaat op een iPhone staand ~40% onderaan). Via
// triggerAction: wx schuift een wxScrolledWindow alleen mee op actionTriggered.
void ScrollFieldAboveKeyboard(QWidget* field) {
  QScreen* sc = field->screen();
  if (!sc) return;
  const QRect sg = sc->geometry();
  const int limit = sg.height() * 45 / 100;
  for (QWidget* p = field->parentWidget(); p; p = p->parentWidget()) {
    auto* sa = qobject_cast<QAbstractScrollArea*>(p);
    if (!sa) continue;
    QScrollBar* vb = sa->verticalScrollBar();
    if (!vb || vb->maximum() <= 0) continue;
    const int bottom = field->mapToGlobal(QPoint(0, field->height())).y() - sg.top();
    const int delta = bottom - limit;
    if (delta <= 0) return;
    const int target = std::min(vb->maximum(), vb->value() + delta);
    if (target == vb->value()) return;
    vb->setSliderPosition(target);
    vb->triggerAction(QAbstractSlider::SliderMove);
    return;
  }
}

// Een wx-knop indrukken zoals een tik: Qt's click() geeft via wxQt een wxEVT_BUTTON.
void PressWxButton(wxButton* b) {
  if (auto* qb = qobject_cast<QAbstractButton*>(b->GetHandle())) qb->click();
}

QPushButton* MakeHeaderButton(QWidget* parent, const QString& text, bool bold) {
  auto* b = new QPushButton(text, parent);
  b->setFlat(true);
  b->setMinimumSize(64, 44);  // iOS-tikvlak
  b->setCursor(Qt::PointingHandCursor);
  const bool dark = qApp->property("ocpnios_dark").toBool();
  b->setStyleSheet(QString("QPushButton { border: none; background: transparent;"
                           " padding: 12px 16px; font-size: 18px; color: %1; %2 }")
                       .arg(dark ? "#d06060" : "#007aff")
                       .arg(bold ? "font-weight: 600;" : ""));
  // Breedte naar de tekst in de stijl van hierboven (18 pt): de maat werd anders met de
  // gewone letter berekend en "Annuleren" viel op de iPhone rechts weg (10 nov 2026).
  {
    QFont f = b->font();
    f.setPixelSize(18);
    if (bold) f.setWeight(QFont::DemiBold);
    const int w = QFontMetrics(f).horizontalAdvance(text) + 2 * 16 + 6;
    b->setMinimumWidth(std::max(64, w));
  }
  return b;
}

// Venster met een HTML-tekstvak (wxHtmlWindow) direct erin. Op klassenaam, zodat dit
// bestand de wx-html-bibliotheek niet nodig heeft.
bool HasHtmlText(wxWindow* ww) {
  if (!ww) return false;
  for (wxWindow* c : ww->GetChildren())
    if (c->GetClassInfo() && wxString(c->GetClassInfo()->GetClassName()) == "wxHtmlWindow")
      return true;
  return false;
}

void EnsureSheetHeader(QWidget* dialog) {
  // Dialogen en losse vensters (wxFrame, bv. Data Monitor: Qt::Window of Qt::Tool).
  // Gemeten 8 nov 2026: de Data Monitor had geen knop om te sluiten en bleef over de
  // kaart staan; het menu-item opende hem alleen opnieuw.
  const Qt::WindowType type = dialog->windowType();
  if (type != Qt::Dialog && type != Qt::Window && type != Qt::Tool) return;
  // Losse vensters alleen met een titel: interne hulpvensters van OpenCPN zonder titel
  // kregen anders ook een knop (gezien boven de menubalk, 8 nov 2026).
  if (type != Qt::Dialog && (IsMainWindow(dialog) || dialog->windowTitle().isEmpty()))
    return;
  // Vensters zonder rand (bv. de werkbalktips van OpenCPN, wxNO_BORDER): niets.
  if (dialog->windowFlags() & Qt::FramelessWindowHint) return;
  // Vensters met een eigen kopregel (Instellingen).
  if (dialog->property("ocpnios_noclose").toBool()) return;
  // Klein venster: niets, behalve een venster met een HTML-tekstvak. Dat krijgt zijn maat
  // van de inhoud en is leeg bijna 0x0 (o-charts "Toon kaartinformatie" zonder kaarten:
  // ~60x50 pt met afgebroken tekst, 11 nov 2026); als blad wordt het schermvullend.
  if (dialog->width() < 120 || dialog->height() < 80) {
    if (!HasHtmlText(wxWindow::QtRetrieveWindowPointer(dialog))) return;
  }
  // Ook dieper zoeken: bij een venster met eigen menubalk zit de balk in een houder.
  if (dialog->findChild<QWidget*>(kHeaderName)) return;
  if (dialog->property(kSheetProp).isValid()) return;  // al bekeken, bewust zonder balk
  // Een plugin die zijn venster zelf indeelt (mayara_pi: radar naast de werkbalk, staand
  // boven, liggend rechts) markeert het met "ocpnios_floating": geen blad, eigen plek.
  if (dialog->property("ocpnios_floating").toBool()) {
    dialog->setProperty(kSheetProp, 4);
    return;
  }

  wxWindow* ww = wxWindow::QtRetrieveWindowPointer(dialog);
  if (!ww) {
    dialog->setProperty(kSheetProp, 0);  // Qt-eigen venster (bv. QMessageBox)
    if (auto* mb = qobject_cast<QMessageBox*>(dialog)) FitMessageBox(mb);
    return;
  }
  // Voortgangsvenster (kaarten bijwerken bij het opstarten): geen sluitknop, zoals een
  // iOS-voortgangsmelding. Een eigen Annuleren-knop (wxPD_CAN_ABORT) blijft staan.
  if (auto* pd = dynamic_cast<wxGenericProgressDialog*>(ww)) {
    dialog->setProperty(kSheetProp, 0);
    dialog->setProperty("ocpnios_progress", true);
    // Eigen achtergrond zoals een melding: zonder stond de tekst los over de pagina
    // eronder ("Bronnen zoeken", iPhone 11 nov 2026). Sinds 26 nov in iOS-vorm.
    ProgressLook(dialog, pd);
    CenterProgress(dialog);
    return;
  }
  // Melding in iOS-stijl (OCPNMessageDialog, gui_lib.cpp): geen kopbalk, wel een
  // kader; zonder kader viel de melding weg tegen het venster eronder.
  if (dialog->property("ocpnios_alert").toBool()) {
    dialog->setProperty(kSheetProp, 0);
    ocpnios_AlertStyle(dialog);
    return;
  }

  std::vector<wxButton*> buttons;
  CollectButtons(ww, buttons);
  // Help in een blad: een tekstknop, geen grote blauwe balk over de volle breedte
  // (Weerrouterings-instellingen, 11 nov 2026). Help is geen hoofdactie.
  for (wxButton* b : buttons) {
    if (b->GetId() != wxID_HELP) continue;
    if (QWidget* q = b->GetHandle()) {
      q->setProperty("ocpnios_nav", true);
      if (wxSizer* bs = b->GetContainingSizer())
        if (wxSizerItem* it = bs->GetItem(b)) it->SetFlag(it->GetFlag() & ~wxEXPAND);
    }
  }
  wxButton* done = nullptr;
  wxButton* cancel = nullptr;
  wxButton* apply = nullptr;  // Toepassen: bestaat op iOS niet
  bool decision = false;  // Ja/Nee: een vraag, geen blad
  for (wxButton* b : buttons) {
    const int id = b->GetId();
    // Alleen knoppen van het venster zelf: een Ja-knop op een tabbladpagina (GRIB
    // "Verwachting ophalen", tabblad e-mail) maakte het hele venster een vraag, zonder
    // kopbalk en met Annuleren/OK onderin (iPhone, 13 nov 2026).
    bool onPage = false;
    for (wxWindow* p = b->GetParent(); p && p != ww; p = p->GetParent())
      if (dynamic_cast<wxBookCtrlBase*>(p)) onPage = true;
    if (!onPage && (id == wxID_YES || id == wxID_NO || LabelIs(b, {"Yes", "No"})))
      decision = true;
    if (!done && (id == wxID_OK || id == wxID_CLOSE ||
                  LabelIs(b, {"OK", "Close", "Done", "Ok"})))
      done = b;
    else if (!cancel && (id == wxID_CANCEL || LabelIs(b, {"Cancel"})))
      cancel = b;
    else if (!apply && !onPage && (id == wxID_APPLY || LabelIs(b, {"Apply"})))
      apply = b;
  }
  // Een venster met knoppen per regel (meldingenlijst: "OK" per melding) is een blad met
  // Gereed; die knoppen zijn niet van het venster zelf (map_controls_ios.cpp).
  const bool listSheet = dialog->property("ocpnios_listsheet").toBool();
  if (listSheet) done = cancel = apply = nullptr, decision = false;
  // Een korte melding met eigen knoppen (OCPNMessageBox, vragen) blijft zoals een
  // iOS-melding: klein, knoppen onderin.
  const QRect avail = UsableArea(dialog);
  // Een HTML-tekst (o-charts kaartinformatie) is geen korte melding: als melding bleef
  // het tekstvak 0 pt hoog en was alleen OK te zien (11 nov 2026).
  const bool small = avail.isValid() && dialog->height() < avail.height() * 45 / 100 &&
                     !HasInputs(ww) && !HasHtmlText(ww);
  if (!listSheet && (decision || (small && (done || cancel) && type == Qt::Dialog))) {
    dialog->setProperty(kSheetProp, 0);
    // Kader zoals bij een melding: zonder lag het venster doorzichtig over wat eronder
    // stond ("Over" van Watchdog op de pluginlijst, iPad 10 nov 2026).
    if (dialog->styleSheet().isEmpty())
      dialog->setStyleSheet("QDialog { border: 1px solid #a0a0a8; }");
    // Zelfde iOS-opmaak als de bladen (knoppen, tekstvak), zonder de indeling te
    // veranderen (veilige-modusvraag had desktopknoppen, iPhone 10 nov 2026).
    ocpnios_AlertLook(dialog);
    return;
  }

  // Ruimte maken: bij een wxDialog een lege regel bovenaan de sizer, bij een wxFrame
  // (QMainWindow) de balk als menuWidget, zodat wx het clientgebied kleiner ziet.
  auto* mw = qobject_cast<QMainWindow*>(dialog);
  wxSizer* sizer = ww->GetSizer();
  if (!mw && !sizer) {
    dialog->setProperty(kSheetProp, 0);
    return;
  }

  // Venster dat alleen iets toont, zonder knoppen of invoer (Dashboard, Tactics: zwevende
  // instrumentpanelen): geen kopbalk en eigen maat en plaats. Schermvullend dekte het de
  // kaart af (10 nov 2026, met echte instrumentdata); aan en uit gaat met de werkbalkknop.
  // Een plugin met eigen getekende knoppen (Navico-stuurautomaat: ApButton, geen
  // wxButton) markeert zijn paneel met "ocpnios_panel"; dat werd anders een zwevend
  // instrumentpaneel half over de werkbalk (iPhone, 13 nov 2026).
  bool marked = dialog->property("ocpnios_panel").toBool();
  if (!marked)
    for (QWidget* c : dialog->findChildren<QWidget*>())
      if (c->property("ocpnios_panel").toBool()) {
        marked = true;
        dialog->setProperty("ocpnios_panel", true);
        break;
      }
  // Zwevend AUI-paneel met eigen knoppen (Navico-stuurautomaat): weer zwevend en
  // versleepbaar, zonder kopbalk (Xander 26 nov 2026: "De Navico plugin is geen zwevend
  // veld meer"). Eerst naast de werkbalk gezet (FitToScreen), niet eroverheen zoals op
  // 13 nov.
  if (marked && dynamic_cast<wxAuiFloatingFrame*>(ww)) {
    dialog->setProperty(kSheetProp, 4);
    dialog->setProperty("ocpnios_place_clear", true);
    return;
  }
  const bool displayOnly = !marked && type != Qt::Dialog && buttons.empty() && !HasInputs(ww);
  if (displayOnly) {
    dialog->setProperty(kSheetProp, 4);
    return;
  }

  auto* header = new QWidget(dialog);
  header->setObjectName(kHeaderName);
  header->setAutoFillBackground(true);
  header->setFixedHeight(kHeaderHeight);
  auto* row = new QHBoxLayout(header);
  row->setContentsMargins(6, 10, 6, 4);  // iets onder de statusbalk
  QPointer<QWidget> guard(dialog);
  if (cancel) {
    // Vertaling uit het domein van OpenCPN zelf: zonder domein won de vertaling van een
    // later geladen plugin (Weather Routing: "Done" = "Klaar", iPad 10 nov 2026).
    auto* b = MakeHeaderButton(header, QString::fromUtf8(wxGetTranslation("Cancel", "opencpn").utf8_string().c_str()), false);
    wxWindowID id = cancel->GetId();
    QObject::connect(b, &QPushButton::clicked, dialog, [guard, ww, id]() {
      if (!guard) return;
      if (auto* c = dynamic_cast<wxButton*>(ww->FindWindow(id))) PressWxButton(c);
    });
    row->addWidget(b, 0);
  } else {
    row->addSpacing(80);  // titel in het midden houden
  }
  auto* title = new QLabel(dialog->windowTitle(), header);
  title->setObjectName("ocpnios_sheet_title");
  dialog->installEventFilter(new TitleFollower(dialog, header));
  title->setProperty("ocpnios_full", dialog->windowTitle());
  title->setAlignment(Qt::AlignCenter);
  title->setStyleSheet("QLabel { font-size: 17px; font-weight: 600; background: transparent; }");
  title->setMinimumWidth(10);
  title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  row->addWidget(title, 1);
  {
    // Een bevestigknop met een eigen opdracht ("Akkoord" in het welkomstvenster) houdt
    // zijn tekst; met "Gereed" was niet te zien dat je ermee akkoord ging (Xander, 11 nov
    // 2026: "op Agree klikken, alleen bestaat die knop niet"). OK/Sluiten wordt "Gereed".
    QString doneText =
        QString::fromUtf8(wxGetTranslation("Done", "opencpn").utf8_string().c_str());
    if (done && !LabelIs(done, {"OK", "Ok", "Close", "Done", "Apply"}) &&
        !PlainLabel(done).empty())
      doneText = QString::fromUtf8(PlainLabel(done).utf8_string().c_str());
    auto* b = MakeHeaderButton(header, doneText, true);
    wxWindowID id = done ? done->GetId() : wxID_NONE;
    // Toepassen staat niet in de balk (iOS kent het niet, Xander 26 nov 2026). Is
    // Gereed een OK, dan past dat zelf toe; is het Sluiten (of er is geen knop), dan
    // eerst Toepassen indrukken, zodat er niets verloren gaat.
    const wxWindowID applyId =
        apply && !(done && done->GetId() == wxID_OK) ? apply->GetId() : wxID_NONE;
    QObject::connect(b, &QPushButton::clicked, dialog, [guard, ww, id, applyId]() {
      if (!guard) return;
      if (applyId != wxID_NONE)
        if (auto* a = dynamic_cast<wxButton*>(ww->FindWindow(applyId))) PressWxButton(a);
      if (!guard) return;
      wxButton* d = id != wxID_NONE ? dynamic_cast<wxButton*>(ww->FindWindow(id)) : nullptr;
      if (d)
        PressWxButton(d);
      else
        guard->close();
    });
    row->addWidget(b, 0);
  }
  // Dunne lijn onder de balk.
  header->setAttribute(Qt::WA_StyledBackground, true);
  // Eigen achtergrond: bij scrollen schoof de inhoud er anders zichtbaar onder door.
  header->setStyleSheet(QString("QWidget#%1 { border-bottom: 1px solid #c8c8cc;"
                                " background-color: %2; }")
                            .arg(kHeaderName)
                            .arg(dialog->palette().color(QPalette::Window).name()));

  // Originele OK/Annuleren verbergen; de balk neemt ze over. Toepassen ook: Gereed doet
  // dat nu.
  if (done) done->Hide();
  if (cancel) cancel->Hide();
  if (apply) apply->Hide();

  if (mw) {
    // Een eigen menubalk van het venster (wxFrame::SetMenuBar, bv. WeatherRouting) niet
    // vervangen: QMainWindow::setMenuWidget gooit de oude met deleteLater weg, terwijl wx
    // (wxMenuBar) en de plugin (menu-items) hem nog gebruiken. Gevolg was een crash in
    // QAction::setEnabled bij het tekenen van de kaart (WeatherRouting, iPhone 10 nov 2026).
    // Daarom: kopregel en bestaande menubalk samen in één houder. wx blijft eigenaar van
    // de menubalk (wxFrame ruimt hem op vóór de Qt-vensters).
    QWidget* oldMenu = mw->menuWidget();
    if (oldMenu && oldMenu != header) {
      if (QLayout* ml = mw->layout()) ml->setMenuBar(nullptr);  // zonder deleteLater los
      auto* holder = new QWidget;
      auto* v = new QVBoxLayout(holder);
      v->setContentsMargins(0, 0, 0, 0);
      v->setSpacing(0);
      v->addWidget(header);
      v->addWidget(oldMenu);
      mw->setMenuWidget(holder);
      // Desktop-menubalk ("Bestand Positie Routings ...", WeatherRouting) als iOS-knop
      // "⋯" links in de kopbalk met dezelfde menu's als uitklapmenu (Xander, 11 nov 2026:
      // "nogal desktop-achtig"). De menubalk zelf blijft bestaan (verborgen): wx en de
      // plugin houden hun acties en menu's.
      if (auto* mbar = qobject_cast<QMenuBar*>(oldMenu); mbar && !mbar->actions().isEmpty()) {
        if (auto* row = qobject_cast<QHBoxLayout*>(header->layout())) {
          if (row->count() > 0 && row->itemAt(0)->spacerItem()) delete row->takeAt(0);
          auto* more = MakeHeaderButton(header, QString::fromUtf8("\u22EF"), true);
          more->setObjectName("ocpnios_menu_button");
          more->setStyleSheet(more->styleSheet() + "QPushButton { font-size: 26px; }");
          QPointer<QMenuBar> mb(mbar);
          QObject::connect(more, &QPushButton::clicked, more, [mb, more]() {
            if (!mb) return;
            // Als iOS-actieblad: eerst de menu's, dan de items (action_sheet.mm). Een
            // Qt-uitklapmenu reageerde niet op een tik op een item (simulator).
            ocpnios_ShowMenuSheet(mb->actions(), more,
                                  QString::fromUtf8(wxGetTranslation("Cancel", "opencpn")
                                                        .utf8_string().c_str()));
          });
          row->insertWidget(0, more, 0);
        }
        oldMenu->hide();
      }
    } else {
      mw->setMenuWidget(header);
    }
    QPointer<QWidget> hg(header);
    QTimer::singleShot(0, header, [hg]() { if (hg) ElideTitle(hg); });
  } else {
    // Ruimte voor de balk. In een gewone kolom een lege regel bovenaan; anders
    // (raster: GRIB, Autopilot Route) de oude sizer in een nieuwe kolom zetten. In een
    // raster werd de lege regel een cel: 113 pt hoog of een verschoven kolom.
    auto* box = dynamic_cast<wxBoxSizer*>(sizer);
    if (box && box->GetOrientation() == wxVERTICAL &&
        !dynamic_cast<wxStaticBoxSizer*>(sizer) && !dynamic_cast<wxWrapSizer*>(sizer)) {
      sizer->Prepend(0, kHeaderHeight);
    } else {
      auto* outer = new wxBoxSizer(wxVERTICAL);
      ww->SetSizer(outer, false);
      outer->AddSpacer(kHeaderHeight);
      outer->Add(sizer, 1, wxEXPAND);
      sizer = outer;
    }
    ww->Layout();
    dialog->resize(dialog->width(), dialog->height() + kHeaderHeight);
    PlaceHeader(dialog, header);
  }
  header->show();
  // Schermvullend, zoals een instellingenpagina op iOS (ook een klein venster als dat
  // van Polar; als los vlak bovenin oogde dat niet als iOS).
  // Uitzondering: een klein, niet-modaal bedieningspaneel zonder OK/Annuleren (GRIB:
  // tijd vooruit/achteruit met de kaart in beeld) wordt een paneel onderin; schermvullend
  // dekte het de kaart af (10 nov 2026).
  // Alleen een echte wxDialog: "Over OpenCPN" (wxFrame, ook Qt::Dialog) werd anders een
  // strook onderin (10 nov 2026).
  // Zwevend AUI-paneel van een plugin (Navico-stuurautomaat): ook een paneel onderin. Als
  // schermvullend blad stond de inhoud bovenin en was de rest zwart (iPhone, 10 nov 2026).
  const bool auiFloat = dynamic_cast<wxAuiFloatingFrame*>(ww) != nullptr;
  // Een plugin kan een venster zelf als paneel onderin aanmerken (GRIB-balk: met veel
  // parameters hoger dan 40% van de iPhone, werd schermvullend en dekte de overlay af,
  // 13 nov 2026). De hoogte begrenst FitToScreen; wat niet past schuift.
  const bool panel = auiFloat || dialog->property("ocpnios_panel").toBool() ||
                     (type == Qt::Dialog && dynamic_cast<wxDialog*>(ww) &&
                      !dialog->isModal() && !done && !cancel &&
                      avail.isValid() &&
                      dialog->height() < std::max(avail.width(), avail.height()) * 40 / 100);
  dialog->setProperty(kSheetProp, listSheet ? 2 : panel ? 3 : 2);
  // Meldingsregel met vaste maat (wxST_NO_AUTORESIZE) in een paneel onderin: de plugin
  // breekt af op zijn eigen breedte, die op de iPhone breder is dan het scherm; de melding
  // van de Navico-stuurautomaat viel rechts weg (13 nov 2026). Qt breekt af op de echte
  // breedte.
  if (panel)
    for (auto* l : dialog->findChildren<QLabel*>())
      if (auto* st = dynamic_cast<wxStaticText*>(wxWindow::QtRetrieveWindowPointer(l)))
        if (st->HasFlag(wxST_NO_AUTORESIZE)) l->setWordWrap(true);
  const QSize wanted = dialog->property(kWantedSizeProp).toSize();
  if (wanted.isValid())
    dialog->setProperty(kWantedSizeProp, QSize(wanted.width(), wanted.height() + kHeaderHeight));
}

// Instrumentpaneel (Dashboard, Tactics: zwevend venster zonder knoppen, blad 4) met de
// vinger verslepen over de kaart. Er is op iOS geen titelbalk om aan te trekken
// (Xander, 11 nov 2026: "een zwevend venster dat je kunt verplaatsen in de kaart zelf").
// Een tik zonder schuiven gaat gewoon naar het paneel (lang indrukken: menu van het
// paneel). Het paneel blijft binnen het bruikbare deel van het scherm; wx ziet de nieuwe
// plaats (move-event) en bewaart hem met de rest van de indeling.
class PanelDrag {
 public:
  bool Handle(QObject* obj, QEvent* ev) {
    const auto t = ev->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseMove &&
        t != QEvent::MouseButtonRelease)
      return false;
    if (!obj->isWidgetType()) return false;
    auto* me = static_cast<QMouseEvent*>(ev);
    if (t == QEvent::MouseButtonPress) {
      QWidget* top = static_cast<QWidget*>(obj)->window();
      if (!top || top->property(kSheetProp).toInt() != 4 || me->button() != Qt::LeftButton)
        return false;
      m_win = top;
      m_start = me->globalPosition().toPoint();
      m_origin = top->pos();
      m_dragging = false;
      return false;  // de druk gaat door naar het paneel
    }
    if (!m_win) return false;
    if (t == QEvent::MouseMove) {
      const QPoint d = me->globalPosition().toPoint() - m_start;
      if (!m_dragging && (std::abs(d.x()) > 10 || std::abs(d.y()) > 10)) {
        m_dragging = true;
        // Druk bij het paneel annuleren: loslaten ver buiten, geen klik.
        QPointF far(-100000, -100000);
        QMouseEvent rel(QEvent::MouseButtonRelease, far, far, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
        m_synthetic = true;
        QCoreApplication::sendEvent(obj, &rel);
        m_synthetic = false;
      }
      if (!m_dragging || !m_win) return m_dragging;
      const QRect avail = UsableArea(m_win);
      QPoint p = m_origin + d;
      if (avail.isValid()) {
        p.setX(std::clamp(p.x(), avail.left(), std::max(avail.left(), avail.right() - m_win->width() + 1)));
        p.setY(std::clamp(p.y(), avail.top(), std::max(avail.top(), avail.bottom() - m_win->height() + 1)));
      }
      m_win->move(p);
      return true;
    }
    // Loslaten.
    if (m_synthetic) return false;
    const bool was = m_dragging;
    if (was && m_win) {
      if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(m_win)) {
        const QPoint p = m_win->pos();
        ww->Move(p.x(), p.y());  // wx (AUI) de nieuwe plaats laten weten
      }
    }
    m_win = nullptr;
    m_dragging = false;
    return was;
  }

 private:
  QPointer<QWidget> m_win;
  QPoint m_start, m_origin;
  bool m_dragging = false;
  bool m_synthetic = false;
};

class WindowFitFilter : public QObject {
  QElapsedTimer m_swallowUntil;  // zie eventFilter: doortikken na sluiten
  QPointF m_lastPress;           // plaats (scherm) van de laatste druk of aanraking
  QPointF m_swallowAt;           // plaats van de tik die het blad sloot
  QTimer* m_refit = nullptr;     // opnieuw passend maken na verschalen app-venster
  PanelDrag m_drag;

 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (m_drag.Handle(obj, ev)) return true;
    // Instrumentpaneel (Dashboard, Tactics) als iOS-kaart: afgeronde hoeken
    // (Xander, 11 nov 2026: "lijkt nog niet echt op iOS"). Bij elke maatwijziging.
    if ((ev->type() == QEvent::Resize || ev->type() == QEvent::Show) && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      if (w->isWindow() && w->property(kSheetProp).toInt() == 4) RoundPanel(w);
    }
    // Een tik die een blad sluit (Gereed/Annuleren, ook knoppen in het venster zelf)
    // viel na het sluiten door naar wat eronder lag: op de iPad zit het kompas van de
    // kaart precies onder "Gereed", en de kaart sprong op koers-boven (Xander, gemeten in
    // de simulator 10 nov 2026). Na het verdwijnen van een blad of dialoog daarom
    // 500 ms geen tik/klik naar het hoofdvenster op de plaats van die tik. Een nieuwe tik
    // ergens anders (de werkbalk direct na Gereed) gaat wel door; die viel eerder weg
    // (HIG-review 26 nov 2026, punt 1).
    if (ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::TouchBegin) {
      if (ev->type() == QEvent::MouseButtonPress)
        m_lastPress = static_cast<QMouseEvent*>(ev)->globalPosition();
      else if (!static_cast<QTouchEvent*>(ev)->points().isEmpty())
        m_lastPress = static_cast<QTouchEvent*>(ev)->points().first().globalPosition();
    }
    if (ev->type() == QEvent::Hide && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      if (w->isWindow() && !IsMainWindow(w) && IsManagedWindow(w) &&
          w->property(kSheetProp).toInt() != 4) {
        m_swallowUntil.start();
        m_swallowAt = m_lastPress;
      }
      // Een paneel onderin dicht: de andere panelen zakken weer naar beneden.
      if (w->isWindow() && w->property(kSheetProp).toInt() == 3) {
        QTimer::singleShot(0, qApp, []() {
          std::vector<QWidget*> panels;
          for (QWidget* o : QApplication::topLevelWidgets())
            if (o->isVisible() && o->property(kSheetProp).toInt() == 3) panels.push_back(o);
          std::sort(panels.begin(), panels.end(), [](QWidget* a, QWidget* b) {
            return a->property("ocpnios_shownseq").toLongLong() <
                   b->property("ocpnios_shownseq").toLongLong();
          });
          for (QWidget* o : panels) FitToScreen(o);
        });
      }
    }
    switch (ev->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::TouchBegin:
      case QEvent::TouchEnd:
        if (m_swallowUntil.isValid() && m_swallowUntil.elapsed() < 500 &&
            obj->isWidgetType() && IsMainWindow(static_cast<QWidget*>(obj)->window())) {
          QPointF at = m_swallowAt;
          if (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchEnd) {
            const auto& pts = static_cast<QTouchEvent*>(ev)->points();
            if (!pts.isEmpty()) at = pts.first().globalPosition();
          } else {
            at = static_cast<QMouseEvent*>(ev)->globalPosition();
          }
          // Binnen een vingerbreedte (44 pt) van de sluitende tik: inslikken.
          if (QLineF(at, m_swallowAt).length() < 44) return true;
        }
        break;
      default:
        break;
    }
    // Wordt een ander topvenster actief (bv. het hoofdvenster na een tik op de werkbalk,
    // dat OpenCPN daarna de focus geeft), haal open dialogen dan weer naar voren. Alleen
    // raise(), geen activateWindow(), om geen lus van activaties te krijgen.
    // Ook losse vensters met titel (wxFrame): een AIS-venster dat via een dubbele tik
    // opende, verdween anders achter de kaart zodra de vinger losliet (Xander, 8 nov 2026).
    if (ev->type() == QEvent::WindowActivate && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      if (w->isWindow() && !IsFloatingWindow(w)) {
        QTimer::singleShot(0, this, []() { RaiseFloatingWindows(false); });
      }
    }
    if (ev->type() == QEvent::Resize && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      // Venstermodus: het app-venster is verschaald; open vensters opnieuw passend maken
      // (zoals na draaien). Pas als het slepen even stilstaat.
      if (w->isWindow() && IsMainWindow(w)) {
        if (!m_refit) {
          m_refit = new QTimer(this);
          m_refit->setSingleShot(true);
          m_refit->setInterval(250);
          QObject::connect(m_refit, &QTimer::timeout, this,
                           []() { RaiseFloatingWindows(true); });
        }
        m_refit->start();
      }
      // Een blad dat zichzelf kleiner maakt (Fit() bij een nieuwe melding, radar_pi
      // MessageBox: smal blok linksboven over de werkbalk, iPad 10 nov 2026) weer op
      // schermbreedte. FitToScreen zet alleen een andere maat als het nodig is.
      if (w->isWindow() && w->isVisible()) {
        const int k = w->property(kSheetProp).toInt();
        // Ook een paneel onderin (3): de GRIB-balk maakt zich na het openen van een
        // bestand smaller (Fit/SetSize) en de zoomknoppen staken er half onder uit
        // (iPhone, 11 nov 2026). Alleen breedte en plaats; de hoogte bepaalt FitToScreen
        // zoals bij het openen.
        if (k == 1 || k == 2 || k == 3) {
          QPointer<QWidget> guard(w);
          QTimer::singleShot(0, w, [guard]() { if (guard && guard->isVisible()) FitToScreen(guard); });
        }
      }
      if (w->isWindow() && w->property("ocpnios_progress").toBool()) {
        QPointer<QWidget> guard(w);
        QTimer::singleShot(0, w, [guard]() { if (guard) CenterProgress(guard); });
      }
      if (w->isWindow()) {
        if (auto* h = w->findChild<QWidget*>(kHeaderName)) {
          if (qobject_cast<QMainWindow*>(w)) {
            QPointer<QWidget> hg(h);
            QTimer::singleShot(0, h, [hg]() { if (hg) ElideTitle(hg); });
          } else if (h->parentWidget() == w) {
            PlaceHeader(w, h);
          }
        }
        if (w->findChild<QScrollArea*>(kSheetScrollName, Qt::FindDirectChildrenOnly))
          UpdateSheetScroll(w);
      }
    }
    // Keuzelijst in een blad die rechts buiten haar ouder valt (wx zette haar na draaien
    // op de plek van de oude, smallere maat; fit_width.cpp legt de breedte vast): naar
    // links schuiven tot ze past.
    if ((ev->type() == QEvent::Move || ev->type() == QEvent::Resize) && obj->isWidgetType()) {
      if (auto* cb = qobject_cast<QComboBox*>(static_cast<QWidget*>(obj))) {
        QWidget* par = cb->parentWidget();
        QWidget* top = cb->window();
        if (par && top && top->property(kSheetProp).toInt() >= 1 &&
            cb->geometry().right() > par->width() - 4 && cb->x() > 0) {
          const int nx = std::max(0, par->width() - 4 - cb->width());
          if (nx != cb->x()) cb->move(nx, cb->y());
        }
      }
    }
    // Een wx-kind dat later in een blad met scrollvlak wordt gemaakt, ook in de inhoud.
    if (ev->type() == QEvent::ChildAdded && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      auto* child = qobject_cast<QWidget*>(static_cast<QChildEvent*>(ev)->child());
      if (w->isWindow() && child && !child->isWindow() &&
          child->objectName() != kSheetScrollName && child->objectName() != kHeaderName) {
        if (auto* sa = w->findChild<QScrollArea*>(kSheetScrollName,
                                                  Qt::FindDirectChildrenOnly)) {
          QPointer<QWidget> c(child);
          QPointer<QScrollArea> area(sa);
          QTimer::singleShot(0, w, [c, area]() {
            if (!c || !area || c->parentWidget() == area->widget()) return;
            if (qobject_cast<QSizeGrip*>(c)) return;
            const QRect g = c->geometry();
            const bool vis = !c->isHidden();
            c->setParent(area->widget());
            c->setGeometry(g);
            if (vis) c->show();
          });
        }
      }
    }
    // Lijsten en bomen (QAbstractItemView) vragen in Qt standaard tekstinvoer aan (zoeken
    // door te typen); op iOS sprong daardoor het toetsenbord open bij een tik in een lijst
    // (Prioriteiten navigatiegegevens, iPad, 10 nov 2026). Bewerken van een regel gebruikt
    // een eigen invoerveld en houdt het toetsenbord.
    // Al bij het tonen, niet pas bij de focus: Qt kan het toetsenbord al opvragen voordat
    // dit filter de focus ziet. Op Xanders iPhone schoof Nieuwe kaartbron na een tik in de
    // boom het hele blad omhoog (Qt scrollToCursor voor het opkomende toetsenbord), met
    // Gereed buiten beeld en onderaan een zwart vlak (12 nov 2026).
    if ((ev->type() == QEvent::Show || ev->type() == QEvent::Polish) && obj->isWidgetType()) {
      if (auto* iv = qobject_cast<QAbstractItemView*>(static_cast<QWidget*>(obj)))
        if (iv->testAttribute(Qt::WA_InputMethodEnabled))
          iv->setAttribute(Qt::WA_InputMethodEnabled, false);
    }
    // Tekstveld dat de focus krijgt zonder tik (wx SetFocus, eerste veld van een nieuw
    // venster): geen toetsenbord. Anders kwam het onverwacht op en schoof Qt het blad
    // omhoog (zie hierboven). Na een tik op het veld werkt het gewoon.
    if (ev->type() == QEvent::FocusIn && obj->isWidgetType()) {
      auto* fw = static_cast<QWidget*>(obj);
      auto* fe = static_cast<QFocusEvent*>(ev);
      const bool textInput = qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw) ||
                             qobject_cast<QPlainTextEdit*>(fw) ||
                             qobject_cast<QAbstractSpinBox*>(fw);
      if (textInput && fe->reason() != Qt::MouseFocusReason && !IsMainWindow(fw->window())) {
        QPointer<QWidget> g(fw);
        QTimer::singleShot(0, fw, [g]() {
          if (!g || !g->hasFocus()) return;
          wxLogMessage("opencpn-ios: focus zonder tik op %s, geen toetsenbord",
                       g->metaObject()->className());
          g->clearFocus();
          if (QInputMethod* im = QGuiApplication::inputMethod()) im->hide();
        });
      }
      // Na een tik op een veld laag in een blad: het blad zelf zo verschuiven dat het veld
      // in de bovenste helft staat, vóór het toetsenbord opkomt. Anders schoof Qt het hele
      // venster omhoog: kop buiten beeld en een zwart vlak boven het toetsenbord (HIG-review
      // ronde 1, punt 8; Opties > Gebruikersinterface, veld "seconden").
      if (textInput && fe->reason() == Qt::MouseFocusReason && !IsMainWindow(fw->window()))
        ScrollFieldAboveKeyboard(fw);
    }
    // Een blad wordt weer actief na een iOS-venster erboven (lettertypekiezer,
    // deelvenster): iOS gaf het veld met de focus het toetsenbord terug, zonder dat er
    // op een veld getikt was; het veld lag onder het toetsenbord (Opties >
    // Gebruikersinterface, HIG-review 26 nov 2026, punt 8). Dan de focus weghalen.
    if (ev->type() == QEvent::WindowActivate && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      if (w->isWindow() && !IsMainWindow(w) && IsManagedWindow(w)) {
        QPointer<QWidget> win(w);
        const QPointF press = m_lastPress;
        QTimer::singleShot(0, w, [win, press]() {
          if (!win) return;
          QWidget* f = win->focusWidget();
          if (!f || !f->testAttribute(Qt::WA_InputMethodEnabled)) return;
          const QRect r(f->mapToGlobal(QPoint(0, 0)), f->size());
          if (r.adjusted(-8, -8, 8, 8).contains(press.toPoint())) return;  // erop getikt
          f->clearFocus();
          if (QInputMethod* im = QGuiApplication::inputMethod()) im->hide();
        });
      }
    }
    // Toetsenbord dat bij de invoer past: een veld met een getal of een positie
    // ("51° 49.5964' N") kreeg het lettertoetsenbord met woordsuggesties en
    // autocorrectie (HIG-review 26 nov 2026, punt 8). Nu cijfers en leestekens, zonder
    // suggesties. Een leeg veld of gewone tekst houdt het gewone toetsenbord.
    if (ev->type() == QEvent::FocusIn && obj->isWidgetType())
      if (auto* le = qobject_cast<QLineEdit*>(static_cast<QWidget*>(obj)))
        SetNumericKeyboard(le);
    if (ev->type() == QEvent::FocusIn && obj->isWidgetType()) {
      if (auto* iv = qobject_cast<QAbstractItemView*>(static_cast<QWidget*>(obj))) {
        if (iv->testAttribute(Qt::WA_InputMethodEnabled)) {
          iv->setAttribute(Qt::WA_InputMethodEnabled, false);
          if (QInputMethod* im = QGuiApplication::inputMethod()) {
            im->update(Qt::ImEnabled);
            im->hide();
          }
        }
      }
    }
    if (ev->type() == QEvent::Show && obj->isWidgetType()) {
      auto* w = static_cast<QWidget*>(obj);
      // OpenCPN's eigen tooltips (gui/src/tooltip.cpp, een wxFrame): op een aanraakscherm
      // blijven ze staan na een tik, zonder weg te gaan ("Koers-Boven / GNSS (0
      // satellieten)", "kan het niet wegklikken en het lijkt totaal niet op iOS", Xander
      // 26 nov 2026). iOS kent geen tooltips; de knoppen op de kaart tonen hun stand zelf.
      if (w->isWindow()) {
        if (dynamic_cast<Tooltip*>(wxWindow::QtRetrieveWindowPointer(w))) {
          w->setWindowOpacity(0.0);
          QPointer<QWidget> g(w);
          QTimer::singleShot(0, w, [g]() { if (g) g->hide(); });
          return false;
        }
      }
      // Qt-melding (wxMessageBox): te lange tekst schuifbaar, melding op het scherm.
      if (auto* mb = qobject_cast<QMessageBox*>(w)) {
        QPointer<QMessageBox> g(mb);
        FitMessageBox(mb);
        QTimer::singleShot(0, mb, [g]() { if (g) FitMessageBox(g); });
      }
      // Schuifregelaars: streepjes (wxSL_AUTOTICKS) tekende Qt los onder de regelaar
      // (Weergaveopties > Uitgebreid, iPad 10 nov 2026); iOS-regelaars hebben ze niet.
      if (auto* sl = qobject_cast<QSlider*>(w)) {
        if (sl->tickPosition() != QSlider::NoTicks) sl->setTickPosition(QSlider::NoTicks);
      }
      if (auto* hv = qobject_cast<QHeaderView*>(w)) {
        QPointer<QHeaderView> guard(hv);
        QTimer::singleShot(0, hv, [guard]() { WidenHeaderSections(guard); });
      }
      // Hoofdvenster: de kaart mag tot de schermranden. Standaard houdt Qt de veilige
      // zones van iOS vrij; liggend bleef zo links ~62 pt (eiland), rechts ~62 pt en
      // onder ~20 pt (thuisbalk) onbenut (simulator iPhone 17 Pro Max, 8 nov 2026).
      // Uitzetten met env OCPNIOS_SAFE_AREA.
      if (w->isWindow() && IsMainWindow(w) && !std::getenv("OCPNIOS_SAFE_AREA") &&
          w->testAttribute(Qt::WA_ContentsMarginsRespectsSafeArea)) {
        w->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
        ApplySafeArea(w);
        if (QWindow* qw = w->windowHandle()) {
          QPointer<QWidget> guard(w);
          QObject::connect(qw, &QWindow::safeAreaMarginsChanged, w, [guard]() {
            if (guard) ApplySafeArea(guard);
          });
        }
      }
      if (IsManagedWindow(w)) {
        w->setProperty("ocpnios_shownseq", ++g_showSeq);
        // Een venster tijdens het opstarten (vraag, voortgang) mag niet onder de
        // bezig-melding verdwijnen: die dan weghalen (app/ios/startup_busy.mm). Een
        // verborgen OK-melding hield de start eindeloos op (iPad, 10 nov 2026).
        if (!IsMainWindow(w)) ocpnios_HideStartupBusy();
        w->setProperty(kWantedSizeProp, w->size());
        // Sleepgreepje (wxRESIZE_BORDER) verbergen: op iOS verschuif je geen vensters;
        // het stond als stippeltjes naast "Annuleren" in de kopbalk.
        for (QSizeGrip* grip : w->findChildren<QSizeGrip*>()) grip->hide();
        EnsureSheetHeader(w);
        FitToScreen(w);
        RaiseWindowLevel(w);
        // Twee keer: direct na deze event-ronde, en nog eens na 250 ms. Een venster dat
        // vanuit een tik op de werkbalk opent, raakt anders achter het hoofdvenster
        // zodra de tik wordt afgehandeld (losgelaten).
        QPointer<QWidget> guard(w);
        for (int delay : {0, 250}) {
          QTimer::singleShot(delay, w, [guard]() {
            if (!guard || !guard->isVisible()) return;
            FitToScreen(guard);
            FitSheetContent(guard);
            guard->raise();
            if (guard->property(kSheetProp).toInt() == 4) {
              RoundPanel(guard);  // soort 4 is pas hier bekend (iPhone, 11 nov 2026)
              RaiseSheetsAbovePanels();  // paneel niet over een open blad
            } else {
              guard->activateWindow();
            }
          });
        }
      }
    }
    return QObject::eventFilter(obj, ev);
  }
};

void InstallWindowFit() {
  // Licht kleurenschema, ook als iOS in de donkere modus staat. Qt neemt anders donkere
  // achtergronden over, terwijl OpenCPN veel tekst zelf zwart tekent: in Instellingen
  // waren de labels op de iPhone onzichtbaar (schermopname Xander, 8 nov 2026).
  // OpenCPN's eigen nachtstand (kleurenschema's) staat hier los van.
  if (!std::getenv("OCPNIOS_SYSTEM_COLOR_SCHEME"))
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);

  if (std::getenv("OCPNIOS_NO_WINDOW_FIT")) return;
  // wx' eigen "layout adaptation" zet een te hoge dialoog in een wxScrolledWindow
  // (radar-voorkeuren). Die schoof op iOS niet met de vinger mee (schuifbalk bewoog,
  // inhoud niet; 10 nov 2026). Uit: de bladen krijgen hun eigen scrollvlak
  // (UpdateSheetScroll).
  wxDialog::EnableLayoutAdaptation(false);
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) return;
  auto* filter = new WindowFitFilter(app);
  app->installEventFilter(filter);

  // Venstermodus (iPadOS 26): verschaalt de gebruiker het app-venster, dan merkt Qt dat
  // niet als schermwijziging. Elke halve seconde de maat vergelijken en bij verandering
  // alle open vensters opnieuw passend maken ("Safe Restart" bleef anders te hoog in
  // een kleiner venster, simulator 10 nov 2026).
  auto* watch = new QTimer(filter);
  watch->setInterval(500);
  QObject::connect(watch, &QTimer::timeout, filter, []() {
    static QRect last;
    const QRect now = ocpnios_AppWindowRect();
    if (!now.isValid() || now == last) return;
    const bool first = !last.isValid();
    last = now;
    if (first) return;
    for (QWidget* top : QApplication::topLevelWidgets())
      if (top->isVisible() && IsMainWindow(top)) ApplySafeArea(top);
    for (QWidget* top : QApplication::topLevelWidgets())
      if (top->isVisible() && IsManagedWindow(top) && !IsMainWindow(top)) {
        FitToScreen(top);
        FitSheetContent(top, true);
      }
    RaiseFloatingWindows(true);
  });
  // Pas starten als de eventlus draait (direct starten crashte in CFRunLoopAddTimer,
  // 10 nov 2026).
  QMetaObject::invokeMethod(watch, [watch]() { watch->start(); }, Qt::QueuedConnection);

  // Scherm gedraaid: open vensters opnieuw passend maken (terug naar hun gevraagde maat,
  // dan inkorten) en naar voren halen. Twee keer, want het hoofdvenster krijgt zijn
  // nieuwe maat pas na een paar event-rondes. Dialogen blijven zo in beeld; een open
  // wxFrame (Data Monitor) raakt na draaien nog achter de kaart (niet opgelost).
  if (QScreen* screen = QGuiApplication::primaryScreen()) {
    QObject::connect(screen, &QScreen::geometryChanged, filter, [filter]() {
      for (int delay : {100, 400, 1000}) {
        QTimer::singleShot(delay, filter, []() { RaiseFloatingWindows(true); });
      }
    });
  }
}

}  // namespace

// Draait zodra QApplication is aangemaakt (wxApp maakt hem in wxEntry).
Q_COREAPP_STARTUP_FUNCTION(InstallWindowFit)

namespace {
// Room (points) an overlay at the left or right edge of a chart canvas keeps free, for the
// span top..bottom (canvas client points). Only what overlaps the island vertically keeps
// clear of it; everything else goes to the edge and only avoids the rounded corners
// (Xander after TestFlight build 5: the zoom bar stayed 62 pt from the right edge in
// landscape, while the island is only in the middle of the screen height).
//
// Assumptions, measured in the iPhone 17 Pro simulator with a masked screenshot
// (24 nov 2026): the island spans 139..263 of 402 pt, so the middle half of the height
// (101..302) is taken as its band; this also covers the wider notch of older iPhones.
// The corner follows a circle with a radius equal to the landscape side inset (62 pt);
// on notch iPhones those two values are also close. In portrait no side has an island
// and the chart starts and ends far enough from the corners, so the result is 0.
int OverlayInset(wxWindow* canvas, int top, int bottom, bool right) {
  if (!canvas) return 0;
  QWidget* cw = canvas->GetHandle();
  QWindow* qw = cw && cw->window() ? cw->window()->windowHandle() : nullptr;
  QScreen* screen = qw && qw->screen() ? qw->screen() : QGuiApplication::primaryScreen();
  if (!qw || !screen) return 0;
  const QRect sg = screen->geometry();
  const QMargins s = qw->safeAreaMargins();
  const Qt::ScreenOrientation o = screen->orientation();
  const QPoint p0 = cw->mapToGlobal(QPoint(0, top));
  const QPoint p1 = cw->mapToGlobal(QPoint(cw->width(), bottom));
  const int h = sg.height();
  // The canvas lies inside the window's top and bottom margins (ApplySafeArea). Clamp to
  // them: at startup the toolbar asks before the canvas is laid out (canvas still at
  // y = 4 instead of 63 in portrait, simulator 24 nov 2026), which indented it 45 pt.
  const int y0 = std::max(p0.y() - sg.top(), s.top());
  const int y1 = std::min(p1.y() - sg.top(), h - s.bottom());
  // How far the canvas edge on this side already is from the screen edge.
  const int gap = right ? sg.right() - p1.x() : p0.x() - sg.left();

  int need = 0;
  const bool islandSide = right ? (o == Qt::LandscapeOrientation)
                                : (o == Qt::InvertedLandscapeOrientation);
  const int side = right ? s.right() : s.left();
  if (islandSide && side > 0 && y1 > h / 4 && y0 < h * 3 / 4) need = side;

  const double r = std::max({s.left(), s.right(), s.top()});
  // Horizontal intrusion of the corner at distance d from the top or bottom edge, plus
  // 4 pt so a box's sharp corner does not touch the curve (it had 2 pt without). Below
  // 1 pt (the zoom bar, 55 pt above the bottom edge) the corner does not count.
  auto corner = [r](double d) {
    if (d >= r) return 0.0;
    const double a = r - std::max(0.0, d);
    const double x = r - std::sqrt(r * r - a * a);
    return x < 1 ? 0.0 : x + 4;
  };
  need = std::max(need, int(std::ceil(std::max(corner(y0), corner(h - y1)))));
  return std::max(0, need - std::max(0, gap));
}
}  // namespace

// Voor OpenCPN (chcanv, toolbar, MUIBar, piano).
int ocpnios_OverlayInsetLeft(wxWindow* canvas, int top, int bottom) {
  return OverlayInset(canvas, top, bottom, false);
}
int ocpnios_OverlayInsetRight(wxWindow* canvas, int top, int bottom) {
  return OverlayInset(canvas, top, bottom, true);
}

// libs/gui/src/dialog_base.cpp (BaseDialog::AddHtmlContent): the html height is
// measured while the window is still narrow and came out taller than the
// screen; the buttons (e.g. Agree/Cancel of the welcome dialog) then fell off.
// Limit it to 60% of the app window (not the whole screen in windowed mode on
// iPadOS); longer text scrolls.
void ocpnios_FitDialogHtml(wxWindow* dlg, int& width, int& height) {
  wxRect area = wxDisplay(dlg).GetClientArea();
  const QRect app = ocpnios_AppWindowRect();
  if (app.isValid()) area = wxRect(app.x(), app.y(), app.width(), app.height());
  height = std::min(height, std::max(120, area.height * 6 / 10));
  width = std::min(width, area.width - 40);
}
