// Hold back resize events of OpenGL widgets while the app is not active.
//
// TestFlight build 5 crashed with the app in the background (crash report 24 nov 2026,
// iPhone 17 Pro Max): iOS lays out the view again in the background (rotating or putting
// down the phone, app switcher snapshot), the chart canvas (a QOpenGLWidget) gets a resize
// event and recreates its framebuffers, but iOS allows no OpenGL in the background, the
// context cannot be made current and Qt dereferences a null QOpenGLContext
// (QOpenGLWidgetPrivate::recreateFbos > initializeOpenGLFunctions). Reproduced in the
// simulator with HOME and two rotations.
//
// While the application is not active, resize events for QOpenGLWidgets are dropped and
// the widget is remembered. When the app becomes active again, each remembered widget gets
// one resize event with its current size, so the framebuffers match the final geometry.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QOpenGLWidget>
#include <QPointer>
#include <QResizeEvent>
#include <vector>

namespace {

class GlBackgroundFilter : public QObject {
public:
  explicit GlBackgroundFilter(QObject* parent) : QObject(parent) {
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState st) {
              if (st == Qt::ApplicationActive) Replay();
            });
  }

  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::Resize || m_replaying) return false;
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) return false;
    auto* gl = qobject_cast<QOpenGLWidget*>(obj);
    if (!gl) return false;
    for (const auto& p : m_pending)
      if (p == gl) return true;
    m_pending.emplace_back(gl);
    return true;  // dropped; replayed in Replay()
  }

private:
  void Replay() {
    std::vector<QPointer<QOpenGLWidget>> pending;
    pending.swap(m_pending);
    m_replaying = true;
    for (const auto& p : pending) {
      if (!p) continue;
      QResizeEvent re(p->size(), QSize(-1, -1));
      QCoreApplication::sendEvent(p.data(), &re);
      p->update();
    }
    m_replaying = false;
  }

  std::vector<QPointer<QOpenGLWidget>> m_pending;
  bool m_replaying = false;
};

void InstallGlBackgroundFilter() {
  QCoreApplication* app = QCoreApplication::instance();
  if (!app || !qGuiApp) return;
  app->installEventFilter(new GlBackgroundFilter(app));
}

}  // namespace

// Runs once the QApplication exists (wxApp creates it in wxEntry).
Q_COREAPP_STARTUP_FUNCTION(InstallGlBackgroundFilter)
