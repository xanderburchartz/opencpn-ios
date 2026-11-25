// Laag 1 van de iOS-bediening (9 nov 2026): gedrag en maat voor de hele app, via Qt.
//
// 1. Vingerscrollen met naloop (QScroller) in alle scrollgebieden:
//    - lijsten, tabellen, bomen, tekstvakken: QScroller::grabGesture (Qt doet tikken en
//      selecteren zelf goed);
//    - wx-schuifvensters (instellingenpagina's e.d., QScrollArea met echte knoppen erin):
//      eigen afhandeling, want een sleep die op een knop begint, komt nooit bij het
//      scrollgebied aan. Na 10 pt beweging wordt de druk op het kind geannuleerd en gaat
//      de beweging naar de QScroller van het scrollgebied.
//    De kaart is geen scrollgebied (ChartCanvas zonder wxVSCROLL) en blijft ongemoeid.
// 2. Aanraakmaten en iOS-achtige vormgeving via een app-stylesheet. Kleuren via palette(),
//    zodat OpenCPN's eigen kleuren (dag/schemer/nacht, DimeControl) blijven gelden.
//    Uitzetten: OCPNIOS_NO_TOUCH_STYLE=1 (stijl), OCPNIOS_NO_KINETIC=1 (scrollen).
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <wx/scrolwin.h>
#include <wx/window.h>
#include <QTabBar>
#include <QKeyEvent>
#include <QScrollerProperties>
#include <QTextEdit>
#include <QTimer>
#include <QElapsedTimer>
#include <cmath>
#include <vector>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <QColor>

namespace {

void TuneScroller(QScroller* s) {
  QScrollerProperties p = s->scrollerProperties();
  // iOS-achtig: kleine drempel, naloop, geen overschieten buiten de randen (wx
  // verwerkt negatieve schuifposities niet netjes).
  p.setScrollMetric(QScrollerProperties::DragStartDistance, 0.002);
  p.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.15);
  // Rustiger dan Qt's standaard (Xander, video 9 nov 2026: in de OpenGL-opties schoot
  // de lijst zo ver door dat een schakelaar moeilijk te raken was): sneller afremmen,
  // lagere topsnelheid, en geen versnelling bij herhaald vegen (standaard tot 3x).
  // Tweede ronde (Xander, 19:50: nog te snel): nog sneller afremmen en lager maximum.
  p.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.6);
  p.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.15);
  p.setScrollMetric(QScrollerProperties::AcceleratingFlickSpeedupFactor, 1.0);
  p.setScrollMetric(QScrollerProperties::AcceleratingFlickMaximumTime, 0.0);
  QVariant off = QVariant::fromValue<QScrollerProperties::OvershootPolicy>(
      QScrollerProperties::OvershootAlwaysOff);
  p.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, off);
  p.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, off);
  s->setScrollerProperties(p);
}

bool CanScroll(QAbstractScrollArea* sa) {
  return (sa->verticalScrollBar() && sa->verticalScrollBar()->maximum() > 0) ||
         (sa->horizontalScrollBar() && sa->horizontalScrollBar()->maximum() > 0);
}

// Dichtstbijzijnde wx-schuifvenster (QScrollArea) waarvan w in de viewport zit.
QAbstractScrollArea* ScrollAreaFor(QWidget* w) {
  for (QWidget* p = w; p; p = p->parentWidget()) {
    if (p->isWindow() && p != w) return nullptr;  // niet buiten het eigen venster zoeken
    auto* sa = qobject_cast<QScrollArea*>(p);
    if (sa && sa != w && CanScroll(sa) && sa->viewport()->isAncestorOf(w)) return sa;
    if (sa && sa->viewport() == w && CanScroll(sa)) return sa;
  }
  return nullptr;
}

QString TouchStyleSheet();

class KineticFilter : public QObject {
 public:
  KineticFilter(QObject* parent, bool style, bool kinetic)
      : QObject(parent), m_style(style), m_kinetic(kinetic) {}

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override {
    // Stylesheet vóór het eerste widget opgemaakt wordt: wx rekent knopmaten bij het
    // aanmaken uit (sizeHint); later gezet viel de tekst buiten de knop (9 nov 2026).
    if (m_style && ev->type() == QEvent::Polish) {
      m_style = false;
      if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        app->setStyleSheet(app->styleSheet() + TouchStyleSheet());
    }
    // Hardwaretoetsenbord: Enter/Escape sluit een dialoog al bij het indrukken. iOS
    // levert het loslaten daarna aan het venster dat dan al weg is, en Qt crasht
    // (QUIView pressesEnded -> QPlatformWindow::window(), simulator 9 nov 2026).
    // Daarom bij een dialoog het indrukken pas na het loslaten afhandelen.
    if ((ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) &&
        obj->isWidgetType()) {
      auto* ke = static_cast<QKeyEvent*>(ev);
      const int k = ke->key();
      QWidget* win = static_cast<QWidget*>(obj)->window();
      if ((k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape) && win &&
          win->windowType() == Qt::Dialog) {  // eigen (geposte) events zijn niet spontaan
        if (ke->spontaneous() && ev->type() == QEvent::KeyPress) return true;
        if (ke->spontaneous() && ev->type() == QEvent::KeyRelease && !ke->isAutoRepeat()) {
          auto* press = new QKeyEvent(QEvent::KeyPress, k, ke->modifiers(), ke->text());
          QCoreApplication::postEvent(obj, press);
          return true;
        }
      }
    }
    if (!m_kinetic) return QObject::eventFilter(obj, ev);
    switch (ev->type()) {
      case QEvent::Polish:
      case QEvent::Show:
        if (auto* w = qobject_cast<QWidget*>(obj)) Prepare(w);
        break;
      case QEvent::MouseButtonPress:
        return OnPress(obj, static_cast<QMouseEvent*>(ev));
      case QEvent::MouseMove:
        return OnMove(obj, static_cast<QMouseEvent*>(ev));
      case QEvent::MouseButtonRelease:
        return OnRelease(obj, static_cast<QMouseEvent*>(ev));
      default:
        break;
    }
    return QObject::eventFilter(obj, ev);
  }

 private:
  void Prepare(QWidget* w) {
    // wx-schuifvensters (QScrollArea, bv. de pagina's van Instellingen) vullen hun
    // achtergrond niet zelf; dan schijnt Qt's lichte standaardachtergrond door,
    // ook in de nachtstand, terwijl OpenCPN hun palet al donker had gezet (gemeten
    // per widget met een pixelopname, 9 nov 2026). Zelf laten vullen met hun palet.
    // Alleen bij een donker schema: overdag geeft OpenCPN die vlakken een zwart
    // palet dat dan juist niet getoond moet worden (gemeten: zwarte pagina).
    if (auto* sa = qobject_cast<QScrollArea*>(w)) {
      const bool dark =
          QCoreApplication::instance()->property("ocpnios_dark").toBool();
      sa->setAutoFillBackground(dark);
      if (sa->viewport()) sa->viewport()->setAutoFillBackground(dark);
    }
    // Tabbladen: namen niet inkorten (met inkorten werd het bij Kaarten "Kaa...",
    // "Ka..."). Passen de tabs niet, dan schuiven met de pijltjes; die zijn via de
    // stylesheet groter gemaakt (QTabBar QToolButton).
    if (auto* tb = qobject_cast<QTabBar*>(w)) {
      if (!tb->property("ocpnios_tabs").toBool()) {
        tb->setProperty("ocpnios_tabs", true);
        tb->setUsesScrollButtons(true);
        tb->setElideMode(Qt::ElideNone);
        tb->setExpanding(true);
      }
      return;
    }
    // Lijsten en tekst: standaard QScroller.
    if (qobject_cast<QAbstractItemView*>(w) || qobject_cast<QTextEdit*>(w) ||
        qobject_cast<QPlainTextEdit*>(w)) {
      auto* sa = static_cast<QAbstractScrollArea*>(w);
      // Uitgevouwen lijst (fit_width.cpp, FlattenInnerScroll): schuift niet zelf, de
      // veeg is voor het blad eromheen.
      if (sa->viewport()->property("ocpnios_flat").toBool()) return;
      if (!sa->viewport()->property("ocpnios_kinetic").toBool()) {
        sa->viewport()->setProperty("ocpnios_kinetic", true);
        QScroller::grabGesture(sa->viewport(), QScroller::LeftMouseButtonGesture);
        TuneScroller(QScroller::scroller(sa->viewport()));
        if (auto* iv = qobject_cast<QAbstractItemView*>(w)) {
          iv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
          iv->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        }
      }
    }
  }

  // wx-schuifvensters rekenen hun schuifbalk in "eenheden" van meerdere pixels
  // (OpenCPN-instellingen: 5 tot 15). QScroller gaat uit van pixels; de pagina
  // liep daardoor sneller dan de vinger en verspringend (Xander, 9 nov 2026).
  // Daarom bij de eerste veeg 1 pixel per eenheid, zoals OpenCPN op Android.
  // Schuifpositie in pixels (onafhankelijk van de wx-eenheid op dat moment).
  static QPoint PixelPos(QWidget* area) {
    wxWindow* ww = nullptr;
    for (QWidget* q = area; q && !ww; q = q->parentWidget())
      ww = wxWindow::QtRetrieveWindowPointer(q);
    auto* sh = dynamic_cast<wxScrollHelperBase*>(ww);
    if (!sh) return QPoint(-1, -1);
    int ux = 0, uy = 0, vx = 0, vy = 0;
    sh->GetScrollPixelsPerUnit(&ux, &uy);
    sh->GetViewStart(&vx, &vy);
    return QPoint(ux ? vx * ux : -1, uy ? vy * uy : -1);
  }

  static void PixelScrollUnits(QWidget* area) {
    wxWindow* ww = nullptr;
    for (QWidget* q = area; q && !ww; q = q->parentWidget())
      ww = wxWindow::QtRetrieveWindowPointer(q);
    auto* sh = dynamic_cast<wxScrollHelperBase*>(ww);
    if (!sh) return;
    int ux = 0, uy = 0;
    sh->GetScrollPixelsPerUnit(&ux, &uy);
    if (ux <= 1 && uy <= 1) return;
    int vx = 0, vy = 0;
    sh->GetViewStart(&vx, &vy);
    sh->SetScrollRate(ux ? 1 : 0, uy ? 1 : 0);
    sh->Scroll(vx * ux, vy * uy);
  }

  bool OnPress(QObject* obj, QMouseEvent* e) {
    if (m_area || e->button() != Qt::LeftButton) return false;
    StopCoast();
    auto* w = qobject_cast<QWidget*>(obj);
    if (!w) return false;
    // Lijsten/tekst regelt hun eigen QScroller.
    for (QWidget* p = w; p; p = p->parentWidget())
      if (p->property("ocpnios_kinetic").toBool()) return false;
    // A drag grip on the right of a list (priorities, connections_ios.cpp):
    // the finger moves the row, not the page.
    for (QWidget* p = w; p; p = p->parentWidget()) {
      const int grip = p->property("ocpnios_grip_right").toInt();
      if (grip > 0 &&
          p->mapFromGlobal(e->globalPosition().toPoint()).x() >= p->width() - grip)
        return false;
    }
    QAbstractScrollArea* sa = ScrollAreaFor(w);
    if (!sa) return false;
    m_area = sa;
    m_child = w;
    m_start = e->globalPosition();
    // Positie bij het indrukken: wx schuift bij focus op een element zelf de
    // pagina om het element in beeld te brengen (gemeten: 100 punten sprong
    // vóór het slepen begon). Bij slepen terug naar deze positie.
    m_pressPos = PixelPos(sa);
    m_scrolling = false;
    return false;  // druk gaat gewoon door naar het kind
  }

  bool OnMove(QObject* obj, QMouseEvent* e) {
    if (!m_area || !(e->buttons() & Qt::LeftButton)) return false;
    QPointF d = e->globalPosition() - m_start;
    if (!m_scrolling) {
      if (std::abs(d.x()) < 10 && std::abs(d.y()) < 10) return false;
      // Rows that are swiped sideways (connections list, connections_ios.cpp):
      // a horizontal move is for the row, not for the page.
      if (std::abs(d.x()) > std::abs(d.y())) {
        for (QWidget* p = m_child; p; p = p->parentWidget())
          if (p->property("ocpnios_hswipe").toBool()) {
            m_area = nullptr;
            m_child = nullptr;
            return false;
          }
      }
      m_scrolling = true;
      // Druk op het kind annuleren: loslaten ver buiten het kind, dus geen klik.
      if (m_child) {
        QPointF far(-100000, -100000);
        QMouseEvent rel(QEvent::MouseButtonRelease, far, far, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
        // Gaat zelf ook door dit filter: niet als echte loslating behandelen
        // (anders was m_area daarna leeg; crash 9 nov 2026).
        m_synthetic = true;
        QCoreApplication::sendEvent(m_child, &rel);
        m_synthetic = false;
      }
      if (!m_area) return false;
      PixelScrollUnits(m_area);
      StopCoast();
      if (m_pressPos.y() >= 0 && m_area->verticalScrollBar())
        m_area->verticalScrollBar()->setValue(m_pressPos.y());
      if (m_pressPos.x() >= 0 && m_area->horizontalScrollBar())
        m_area->horizontalScrollBar()->setValue(m_pressPos.x());
      // Vanaf hier volgt de pagina de vinger 1:1: startpunt = huidige vinger.
      m_start = e->globalPosition();
      m_startX = m_area->horizontalScrollBar() ? m_area->horizontalScrollBar()->value() : 0;
      m_startY = m_area->verticalScrollBar() ? m_area->verticalScrollBar()->value() : 0;
      m_samples.clear();
      d = QPointF(0, 0);
    }
    if (!m_area) return false;
    // Direct de schuifbalk zetten (1 eenheid = 1 pixel na PixelScrollUnits):
    // QScroller liet de pagina ~1,5x zo ver gaan als de vinger (gemeten).
    if (auto* sb = m_area->verticalScrollBar()) sb->setValue(m_startY - int(std::lround(d.y())));
    if (auto* sb = m_area->horizontalScrollBar()) sb->setValue(m_startX - int(std::lround(d.x())));
    m_samples.push_back({e->globalPosition(), m_clock.elapsed()});
    while (m_samples.size() > 6) m_samples.erase(m_samples.begin());
    return true;  // beweging niet meer naar het kind
  }

  bool OnRelease(QObject* obj, QMouseEvent* e) {
    if (m_synthetic || !m_area) return false;
    bool was = m_scrolling;
    if (was) StartCoast();
    m_area = nullptr;
    m_child = nullptr;
    m_scrolling = false;
    return was;  // na scrollen geen klik op het kind
  }

  // Naloop na loslaten, zoals iOS: snelheid uit de laatste ~100 ms van de vinger,
  // daarna exponentieel afremmen (tijdconstante ~0,33 s, iOS "normal").
  void StartCoast() {
    StopCoast();
    if (m_samples.size() < 2) return;
    const auto& b = m_samples.back();
    size_t k = 0;
    for (size_t i = 0; i < m_samples.size(); i++)
      if (b.t - m_samples[i].t <= 100) { k = i; break; }
    const auto& a = m_samples[k];
    const double dt = (b.t - a.t) / 1000.0;
    if (dt <= 0.01 || m_clock.elapsed() - b.t > 80) return;  // stilgehouden: geen naloop
    m_vel = -(b.p - a.p) / dt;  // pixels per seconde, richting van de inhoud
    if (std::hypot(m_vel.x(), m_vel.y()) < 150) return;
    m_coastArea = m_area;
    m_coastPos = QPointF(m_coastArea->horizontalScrollBar() ? m_coastArea->horizontalScrollBar()->value() : 0,
                         m_coastArea->verticalScrollBar() ? m_coastArea->verticalScrollBar()->value() : 0);
    m_coastLast = m_clock.elapsed();
    if (!m_coastTimer) {
      m_coastTimer = new QTimer(this);
      m_coastTimer->setInterval(16);
      connect(m_coastTimer, &QTimer::timeout, this, [this]() { CoastStep(); });
    }
    m_coastTimer->start();
  }
  void StopCoast() {
    if (m_coastTimer) m_coastTimer->stop();
  }
  void CoastStep() {
    if (!m_coastArea) { StopCoast(); return; }
    const qint64 now = m_clock.elapsed();
    const double dt = (now - m_coastLast) / 1000.0;
    m_coastLast = now;
    m_coastPos += m_vel * dt;
    m_vel *= std::exp(-dt / 0.33);
    bool moved = false;
    if (auto* sb = m_coastArea->verticalScrollBar()) {
      int v = int(std::lround(m_coastPos.y()));
      if (v < sb->minimum() || v > sb->maximum()) m_vel.setY(0);
      sb->setValue(v);
      moved = true;
    }
    if (auto* sb = m_coastArea->horizontalScrollBar()) {
      int v = int(std::lround(m_coastPos.x()));
      if (v < sb->minimum() || v > sb->maximum()) m_vel.setX(0);
      sb->setValue(v);
      moved = true;
    }
    if (!moved || std::hypot(m_vel.x(), m_vel.y()) < 20) StopCoast();
  }

  struct Sample { QPointF p; qint64 t; };
  std::vector<Sample> m_samples;
  QElapsedTimer m_clock = [] { QElapsedTimer t; t.start(); return t; }();
  int m_startX = 0, m_startY = 0;
  QPoint m_pressPos{-1, -1};
  QPointF m_vel, m_coastPos;
  qint64 m_coastLast = 0;
  QPointer<QAbstractScrollArea> m_coastArea;
  QTimer* m_coastTimer = nullptr;

  bool m_style;
  bool m_kinetic;
  bool m_synthetic = false;
  QPointer<QAbstractScrollArea> m_area;
  QPointer<QWidget> m_child;
  QPointF m_start;
  bool m_scrolling = false;
};

// Aanraakmaten (Apple: minstens 44 pt; hier iets kleiner waar OpenCPN veel rijen onder
// elkaar zet) en een rustige, afgeronde vormgeving. Schakelaars voor vinkjes.
// Geen min-height op knoppen e.d.: wx geeft sommige knoppen een vaste maat (Raymarine-
// paneel) en een minimum laat ze dan overlappen. Opvulling vergroot alleen de sizeHint.
QString TouchStyleSheet() {
  const QString ui = QCoreApplication::applicationDirPath() + "/SharedSupport/uidata/ios/";
  return QString(R"(
QPushButton {
  padding: 8px 4px;
  border: 1px solid palette(mid); border-radius: 9px;
  background-color: palette(button); color: palette(button-text);
}
QPushButton:pressed { background-color: palette(midlight); }
QPushButton:default { border: 2px solid palette(highlight); }
QPushButton:disabled { color: palette(mid); }
QPushButton[ocpnios_nav="true"] {
  border: none; background: transparent; color: #007aff; font-size: 18px; padding: 12px 14px;
}
QPushButton[ocpnios_nav="true"]:pressed { color: #66aaff; }
QPushButton[ocpnios_nav="true"]:disabled { color: palette(mid); }
QCheckBox { spacing: 10px; padding: 3px 0px; }
QCheckBox[ocpnios_row="true"] { padding: 9px 0px; }
QCheckBox::indicator { width: 46px; height: 28px; image: url(%1switch_off.svg); }
QCheckBox::indicator:checked { image: url(%1switch_on.svg); }
QCheckBox::indicator:disabled { image: url(%1switch_disabled.svg); }
QCheckBox::indicator:checked:disabled { image: url(%1switch_on_disabled.svg); }
QRadioButton { spacing: 10px; padding: 6px 0px; }
QRadioButton::indicator { width: 22px; height: 22px; }
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
  padding: 6px 4px;
  border: 1px solid palette(mid); border-radius: 8px;
}
QComboBox::drop-down {
  subcontrol-origin: padding; subcontrol-position: center right;
  width: 28px; border: none; background: transparent;
}
QComboBox::down-arrow { image: url(%1chevron_updown.svg); width: 10px; height: 16px; }
QComboBox::down-arrow:disabled { image: url(%1chevron_updown.svg); }
QComboBox[ocpnios_formvalue="true"] {
  border: none; background: transparent; color: #8e8e93;
}
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: #c8c8cc; }
QSlider { min-height: 32px; }
QSlider::groove:horizontal { height: 4px; background: #d1d1d6; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #007aff; border-radius: 2px; }
QSlider::handle:horizontal {
  width: 26px; height: 26px; margin: -11px 0px; border-radius: 13px;
  background: #ffffff; border: 1px solid #c7c7cc;
}
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { width: 0px; border: none; }
QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow { width: 0px; height: 0px; }
QComboBox QAbstractItemView::item { min-height: 40px; }
QAbstractItemView::item { min-height: 36px; }
QAbstractItemView::indicator { width: 24px; height: 24px; image: url(%1check_off.svg); }
QAbstractItemView::indicator:checked { image: url(%1check_on.svg); }
QAbstractItemView::indicator:disabled { image: url(%1check_disabled.svg); }
QTabBar::tab { padding: 8px 8px; }
QTabBar QToolButton { width: 34px; border: 1px solid palette(mid); border-radius: 6px; background-color: palette(button); }
QMenu::item { padding: 10px 24px 10px 16px; }
QAbstractScrollArea::corner { background: transparent; border: none; }
QScrollBar:vertical { width: 6px; background: transparent; margin: 2px; }
QScrollBar:horizontal { height: 6px; background: transparent; margin: 2px; }
QScrollBar::handle { background: rgba(128,128,128,140); border-radius: 3px; min-height: 30px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)")
      .arg(ui);
}

void InstallTouchUi() {
  QCoreApplication* app = QCoreApplication::instance();
  if (!app) return;
  bool style = !std::getenv("OCPNIOS_NO_TOUCH_STYLE");
  bool kinetic = !std::getenv("OCPNIOS_NO_KINETIC");
  if (style || kinetic) app->installEventFilter(new KineticFilter(app, style, kinetic));
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(InstallTouchUi)
