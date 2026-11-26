// iOS map controls (HIG review, round 2; same approach as toolbar_ios.cpp): the zoom bar
// at the bottom right (zoom in/out, follow ship, chart display options) and the compass
// / GNSS box at the top right, as iOS buttons with SF Symbols in the toolbar's colours.
//
// OpenCPN's own versions stay in place but out of sight: the MUIBar is moved off the
// screen (it is drawn at m_screenPos and hit-tested there), the compass window is
// switched off per chart canvas. The iOS buttons send the same commands: ID_ZOOMIN,
// ID_ZOOMOUT and ID_FOLLOW to the canvas, SetUpMode() as a tap on the compass did, and the
// chart display options (CanvasOptions) as the MUIBar menu button opened them.
// "Show compass and GPS status window" in Options still switches the compass box.
// No core change.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include <wx/wx.h>

#include <QApplication>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <QLabel>
#include <QTransform>
#include <QAbstractScrollArea>
#include <QDate>
#include <QStyle>
#include <functional>
#include <QScreen>
#include <QWindow>

#include "model/comm_vars.h"    // g_bSatValid, g_SatsInView
#include "model/config_vars.h"  // g_bShowCompassWin
#include "model/gui_vars.h"     // g_CanvasHideNotificationIcon
#include "model/notification_manager.h"
#include "notification_manager_gui.h"  // NotificationsList
#include "model/idents.h"       // ID_ZOOMIN, ID_ZOOMOUT, ID_FOLLOW
#include "model/own_ship.h"     // bGPSValid
#include "canvas_options.h"
#include "chartdb.h"  // ChartData
#include "chcanv.h"
#include "compass.h"
#include "mui_bar.h"
#include "navutil.h"  // pConfig
#include "ocpn_frame.h"
#include "viewport.h"  // global_color_scheme

#ifdef __OCPN__IOS__

QImage ocpnios_SymbolImage(const char* name, int pointSize);         // ios_look.mm
int ocpnios_OverlayInsetLeft(wxWindow* canvas, int top, int bottom);   // window_fit.cpp
int ocpnios_OverlayInsetRight(wxWindow* canvas, int top, int bottom);  // window_fit.cpp
void ocpnios_ToolbarRelayout();      // toolbar_ios.cpp
void ocpnios_StatusPanelRelayout();  // status_panel.cpp

namespace {

wxString Tr(const char* en, const char* nl) {
  const wxLocale* l = wxGetLocale();
  if (l && l->GetCanonicalName().StartsWith("nl")) return wxString::FromUTF8(nl);
  return wxString::FromUTF8(en);
}
QString Q(const wxString& s) { return QString::fromUtf8(s.utf8_str().data()); }

constexpr int kButton = 44;
constexpr int kPad = 4;

struct Look {
  QColor bar, icon, on;
};
Look CurrentLook() {
  switch (global_color_scheme) {
    case GLOBAL_COLOR_SCHEME_NIGHT:
      return {QColor(0, 0, 0, 230), QColor(150, 60, 60), QColor(60, 20, 20)};
    case GLOBAL_COLOR_SCHEME_DUSK:
      return {QColor(28, 28, 30, 230), QColor(200, 200, 204), QColor(80, 80, 86)};
    default:
      return {QColor(40, 40, 42, 225), QColor(255, 255, 255), QColor(110, 110, 116)};
  }
}

QIcon Symbol(const char* name, const QColor& c, double rotateDeg = 0, bool turnable = false) {
  QImage img = ocpnios_SymbolImage(name, 21);
  if (img.isNull()) return QIcon();
  img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  if (turnable) {
    // Room around a symbol that turns, so its corners stay whole (the tip of the north
    // arrow was cut off); always, so it keeps one size whether turned or not.
    const qreal r = img.devicePixelRatio();
    // Square, so a wide symbol (antenna) takes the same height as a tall one (arrow)
    // and the words under them stay on one line.
    const int side = std::max(img.width(), img.height());
    const int m = side / 5;
    QImage padded(side + 2 * m, side + 2 * m, QImage::Format_ARGB32_Premultiplied);
    padded.fill(Qt::transparent);
    img.setDevicePixelRatio(1.0);
    QPainter pp(&padded);
    pp.drawImage(m + (side - img.width()) / 2, m + (side - img.height()) / 2, img);
    pp.end();
    padded.setDevicePixelRatio(r);
    img = padded;
  }
  const qreal dpr = img.devicePixelRatio();
  {
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), c);
  }
  if (std::fabs(rotateDeg) > 0.5) {
    // Draw in pixels: an image with a pixel ratio of 3 drawn onto one of 1 comes out at a
    // third of its size (the rose was a dot after turning the chart, Xander 26 nov 2026).
    img.setDevicePixelRatio(1.0);
    QImage rot(img.size(), QImage::Format_ARGB32_Premultiplied);
    rot.fill(Qt::transparent);
    QPainter p(&rot);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(img.width() / 2.0, img.height() / 2.0);
    p.rotate(rotateDeg);
    p.translate(-img.width() / 2.0, -img.height() / 2.0);
    p.drawImage(0, 0, img);
    p.end();
    img = rot;
  }
  img.setDevicePixelRatio(dpr);
  return QIcon(QPixmap::fromImage(img));
}

// Rounded dark capsule with 44 pt buttons, vertical or horizontal.
class Capsule : public QWidget {
 public:
  Capsule(QWidget* parent, Qt::Orientation o) : QWidget(parent), m_orient(o) {
    setAttribute(Qt::WA_NoMousePropagation);
  }
  QToolButton* Add() {
    auto* b = new QToolButton(this);
    b->setFixedSize(kButton, kButton);
    b->setIconSize(QSize(26, 26));
    m_buttons.push_back(b);
    Restyle();
    Layout();
    return b;
  }
  void Restyle() {
    m_look = CurrentLook();
    const QString style =
        QString("QToolButton { border: none; border-radius: 9px; background: transparent;"
                " color: %2; font-size: 10px; font-weight: 600; padding: 0px; }"
                " QToolButton:pressed { background: %1; }"
                " QToolButton:checked { background: %1; }")
            .arg(m_look.on.name(QColor::HexArgb), m_look.icon.name());
    for (QToolButton* b : m_buttons) b->setStyleSheet(style);
    update();
  }
  // Hidden buttons (the bell without notifications) take no room.
  void Layout() {
    int n = 0;
    for (QToolButton* b : m_buttons) {
      if (b->isHidden()) continue;
      b->move(m_orient == Qt::Vertical ? QPoint(kPad, kPad + n * kButton)
                                       : QPoint(kPad + n * kButton, kPad));
      n++;
    }
    resize(m_orient == Qt::Vertical ? QSize(kButton + 2 * kPad, n * kButton + 2 * kPad)
                                    : QSize(n * kButton + 2 * kPad, kButton + 2 * kPad));
  }
  const Look& look() const { return m_look; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_look.bar);
    p.drawRoundedRect(rect(), 12, 12);
  }

 private:
  Qt::Orientation m_orient;
  std::vector<QToolButton*> m_buttons;
  Look m_look = CurrentLook();
};

int s_compassWanted = -1;  // the user's "Show compass and GPS status window"

void SaveCompassWanted() {
  if (!pConfig) return;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath("/Settings/iOS");
  pConfig->Write("CompassWanted", (long)s_compassWanted);
  pConfig->SetPath(old);
  pConfig->Flush();
}

QPointer<Capsule> s_zoom, s_compass;
QToolButton *s_zin = nullptr, *s_zout = nullptr, *s_follow = nullptr, *s_menu = nullptr;
QToolButton *s_rose = nullptr, *s_gps = nullptr, *s_bell = nullptr;

QString UpModeWord(int mode) {
  if (mode == COURSE_UP_MODE) return Q(Tr("Course", "Koers"));
  if (mode == HEAD_UP_MODE) return Q(Tr("Head", "Kop"));
  return Q(Tr("North", "Noord"));
}

QString GpsWord() {
  if (!bGPSValid) return Q(Tr("No fix", "Geen fix"));
  if (!g_bSatValid || g_SatsInView < 0) return "GPS";
  return QString("%1 sat").arg(g_SatsInView);
}
CanvasOptions* s_options = nullptr;  // cleared when destroyed
QPointer<QLabel> s_scale;  // chart scale next to the zoom bar on a wide screen
QTimer* s_timer = nullptr;
QString s_state;  // what the icons show now; redraw only on change

QMainWindow* MainWindow() {
  wxWindow* top = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
  return top ? qobject_cast<QMainWindow*>(top->GetHandle()) : nullptr;
}

ChartCanvas* Canvas() { return gFrame ? gFrame->GetPrimaryCanvas() : nullptr; }

void Post(int id) {
  ChartCanvas* cc = Canvas();
  if (!cc) return;
  wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, id);
  cc->GetEventHandler()->AddPendingEvent(evt);
}

void OpenChartOptions() {
  ChartCanvas* cc = Canvas();
  if (!cc) return;
  if (!s_options) {
    s_options = new CanvasOptions(cc);
    // Size and place as MUIBar::HandleMenuClick does; window_fit then shows it as a
    // sheet (it was a thin strip at the top without this).
    const int size_y = std::max(100, cc->GetSize().y - 2 * cc->GetCharHeight());
    s_options->SetSize(wxSize(-1, size_y));
    s_options->Move(cc->ClientToScreen(wxPoint(cc->GetSize().x, 0)));
    s_options->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
      e.Skip();
      if (e.GetEventObject() == s_options) s_options = nullptr;
    });
  }
  // As the MUIBar: ENC options only when the chart group holds ENCs.
  s_options->SetENCAvailable(ChartData && ChartData->IsENCInGroup(cc->m_groupIndex));
  s_options->RefreshControlValues();
  if (s_options->IsShown())
    s_options->Hide();
  else
    s_options->Show();
}

// A notification row of OpenCPN (NotificationPanel) as an iOS list row (Xander 26 nov
// 2026: "die herhaalde test notificatie ziet er ook nog totaal niet iOS-achtig uit"):
// white, no frame; the repeat count only when above 1 ("2x"); the time small and grey,
// without seconds and without the date when it is today; OK as a blue text button.
class RowPaint : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::Paint) return false;
    auto* w = static_cast<QWidget*>(obj);
    QPainter p(w);
    // The corners outside the rounded row in the list's own colour (they stayed black).
    p.fillRect(w->rect(), w->window()->palette().color(QPalette::Window));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const bool dark = qApp->property("ocpnios_dark").toBool();
    p.setBrush(dark ? QColor(44, 44, 46) : QColor(255, 255, 255));
    p.drawRoundedRect(QRectF(w->rect()).adjusted(4, 3, -4, -3), 10, 10);
    return true;  // instead of OpenCPN's grey frame (NotificationPanel::OnPaint)
  }
};

// List area and rows as wide as the sheet; OpenCPN gives them the width the window had
// when they were made. Again after window_fit sized the sheet.
void FitNotificationWidths(wxWindow* dlg) {
  if (!dlg) return;
  const int fullW = dlg->GetClientSize().x - 8;
  if (fullW < 100) return;
  for (wxWindow* c : dlg->GetChildren())
    if (auto* lp = dynamic_cast<NotificationListPanel*>(c)) {
      lp->SetMinSize(wxSize(fullW, lp->GetMinSize().y));
      for (wxWindow* r : lp->GetChildren())
        if (dynamic_cast<NotificationPanel*>(r)) r->SetMinSize(wxSize(fullW - 4, -1));
      lp->Layout();
      lp->FitInside();
    }
  dlg->Layout();
}

void StyleNotificationRows(wxWindow* list) {
  if (!list) return;
  // OpenCPN's list area: black palette and the width it had when made. Now the sheet's
  // colour and its full width.
  if (QWidget* top = list->GetHandle()) {
    const QColor bg = top->palette().color(QPalette::Window);
    const int fullW = list->GetClientSize().x - 8;
    for (wxWindow* c : list->GetChildren())
      if (auto* lp = dynamic_cast<NotificationListPanel*>(c)) {
        lp->SetBackgroundColour(wxColour(bg.red(), bg.green(), bg.blue()));
        if (QWidget* lq = lp->GetHandle()) {
          QPalette pal = lq->palette();
          pal.setColor(QPalette::Window, bg);
          lq->setPalette(pal);
          lq->setAutoFillBackground(true);
          if (auto* sa = qobject_cast<QAbstractScrollArea*>(lq))
            if (sa->viewport()) {
              sa->viewport()->setPalette(pal);
              sa->viewport()->setAutoFillBackground(true);
            }
        }
      }
  }
  const QString today = QDate::currentDate().toString("dd-MM-yyyy");
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (auto* row = dynamic_cast<NotificationPanel*>(c)) {
        QWidget* q = row->GetHandle();
        if (!q || q->property("ocpnios_noterow").toBool()) continue;
        q->setProperty("ocpnios_noterow", true);
        q->installEventFilter(new RowPaint(q));
        std::vector<wxStaticText*> texts;
        for (wxWindow* k : row->GetChildren()) {
          if (auto* t = dynamic_cast<wxStaticText*>(k)) texts.push_back(t);
          if (auto* b = dynamic_cast<wxButton*>(k))
            if (QWidget* bq = b->GetHandle()) {
              bq->setProperty("ocpnios_nav", true);
              bq->style()->unpolish(bq);
              bq->style()->polish(bq);
            }
        }
        // Order as NotificationPanel makes them: repeat count, time, message.
        if (texts.size() >= 3) {
          const wxString count = texts[0]->GetLabel().AfterLast('\n').Strip(wxString::both);
          long n = 1;
          count.ToLong(&n);
          if (n > 1)
            texts[0]->SetLabel(wxString::Format("%ld", n) + wxString::FromUTF8("\u00d7"));
          else
            texts[0]->Hide();
          // "dd-mm-yyyy\nhh:mm:ss" -> "hh:mm" (today) or "dd-mm hh:mm".
          const QString t = Q(texts[1]->GetLabel());
          const QString date = t.section('\n', 0, 0).trimmed();
          const QString time = t.section('\n', 1, 1).trimmed().left(5);
          const QString shown = (date == today || time.isEmpty()) ? (time.isEmpty() ? date : time)
                                                                  : date.left(5) + " " + time;
          texts[1]->SetLabel(wxString::FromUTF8(shown.toUtf8().constData()));
          for (wxStaticText* t2 : {texts[0], texts[1]})
            if (QWidget* tq = t2->GetHandle())
              tq->setStyleSheet("QLabel { color: #8e8e93; font-size: 13px; background: transparent; }");
          if (QWidget* mq = texts[2]->GetHandle())
            mq->setStyleSheet("QLabel { font-size: 16px; background: transparent; }");
        }
        row->Layout();
      } else {
        visit(c);
      }
    }
  };
  visit(list);
  FitNotificationWidths(list);
}

// The list of notifications, as a tap on OpenCPN's bell opened it
// (ChartCanvas::HandleNotificationMouseClick, which is private).
NotificationsList* s_notes = nullptr;  // cleared when destroyed
void ToggleNotifications() {
  ChartCanvas* cc = Canvas();
  if (!cc) return;
  if (!s_notes) {
    s_notes = new NotificationsList(cc);
    s_notes->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
      e.Skip();
      if (e.GetEventObject() == s_notes) s_notes = nullptr;
    });
    // A sheet with Done, not an alert (window_fit.cpp): each notification has its own
    // OK button, which looked like the dialog's.
    if (QWidget* q = s_notes->GetHandle()) q->setProperty("ocpnios_listsheet", true);
    // Short title, as an iOS sheet ("OpenCPN Notifications" had no Dutch translation).
    s_notes->SetTitle(Tr("Notifications", "Meldingen"));
    s_notes->RecalculateSize();
    s_notes->Hide();
  }
  if (s_notes->IsShown()) {
    s_notes->Hide();
  } else {
    s_notes->RecalculateSize();
    s_notes->ReloadNotificationList();
    StyleNotificationRows(s_notes);
    s_notes->Show();
    // Again once the sheet has its width (window_fit sizes it after Show).
    QTimer::singleShot(300, qApp, []() {
      if (s_notes && s_notes->IsShown()) FitNotificationWidths(s_notes);
    });
  }
}

// Keep OpenCPN's own zoom bar and compass out of sight. Also right after a resize
// (rotation), when OpenCPN places them again.
void HideCore() {
  if (!gFrame) return;
  // The bell is in the compass box below; OpenCPN's own one is not drawn.
  g_CanvasHideNotificationIcon = true;
  for (unsigned i = 0; i < g_canvasArray.GetCount(); i++) {
    ChartCanvas* cc = g_canvasArray.Item(i);
    if (!cc) continue;
    if (MUIBar* mb = cc->GetMUIBar())
      if (mb->m_screenPos.x > -5000) mb->m_screenPos = wxPoint(-10000, -10000);
    // OpenCPN's compass is not drawn (g_bShowCompassWin off) but could still be "shown"
    // from a moment g_bShowCompassWin was on: then it took taps in its (invisible) box,
    // turned the chart and showed its tooltip "Course Up / GNSS (0 satellites)", after
    // rotating in the middle of the screen (Xander, iPhone, 26 nov 2026).
    if (ocpnCompass* c = cc->GetCompass())
      if (c->IsShown()) c->Show(false);
  }
}

const char* GpsSymbolAndColor(QColor* c) {
  if (!bGPSValid) {
    *c = QColor(255, 69, 58);  // no position: red
    return "location.slash";
  }
  if (!g_bSatValid) {
    *c = QColor(142, 142, 147);  // stale
    return "location";
  }
  *c = g_SatsInView > 4 ? QColor(52, 199, 89) : QColor(255, 204, 0);
  return "location.fill";
}

void Update() {
  QMainWindow* mw = MainWindow();
  ChartCanvas* cc = Canvas();
  if (!mw || !cc) return;
  HideCore();
  if (g_bShowCompassWin) {  // set again elsewhere (plugin API): that is the user's wish
    s_compassWanted = 1;
    SaveCompassWanted();
    g_bShowCompassWin = false;
  }
  if (!s_zoom) {
    s_zoom = new Capsule(mw, Qt::Vertical);
    s_zin = s_zoom->Add();
    s_zout = s_zoom->Add();
    s_follow = s_zoom->Add();
    s_menu = s_zoom->Add();
    s_follow->setCheckable(true);
    // Names for VoiceOver and the accessibility API (also how the Mac clicks them).
    auto name = [](QToolButton* b, const wxString& t) {
      b->setAccessibleName(Q(t));
      b->setToolTip(Q(t));
    };
    name(s_zin, Tr("Zoom in", "Inzoomen"));
    name(s_zout, Tr("Zoom out", "Uitzoomen"));
    name(s_follow, Tr("Follow ship", "Schip volgen"));
    name(s_menu, Tr("Chart display options", "Weergaveopties kaart"));
    QObject::connect(s_zin, &QToolButton::clicked, s_zoom, []() { Post(ID_ZOOMIN); });
    QObject::connect(s_zout, &QToolButton::clicked, s_zoom, []() { Post(ID_ZOOMOUT); });
    QObject::connect(s_follow, &QToolButton::clicked, s_zoom, []() { Post(ID_FOLLOW); });
    QObject::connect(s_menu, &QToolButton::clicked, s_zoom, []() { OpenChartOptions(); });
    s_compass = new Capsule(mw, Qt::Horizontal);
    s_bell = s_compass->Add();
    s_rose = s_compass->Add();
    s_gps = s_compass->Add();
    s_bell->hide();
    s_compass->Layout();
    s_bell->setAccessibleName(Q(Tr("Notifications", "Meldingen")));
    s_rose->setAccessibleName(Q(Tr("Chart orientation", "Kaartoriëntatie")));
    s_gps->setAccessibleName(Q(Tr("GPS status", "GPS-status")));
    QObject::connect(s_bell, &QToolButton::clicked, s_compass, []() { ToggleNotifications(); });
    QObject::connect(s_rose, &QToolButton::clicked, s_compass, []() {
      ChartCanvas* c = Canvas();
      if (!c) return;
      // Same cycle as a tap on OpenCPN's compass (compass.cpp).
      if (c->GetUpMode() == NORTH_UP_MODE)
        c->SetUpMode(COURSE_UP_MODE);
      else if (c->GetUpMode() == COURSE_UP_MODE)
        c->SetUpMode(HEAD_UP_MODE);
      else
        c->SetUpMode(NORTH_UP_MODE);
    });
  }

  // Icons: only rebuilt when something they show changed.
  QColor gpsColor;
  const char* gpsSym = GpsSymbolAndColor(&gpsColor);
  const int rot = (int)std::lround(cc->GetVPRotation() * 180.0 / M_PI);
  auto& noteman = NotificationManager::GetInstance();
  const int notes = (int)noteman.GetNotificationCount();
  const int severity = notes ? (int)noteman.GetMaxSeverity() : -1;
  const QString state = QString("%1|%2|%3|%4|%5|%6|%7|%8")
                            .arg((int)global_color_scheme)
                            .arg(rot)
                            .arg(cc->GetUpMode())
                            .arg(gpsSym)
                            .arg(gpsColor.name())
                            .arg(cc->GetbFollow())
                            .arg(severity)
                            .arg(GpsWord());
  if (state != s_state) {
    s_state = state;
    s_zoom->Restyle();
    s_compass->Restyle();
    const QColor ic = s_zoom->look().icon;
    s_zin->setIcon(Symbol("plus.magnifyingglass", ic));
    s_zout->setIcon(Symbol("minus.magnifyingglass", ic));
    s_follow->setIcon(Symbol(cc->GetbFollow() ? "location.north.fill" : "location.north", ic));
    s_follow->setChecked(cc->GetbFollow());
    s_menu->setIcon(Symbol("square.3.layers.3d", ic));
    const bool day = global_color_scheme == GLOBAL_COLOR_SCHEME_DAY ||
                     global_color_scheme == GLOBAL_COLOR_SCHEME_RGB;
    double roseDeg = -cc->GetVPRotation() * 180.0 / M_PI;
    // Own tests only: env OCPNIOS_TEST_ROSE_DEG turns the rose as a turned chart would.
    if (const char* t = std::getenv("OCPNIOS_TEST_ROSE_DEG")) roseDeg += std::atof(t);
    const int mode = cc->GetUpMode();
    // North arrow turned with the chart, the mode in a word; a satellite symbol in the GPS
    // colour with the number of satellites (Xander chose this over OpenCPN's rose and GPS
    // bars, 26 nov 2026: "we gaan voor variant B"; with the old icons he understood
    // "weinig van de werking").
    for (QToolButton* b : {s_rose, s_gps}) b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    s_rose->setIconSize(QSize(28, 28));  // the room around the arrow (Symbol turnable)
    s_rose->setIcon(Symbol("location.north.fill", ic, roseDeg, true));
    s_rose->setText(UpModeWord(mode));
    s_gps->setIconSize(QSize(28, 28));  // same size and room as the rose: words on one line
    s_gps->setIcon(Symbol(bGPSValid ? "antenna.radiowaves.left.and.right" : "location.slash",
                          day ? gpsColor : gpsColor.darker(220), 0, true));
    s_gps->setText(GpsWord());
    s_rose->setToolTip(UpModeWord(mode));
    // The bell only with notifications, coloured by the most severe one, as OpenCPN's.
    s_bell->setVisible(notes > 0);
    // The open list follows: reloaded when notifications come or go, closed when all
    // are acknowledged ("Acknowledge All" did not change it).
    static int s_shownNotes = -1;
    if (s_notes && s_notes->IsShown()) {
      if (notes == 0) {
        s_notes->Hide();
      } else if (notes != s_shownNotes) {
        s_notes->ReloadNotificationList();
        StyleNotificationRows(s_notes);
      }
    }
    s_shownNotes = notes;
    if (notes > 0) {
      const QColor bc = severity == (int)NotificationSeverity::kCritical ? QColor(255, 69, 58)
                        : severity == (int)NotificationSeverity::kWarning ? QColor(255, 204, 0)
                                                                         : ic;
      s_bell->setIconSize(QSize(26, 26));
      s_bell->setIcon(Symbol("bell.fill", day ? bc : bc.darker(220)));
    }
    s_compass->Layout();
  }

  // Positions: zoom bar bottom right, compass top right, clear of island and corners.
  QWidget* central = mw->centralWidget();
  const QRect area = central ? central->geometry() : mw->rect();
  QWidget* cw = cc->GetHandle();
  auto insetRight = [&](int top, int h) {
    if (!cw) return 0;
    const QPoint p = cw->mapFrom(mw, QPoint(area.left(), top));
    return ocpnios_OverlayInsetRight(cc, p.y(), p.y() + h);
  };
  {
    const QSize sz = s_compass->size();
    const int top = area.top() + 8;
    const int x = area.right() - std::max(8, insetRight(top, sz.height())) - sz.width();
    s_compass->move(x, top);
    s_compass->setVisible(s_compassWanted != 0);
    s_compass->raise();
  }
  {
    const QSize sz = s_zoom->size();
    const int top = area.bottom() - 8 - sz.height();
    const int x = area.right() - std::max(8, insetRight(top, sz.height())) - sz.width();
    s_zoom->move(x, top);
    s_zoom->show();
    s_zoom->raise();
  }
  // Chart scale next to the zoom bar, as OpenCPN's bar showed it. On every screen: it was
  // only shown from 700 pt wide, so on an iPhone it came in landscape and went again in
  // portrait (Xander 26 nov 2026: "na kantelen en weer terug valt op de iphone de
  // zoomfactor weg").
  {
    if (!s_scale) s_scale = new QLabel(mw);
    {
      const int scale = (int)cc->GetScaleValue();
      const QString text = scale < 1000000 ? QString("1:%1").arg(scale)
                                            : QString("1:%1 M").arg(scale / 1e6, 0, 'f', 1);
      const Look l = s_zoom->look();
      const QString style = QString("QLabel { background: %1; color: %2; border-radius: 12px;"
                                    " padding: 0 12px; font-size: 15px; font-weight: 600; }")
                                .arg(l.bar.name(QColor::HexArgb), l.icon.name());
      if (s_scale->styleSheet() != style) s_scale->setStyleSheet(style);
      if (s_scale->text() != text) s_scale->setText(text);
      s_scale->adjustSize();
      const int h = kButton + 2 * kPad;
      s_scale->resize(s_scale->width(), h);
      s_scale->move(s_zoom->x() - 8 - s_scale->width(), s_zoom->y() + s_zoom->height() - h);
      s_scale->show();
      s_scale->raise();
    }
  }
}

// All bars on the chart again in their place, right away and a few times while the
// rotation settles: the window size, the safe area and the canvas inside it change one
// after the other. With only the bars' own timers (0.5 and 1 s) they stood a moment at
// the old place, over the status bar (simulator), on the iPhone longer ("na kantelen
// niet meer tegen de kaders uitgelijnd", Xander 26 nov 2026).
void RelayoutAll() {
  for (int delay : {0, 120, 350, 800}) {
    QTimer::singleShot(delay, qApp, []() {
      Update();
      ocpnios_ToolbarRelayout();
      ocpnios_StatusPanelRelayout();
    });
  }
}

class ResizeWatch : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() == QEvent::Resize && obj == MainWindow()) RelayoutAll();
    return false;
  }
};

}  // namespace

// Start (from ocpnios_OnStartupDone, frame_ios.cpp).
void ocpnios_MapControlsInit() {
  if (s_timer) return;
  // Own tests only: env OCPNIOS_TEST_NOTIFICATION=info|warning|critical puts one
  // notification up at startup, to see the bell (in the simulator nothing raises one).
  if (const char* t = std::getenv("OCPNIOS_TEST_NOTIFICATION")) {
    const std::string sev(t);
    NotificationManager::GetInstance().AddNotification(
        sev == "critical"  ? NotificationSeverity::kCritical
        : sev == "warning" ? NotificationSeverity::kWarning
                           : NotificationSeverity::kInformational,
        "Test notification", 600);
  }
  s_timer = new QTimer(qApp);
  s_timer->setInterval(500);
  QObject::connect(s_timer, &QTimer::timeout, qApp, []() { Update(); });
  s_timer->start();
  if (QMainWindow* mw = MainWindow()) {
    mw->installEventFilter(new ResizeWatch(mw));
    if (QWindow* qw = mw->windowHandle())
      QObject::connect(qw, &QWindow::safeAreaMarginsChanged, mw, []() { RelayoutAll(); });
    if (QScreen* sc = mw->screen())
      QObject::connect(sc, &QScreen::orientationChanged, mw, []() { RelayoutAll(); });
  }
  Update();
}

// OpenCPN's own zoom bar and compass never drawn, from the first frame on: called when
// the main frame is built (after the config is read, before the chart canvases exist)
// and after Options were applied (frame_ios.cpp). Without the MUI OpenCPN builds no
// MUIBar at all; the compass is only drawn with g_bShowCompassWin, so the user's choice
// is kept in /Settings/iOS/CompassWanted and g_bShowCompassWin stays off.
void ocpnios_CoreOverlaysOff() {
  g_useMUI = false;
  // The bell is in the compass box (Update); OpenCPN's own one is never drawn.
  g_CanvasHideNotificationIcon = true;
  if (s_compassWanted < 0 && pConfig) {
    const wxString old = pConfig->GetPath();
    pConfig->SetPath("/Settings/iOS");
    long w = -1;
    pConfig->Read("CompassWanted", &w, -1L);
    pConfig->SetPath(old);
    s_compassWanted = w >= 0 ? (int)w : (g_bShowCompassWin ? 1 : 0);
    if (w < 0) SaveCompassWanted();
  } else if (g_bShowCompassWin && s_compassWanted >= 0) {
    // Options applied with the box ticked.
    s_compassWanted = 1;
    SaveCompassWanted();
  }
  g_bShowCompassWin = false;
}

int ocpnios_CompassWanted() { return s_compassWanted; }

void ocpnios_SetCompassWanted(bool wanted) {
  s_compassWanted = wanted ? 1 : 0;
  SaveCompassWanted();
  Update();
}

#endif  // __OCPN__IOS__
