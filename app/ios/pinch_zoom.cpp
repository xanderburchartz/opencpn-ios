// Knijpzoomen op iOS.
//
// Deze event-filter op QApplication rekent het knijpen uit de aanrakingen (factor =
// afstand nu / afstand bij het begin) en stuurt wxZoomGestureEvents met ID 0x51A7:
// begin, updates en einde. ChartCanvas::OnZoom (OpenCPN-patch) gebruikt begin/einde van
// deze filter en zoomt per update met de verandering ten opzichte van de vorige.
// Gemeten 8 nov 2026: op de iPhone komen de aanrakingen hier volledig binnen; in de
// simulator alleen begin en einde, daar komen de updates van wxQt (QPinchGesture).
// Muisbewegingen die iOS tegelijk van de eerste vinger maakt, worden tijdens het knijpen
// weggegooid, zodat de kaart niet ook nog meeschuift.
// Uitzetten met env OCPNIOS_NO_PINCH.
//
// Dezelfde filter herkent ook een dubbele tik en lang indrukken met één vinger. Op iOS
// komen die niet als wx-events aan (gemeten 8 nov 2026: dubbel tikken en lang drukken op
// een AIS-doel deden niets). Een dubbele tik wordt een wxEVT_LEFT_DCLICK (OpenCPN opent
// dan o.a. de AIS-info), lang indrukken een wxEVT_RIGHT_DOWN (OpenCPN opent het
// contextmenu). Uitzetten met env OCPNIOS_NO_TAPGESTURES.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/event.h>
#include <wx/log.h>
#include <wx/window.h>

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPointer>
#include <QTouchEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Afstanden in punten (logische pixels). Factor per gebaar begrensd; OpenCPN begrenst
// de schaal daarnaast zelf (g_maxzoomin).
constexpr double kMinStartDist = 1.0;
constexpr double kMinFactor = 0.05;
constexpr double kMaxFactor = 20.0;
// Tikgebaren (punten en milliseconden), in de buurt van de iOS-standaard.
constexpr double kTapSlop = 12.0;         // max. verschuiving binnen één tik
constexpr double kDoubleTapDist = 30.0;   // max. afstand tussen twee tikken
constexpr qint64 kDoubleTapMs = 450;      // max. tijd tussen loslaten en volgende tik
constexpr qint64 kLongPressMs = 600;      // min. duur van lang indrukken

// Een wx-muisevent op de plek van een aanraking, via de wachtrij zodat het na de
// gewone (door Qt gesynthetiseerde) muis-events van dezelfde aanraking komt.
// Het event gaat naar het diepste wx-venster onder de vinger. Let op: de kaart
// (ChartCanvas) koppelt met gebaar-events geen dubbelklik in zijn event-tabel; de
// OpenCPN-patch koppelt wxEVT_LEFT_DCLICK daarom apart (chcanv.cpp).
void PostMouse(QWidget* target, const QPointF& global, wxEventType type) {
  QWidget* at = QApplication::widgetAt(global.toPoint());
  if (!at) at = target;
  wxWindow* win = nullptr;
  for (QWidget* w = at; w && !win; w = w->parentWidget())
    win = wxWindow::QtRetrieveWindowPointer(w);
  if (!win || !win->GetHandle()) return;
  const QPoint local = win->GetHandle()->mapFromGlobal(global.toPoint());
  wxLogMessage("OCPNIOS tikgebaar %s op %d,%d",
               type == wxEVT_LEFT_DCLICK ? "dubbel" : "lang", local.x(), local.y());
  wxMouseEvent* ev = new wxMouseEvent(type);
  ev->SetEventObject(win);
  ev->SetId(win->GetId());
  ev->m_x = local.x();
  ev->m_y = local.y();
  win->GetEventHandler()->QueueEvent(ev);
}

class PinchFilter : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    switch (ev->type()) {
      case QEvent::TouchBegin:
      case QEvent::TouchUpdate:
        if (obj->isWidgetType()) {
          auto* touch = static_cast<QTouchEvent*>(ev);
          TapTouch(static_cast<QWidget*>(obj), touch);
          HandleTouch(static_cast<QWidget*>(obj), touch);
        }
        break;
      case QEvent::TouchEnd:
      case QEvent::TouchCancel:
        // Het einde kan bij het QWindow aankomen in plaats van bij een widget; daarom
        // hier op elk object het knijpen afsluiten.
        if (m_active) EndPinch();
        TapEnd(ev->type() == QEvent::TouchCancel);
        break;
      case QEvent::MouseButtonRelease:
        // Vangnet: geen vinger meer op het scherm betekent geen knijpen meer.
        if (m_active) EndPinch();
        break;
      case QEvent::MouseMove:
        if (m_tapDown) {
          const QPointF g = static_cast<QMouseEvent*>(ev)->globalPosition();
          if (QLineF(g, m_tapStart).length() > kTapSlop) m_tapMoved = true;
        }
        // Tijdens het knijpen geen verschuiven met de eerste vinger.
        if (m_active) return true;
        break;
      default:
        break;
    }
    return QObject::eventFilter(obj, ev);
  }

 private:
  // --- dubbele tik en lang indrukken -------------------------------------------------
  void TapTouch(QWidget* widget, QTouchEvent* touch) {
    if (!m_tapsEnabled) return;
    const auto& pts = touch->points();
    if (pts.size() >= 2) {  // tweede vinger: geen tikgebaar
      m_tapDown = false;
      m_tapMulti = true;
      m_lastTapValid = false;
      return;
    }
    if (pts.isEmpty()) return;
    const QPointF g = pts[0].globalPosition();
    if (touch->type() == QEvent::TouchBegin) {
      m_tapDown = true;
      m_tapMoved = false;
      m_tapMulti = false;
      m_tapStart = g;
      m_tapTarget = widget;
      m_tapTimer.start();
    } else if (m_tapDown && QLineF(g, m_tapStart).length() > kTapSlop) {
      m_tapMoved = true;
    }
  }

  void TapEnd(bool cancelled) {
    if (!m_tapDown) return;
    m_tapDown = false;
    if (cancelled || m_tapMoved || m_tapMulti || !m_tapTarget) {
      m_lastTapValid = false;
      return;
    }
    const qint64 held = m_tapTimer.elapsed();
    if (held >= kLongPressMs) {
      m_lastTapValid = false;
      PostMouse(m_tapTarget, m_tapStart, wxEVT_RIGHT_DOWN);
      return;
    }
    // Testhaak: env OCPNIOS_DOUBLETAP_MS verlengt de tijd tussen twee tikken (de
    // simulatortool tikt te langzaam voor een echte dubbele tik).
    static const qint64 doubleTapMs = [] {
      const char* v = std::getenv("OCPNIOS_DOUBLETAP_MS");
      return v ? std::atoll(v) : kDoubleTapMs;
    }();
    if (m_lastTapValid && m_sinceLastTap.elapsed() <= doubleTapMs &&
        QLineF(m_tapStart, m_lastTapPos).length() <= kDoubleTapDist) {
      m_lastTapValid = false;
      PostMouse(m_tapTarget, m_tapStart, wxEVT_LEFT_DCLICK);
      return;
    }
    m_lastTapValid = true;
    m_lastTapPos = m_tapStart;
    m_sinceLastTap.start();
  }

  void HandleTouch(QWidget* widget, QTouchEvent* touch) {
    const auto& pts = touch->points();
    const bool ending = touch->type() == QEvent::TouchEnd ||
                        touch->type() == QEvent::TouchCancel;

    if (!ending && pts.size() >= 2) {
      const QPointF a = pts[0].globalPosition();
      const QPointF b = pts[1].globalPosition();
      const double dist = QLineF(a, b).length();
      const QPointF center = (a + b) / 2.0;
      if (!m_active) {
        // Pas beginnen als de vingers echt uit elkaar staan. Met een begin-afstand
        // van een paar punten (twee vingers die tegelijk neerkomen) werd de factor
        // enorm en raakte de kaart op de iPhone in een onbruikbare stand
        // ("OverZoom", wxRound-asserts, leeg scherm; schermopname 8 nov 2026).
        if (dist < kMinStartDist) return;
        m_active = true;
        m_target = widget;
        m_startDist = dist;
        Send(center, 1.0, true, false);
      } else {
        double factor = dist / m_startDist;
        if (!std::isfinite(factor)) return;
        factor = std::clamp(factor, kMinFactor, kMaxFactor);
        Send(center, factor, false, false);
      }
      return;
    }

    if (m_active && (ending || pts.size() < 2)) EndPinch();
  }

  void EndPinch() {
    m_active = false;
    Send(m_lastCenter, m_lastFactor, false, true);
    m_target.clear();
  }

  void Send(const QPointF& globalCenter, double factor, bool start, bool end) {
    m_lastCenter = globalCenter;
    m_lastFactor = factor;
    if (!m_target) return;
    // De aanraking kan op een kindwidget vallen (bv. de viewport van een scrollarea):
    // loop omhoog naar het eerste widget dat bij een wx-venster hoort.
    wxWindow* win = nullptr;
    for (QWidget* w = m_target; w && !win; w = w->parentWidget())
      win = wxWindow::QtRetrieveWindowPointer(w);
    if (!win) return;
    const QPoint local = m_target->mapFromGlobal(globalCenter.toPoint());
    // Vaste ID 0x51A7: ChartCanvas::OnZoom (OpenCPN-patch) gebruikt op iOS alleen deze
    // events, niet de eigen zoom-events van wxQt.
    wxZoomGestureEvent evt(0x51A7);
    evt.SetEventObject(win);
    evt.SetPosition(wxPoint(local.x(), local.y()));
    evt.SetZoomFactor(factor);
    if (start) evt.SetGestureStart();
    if (end) evt.SetGestureEnd();
    win->GetEventHandler()->ProcessEvent(evt);
  }

  // Tikgebaren
  bool m_tapsEnabled = std::getenv("OCPNIOS_NO_TAPGESTURES") == nullptr;
  bool m_tapDown = false;
  bool m_tapMoved = false;
  bool m_tapMulti = false;
  QPointF m_tapStart;
  QPointer<QWidget> m_tapTarget;
  QElapsedTimer m_tapTimer;
  bool m_lastTapValid = false;
  QPointF m_lastTapPos;
  QElapsedTimer m_sinceLastTap;

  bool m_active = false;
  QPointer<QWidget> m_target;
  double m_startDist = 1.0;
  QPointF m_lastCenter;
  double m_lastFactor = 1.0;
};

void InstallPinch() {
  if (std::getenv("OCPNIOS_NO_PINCH")) return;
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) return;
  app->installEventFilter(new PinchFilter(app));
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(InstallPinch)
