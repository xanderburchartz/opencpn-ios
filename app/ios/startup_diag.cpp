#include <sys/stat.h>
// Diagnose voor het zwarte scherm bij een snelle start vanaf het beginscherm.
//
// Schrijft gedurende de eerste 20 s na het starten naar
// ~/Library/Application Support/opencpn/ocpnios-diag.log:
//  - alle Qt-meldingen (qWarning e.d.; die gaan anders alleen naar stderr, die we bij een
//    start vanaf het beginscherm niet zien);
//  - de toestand van de app (actief/inactief) en van elk topvenster: zichtbaar, maat,
//    "exposed", actief; elke 500 ms en bij Show/Hide/Expose/Resize.
// Daarna alleen nog de Qt-meldingen. Uitzetten met env OCPNIOS_NO_DIAG.
//
// Alleen voor eigen tests (besluit Xander 25 nov 2026, nu er externe TestFlight-testers komen):
// standaard uit. Aan zolang de map Documents/log/schermafbeeldingen bestaat (zonder bestand OFF
// erin), of met env OCPNIOS_DIAG=1. Zelfde schakelaar als de schermafbeeldingen
// (screen_capture.mm). Aanzetten op een toestel: in de Bestanden-app onder "OpenCPN › log" een
// map "schermafbeeldingen" maken; daarna de app opnieuw starten.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QMainWindow>
#include <QPointer>
#include <QPalette>
#include <QMouseEvent>
#include <QImage>
#include <QPixmap>

#include <cstdio>
#include <cstdlib>
#include <string>

// Schakelaar voor schermafbeeldingen en het diagnoselog, zie boven.
bool ocpnios_TestDiagnostics() {
  static int cached = -1;
  if (cached >= 0) return cached;
  cached = 0;
  if (std::getenv("OCPNIOS_DIAG")) return cached = 1;
  const char* home = std::getenv("HOME");
  if (!home) return cached;
  const std::string dir = std::string(home) + "/Documents/log/schermafbeeldingen";
  struct stat st;
  if (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return cached;
  if (stat((dir + "/OFF").c_str(), &st) == 0) return cached;
  return cached = 1;
}

namespace {

FILE* g_diag = nullptr;
QElapsedTimer g_clock;
QtMessageHandler g_prevHandler = nullptr;
bool g_detail = true;  // vensterdetails alleen in de eerste 20 s

void Diag(const QString& s) {
  if (!g_diag) return;
  const QString line = QStringLiteral("%1 +%2ms %3\n")
                           .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                           .arg(g_clock.isValid() ? g_clock.elapsed() : -1)
                           .arg(s);
  std::fputs(line.toUtf8().constData(), g_diag);
  std::fflush(g_diag);
}

void MessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  static const char* names[] = {"debug", "warning", "critical", "fatal", "info"};
  const char* n = (type >= 0 && type <= 4) ? names[type] : "?";
  Diag(QStringLiteral("qt-%1 %2").arg(n, msg));
  if (g_prevHandler) g_prevHandler(type, ctx, msg);
}

QString Rect(const QRect& r) {
  return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QString Describe(QWidget* w) {
  QWindow* h = w->windowHandle();
  return QStringLiteral("%1 '%2' vis=%3 geo=%4 exposed=%5 active=%6 state=%7")
      .arg(QString::fromLatin1(w->metaObject()->className()), w->windowTitle())
      .arg(w->isVisible())
      .arg(Rect(w->geometry()))
      .arg(h ? QString::number(h->isExposed()) : QStringLiteral("-"))
      .arg(w->isActiveWindow())
      .arg(int(w->windowState()));
}

void Snapshot(const char* why) {
  QStringList parts;
  for (QWidget* w : QApplication::topLevelWidgets()) {
    if (!w->isVisible() && !w->windowHandle()) continue;
    parts << Describe(w);
  }
  QScreen* s = QGuiApplication::primaryScreen();
  QWindow* fw = QGuiApplication::focusWindow();
  Diag(QStringLiteral("snapshot %1 appState=%2 screen=%3 focus=%4 | %5")
           .arg(why)
           .arg(int(QGuiApplication::applicationState()))
           .arg(s ? Rect(s->geometry()) : QStringLiteral("-"))
           .arg(fw ? fw->objectName() + "/" + QString::fromLatin1(fw->metaObject()->className())
                   : QStringLiteral("-"))
           .arg(parts.join(" | ")));
}

void StartTimers();

// Welke widget ligt midden in een venster, met zijn ouders: klasse, naam,
// autoFillBackground, paletkleur (Window) en of er een eigen stylesheet is.
void DumpChainAt(const QPoint& c, const QString& what);

void DumpCenterChain(QWidget* top) {
  if (!top || !top->isVisible()) return;
  DumpChainAt(top->mapToGlobal(QPoint(top->width() / 2, top->height() / 2)),
              QStringLiteral("midden ") + top->windowTitle());
}

void DumpChainAt(const QPoint& c, const QString& what) {
  // Not in the background: grabbing the chart canvas (a QOpenGLWidget) there crashed
  // on a null GL context (simulator, 24 nov 2026: tap, then HOME within the 1 s delay).
  if (QGuiApplication::applicationState() != Qt::ApplicationActive) return;
  QWidget* top = nullptr;
  QWidget* w = QApplication::widgetAt(c);
  QStringList chain;
  for (int i = 0; w && i < 12; w = w->parentWidget(), i++) {
    const QPoint lp = w->mapFromGlobal(c);
    // GL widgets are not read back: that needs a current GL context.
    const QColor px = w->inherits("QOpenGLWidget")
                          ? QColor()
                          : w->grab(QRect(lp, QSize(1, 1))).toImage().pixelColor(0, 0);
    chain << QStringLiteral("%1#%2 pix=%7 fill=%3 win=%4 base=%5 css=%6")
                 .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName())
                 .arg(w->autoFillBackground())
                 .arg(w->palette().color(w->backgroundRole()).name())
                 .arg(w->palette().color(QPalette::Base).name())
                 .arg(w->styleSheet().isEmpty() ? 0 : 1)
                 .arg(px.name());
    if (w->isWindow()) break;
  }
  (void)top;
  Diag(QStringLiteral("%1: %2").arg(what, chain.join(" < ")));
}

class DiagFilter : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (e->type() == QEvent::MouseButtonRelease) {
      auto* me = static_cast<QMouseEvent*>(e);
      const QPoint gp = me->globalPosition().toPoint();
      static qint64 last = -1000;
      if (g_clock.elapsed() - last > 300) {  // één keer per tik, niet per ouder
        last = g_clock.elapsed();
        QTimer::singleShot(1000, [gp]() { DumpChainAt(gp, QStringLiteral("tik")); });
      }
    }
    switch (e->type()) {
      case QEvent::Show:
      case QEvent::Hide:
      case QEvent::Resize:
      case QEvent::Expose:
      case QEvent::WindowActivate:
      case QEvent::WindowDeactivate: {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->isWindow()) {
          if (e->type() == QEvent::Show) {
            StartTimers();
            QPointer<QWidget> pw(w);
            QTimer::singleShot(1500, [pw]() { if (pw) DumpCenterChain(pw); });
          }
          if (g_detail || (e->type() == QEvent::Resize &&
                           qobject_cast<QMainWindow*>(w)))
            Diag(QStringLiteral("event %1 %2").arg(int(e->type())).arg(Describe(w)));
        } else if (auto* win = qobject_cast<QWindow*>(o)) {
          if (g_detail && e->type() == QEvent::Expose)
            Diag(QStringLiteral("expose QWindow %1 exposed=%2 geo=%3")
                     .arg(QString::fromLatin1(win->metaObject()->className()))
                     .arg(win->isExposed())
                     .arg(Rect(win->geometry())));
        }
        break;
      }
      default:
        break;
    }
    return false;
  }
};

void InstallDiag() {
  if (std::getenv("OCPNIOS_NO_DIAG")) return;
  if (!ocpnios_TestDiagnostics()) return;
  const char* home = std::getenv("HOME");
  if (!home) return;
  // In Documents/log, bij de andere logbestanden (13 nov 2026).
  const std::string dir = std::string(home) + "/Documents/log";
  mkdir((std::string(home) + "/Documents").c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  const std::string path = dir + "/ocpnios-diag.log";
  g_diag = std::fopen(path.c_str(), "w");  // per start opnieuw
  if (!g_diag) return;
  g_clock.start();
  Diag(QStringLiteral("start diag"));
  g_prevHandler = qInstallMessageHandler(MessageHandler);

  QCoreApplication* app = QCoreApplication::instance();
  // Geen QTimer of signaalkoppeling hier: deze functie draait in de constructor van
  // QApplication, voordat de iOS-runloop bestaat (QTimer::start gaf daar een crash in
  // CFRunLoopAddTimer, 9 nov 2026). Dat gebeurt bij het eerste Show-event.
  app->installEventFilter(new DiagFilter(app));
}

void StartTimers() {
  static bool started = false;
  if (started) return;
  started = true;
  QCoreApplication* app = QCoreApplication::instance();
  Diag(QStringLiteral("timers starten"));
  if (QScreen* scr = QGuiApplication::primaryScreen()) {
    QObject::connect(scr, &QScreen::geometryChanged, app, [](const QRect& r) {
      Diag(QStringLiteral("scherm geometry %1").arg(Rect(r)));
    });
    QObject::connect(scr, &QScreen::availableGeometryChanged, app,
                     [](const QRect& r) {
                       Diag(QStringLiteral("scherm available %1").arg(Rect(r)));
                     });
    QObject::connect(scr, &QScreen::orientationChanged, app,
                     [](Qt::ScreenOrientation o) {
                       Diag(QStringLiteral("scherm orientatie %1").arg(int(o)));
                     });
  }
  if (auto* gapp = qobject_cast<QGuiApplication*>(app)) {
    QObject::connect(gapp, &QGuiApplication::applicationStateChanged, gapp,
                     [](Qt::ApplicationState s) {
                       Diag(QStringLiteral("applicationState -> %1").arg(int(s)));
                       Snapshot("state");
                     });
  }
  auto* timer = new QTimer(app);
  QObject::connect(timer, &QTimer::timeout, app, [timer]() {
    Snapshot("tick");
    if (g_clock.elapsed() > 20000) {
      g_detail = false;
      timer->stop();
      Diag(QStringLiteral("einde vensterdetails"));
    }
  });
  timer->start(500);
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(InstallDiag)
