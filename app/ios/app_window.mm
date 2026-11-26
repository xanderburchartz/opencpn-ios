// Maat van het eigen app-venster op het scherm.
//
// In de venstermodus van iPadOS 26 (app niet schermvullend) meldt Qt als scherm nog
// het hele beeldscherm, terwijl de app maar een deel heeft. Vensters die op het scherm
// werden gecentreerd, vielen zo rechts en onder buiten het app-venster ("Safe Restart",
// Xander 10 nov 2026). Qt-coördinaten zijn relatief aan het app-venster (UIWindow).
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <QRect>

QRect ocpnios_AppWindowRect() {
  UIWindowScene* best = nil;
  for (UIScene* sc in [UIApplication sharedApplication].connectedScenes) {
    if (![sc isKindOfClass:[UIWindowScene class]]) continue;
    if (!best || sc.activationState == UISceneActivationStateForegroundActive)
      best = (UIWindowScene*)sc;
  }
  if (!best) return QRect();
  // Het venster van Qt: het grootste normale venster (de bezig-melding van
  // startup_busy.mm staat op een hoger niveau en telt niet mee).
  UIWindow* win = nil;
  for (UIWindow* w in best.windows) {
    if (w.windowLevel != UIWindowLevelNormal || w.hidden) continue;
    if (!win || w.bounds.size.width * w.bounds.size.height >
                    win.bounds.size.width * win.bounds.size.height)
      win = w;
  }
  const CGRect b = win ? win.bounds : best.coordinateSpace.bounds;
  const CGRect s = best.screen.bounds;
  // Schermvullend: geen beperking (de veilige zones regelt window_fit.cpp zelf).
  if (b.size.width >= s.size.width - 1 && b.size.height >= s.size.height - 1)
    return QRect(0, 0, (int)b.size.width, (int)b.size.height);
  // Venster: de strook met de vensterknopjes (●●●) bovenin en de afgeronde hoeken
  // vrijhouden (safeAreaInsets van het venster).
  const UIEdgeInsets in = win ? win.safeAreaInsets : UIEdgeInsetsZero;
  return QRect((int)in.left, (int)in.top, (int)(b.size.width - in.left - in.right),
               (int)(b.size.height - in.top - in.bottom));
}

// Draait de app in een venster (niet schermvullend)?
bool ocpnios_AppIsWindowed() {
  const QRect r = ocpnios_AppWindowRect();
  UIScreen* scr = [UIScreen mainScreen];
  if (!r.isValid() || !scr) return false;
  return r.x() + r.width() < scr.bounds.size.width - 1 ||
         r.y() + r.height() < scr.bounds.size.height - 1 || r.y() > 0;
}

// iPad app on a Mac ("Designed for iPad"): the window has a real macOS title bar with
// its own buttons, so no strip for the iPadOS window buttons is needed inside the app.
// Without this the app kept an empty grey strip of 66 pt at the top (Mac, 26 Nov 2026).
bool ocpnios_AppIsOnMac() {
  if (@available(iOS 14.0, *)) return [NSProcessInfo processInfo].isiOSAppOnMac;
  return false;
}
