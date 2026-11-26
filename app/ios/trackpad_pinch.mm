// Pinch to zoom with a trackpad (iPad with Magic Keyboard or trackpad, iPad app on a
// Mac).
//
// A trackpad pinch produces no touches: iPadOS delivers it only to gesture
// recognizers. Qt adds none for it, so nothing reached the chart. This adds one
// UIPinchGestureRecognizer to the app window that only accepts indirect pointer
// input (allowedTouchTypes), so finger pinches keep going through Qt and
// pinch_zoom.cpp. It sends the same wxZoomGestureEvents (ID 0x51A7) as
// pinch_zoom.cpp: begin, updates with the factor since the start, end.
// Disable with env OCPNIOS_NO_TRACKPAD_PINCH.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <wx/event.h>
#include <wx/log.h>
#include <wx/window.h>

#include <QApplication>
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

QPointer<QWidget> g_target;

void SendZoom(const QPoint& global, double factor, bool start, bool end) {
  if (start) {
    g_target = QApplication::widgetAt(global);
  }
  if (!g_target) return;
  wxWindow* win = nullptr;
  for (QWidget* w = g_target; w && !win; w = w->parentWidget())
    win = wxWindow::QtRetrieveWindowPointer(w);
  if (!win) return;
  const QPoint local = g_target->mapFromGlobal(global);
  wxZoomGestureEvent evt(0x51A7);
  evt.SetEventObject(win);
  evt.SetPosition(wxPoint(local.x(), local.y()));
  evt.SetZoomFactor(factor);
  if (start) evt.SetGestureStart();
  if (end) evt.SetGestureEnd();
  win->GetEventHandler()->ProcessEvent(evt);
  if (end) g_target.clear();
}

}  // namespace

@interface OcpnTrackpadPinch : NSObject <UIGestureRecognizerDelegate>
@property(nonatomic) CGPoint center;
@property(nonatomic) BOOL active;
@end

@implementation OcpnTrackpadPinch

- (void)pinch:(UIPinchGestureRecognizer*)r {
  // Qt coordinates are points relative to the app window (UIWindow).
  const CGPoint p = [r locationInView:r.view];
  if (r.state == UIGestureRecognizerStateBegan) {
    self.center = p;
    self.active = YES;
  }
  if (!self.active) return;
  const QPoint g((int)std::lround(self.center.x), (int)std::lround(self.center.y));
  double f = r.scale;
  if (!std::isfinite(f) || f <= 0) f = 1.0;
  f = std::clamp(f, 0.05, 20.0);
  switch (r.state) {
    case UIGestureRecognizerStateBegan:
      SendZoom(g, 1.0, true, false);
      break;
    case UIGestureRecognizerStateChanged:
      SendZoom(g, f, false, false);
      break;
    default:  // ended, cancelled, failed
      self.active = NO;
      SendZoom(g, f, false, true);
      break;
  }
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)a
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)b {
  return YES;
}

@end

namespace {

OcpnTrackpadPinch* g_handler = nil;

// The app window exists only after Qt shows the first window: try again until then.
void Attach() {
  for (UIScene* sc in [UIApplication sharedApplication].connectedScenes) {
    if (![sc isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* w in ((UIWindowScene*)sc).windows) {
      if (w.windowLevel != UIWindowLevelNormal) continue;
      BOOL has = NO;
      for (UIGestureRecognizer* g in w.gestureRecognizers)
        if (g.delegate == g_handler) has = YES;
      if (has) return;
      auto* r = [[UIPinchGestureRecognizer alloc] initWithTarget:g_handler
                                                          action:@selector(pinch:)];
      r.allowedTouchTypes = @[ @(UITouchTypeIndirectPointer) ];
      r.cancelsTouchesInView = NO;
      r.delaysTouchesBegan = NO;
      r.delaysTouchesEnded = NO;
      r.delegate = g_handler;
      [w addGestureRecognizer:r];
      [r release];
      wxLogMessage("OCPNIOS trackpad pinch recognizer added");
      return;
    }
  }
  QTimer::singleShot(500, [] { Attach(); });
}

void InstallTrackpadPinch() {
  if (std::getenv("OCPNIOS_NO_TRACKPAD_PINCH")) return;
  g_handler = [[OcpnTrackpadPinch alloc] init];
  // No timers yet at this point (crash in CFRunLoopAddTimer): queue it instead.
  QMetaObject::invokeMethod(QCoreApplication::instance(), [] { Attach(); },
                            Qt::QueuedConnection);
}

}  // namespace

Q_COREAPP_STARTUP_FUNCTION(InstallTrackpadPinch)
