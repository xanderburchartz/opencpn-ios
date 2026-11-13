// iOS-actieblad (UIAlertController) voor menu's van plugins.
//
// WeatherRouting heeft een desktop-menubalk (Bestand, Positie, Routings, Weergave,
// Help). window_fit.cpp vervangt die door een knop "⋯" in de kopbalk; die opent hier
// een actieblad met de menu's, en daarna een tweede met de items van het gekozen menu
// (Xander, 11 nov 2026: "nogal desktop-achtig"). Een Qt-uitklapmenu reageerde op iOS niet
// op een tik op een item (simulator, 11 nov 2026). De keuze start de QAction van wx.
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace {

UIViewController* TopController() {
  UIWindow* key = nil;
  for (UIScene* sc in UIApplication.sharedApplication.connectedScenes) {
    if (![sc isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* w in ((UIWindowScene*)sc).windows)
      if (w.isKeyWindow) key = w;
    if (!key && ((UIWindowScene*)sc).windows.count) key = ((UIWindowScene*)sc).windows.firstObject;
  }
  UIViewController* vc = key.rootViewController;
  while (vc.presentedViewController) vc = vc.presentedViewController;
  return vc;
}

NSString* Plain(const QString& t) {
  QString s = t;
  s.remove('&');
  const int tab = s.indexOf('\t');  // sneltoets weg
  if (tab >= 0) s = s.left(tab);
  return s.trimmed().toNSString();
}

void Present(UIAlertController* ac, QWidget* anchor) {
  UIViewController* vc = TopController();
  if (!vc) return;
  // iPad: een actieblad hoort bij een plek op het scherm.
  if (ac.popoverPresentationController && anchor) {
    const QPoint g = anchor->mapToGlobal(QPoint(0, 0));
    ac.popoverPresentationController.sourceView = vc.view;
    ac.popoverPresentationController.sourceRect =
        CGRectMake(g.x(), g.y(), anchor->width(), anchor->height());
  }
  [vc presentViewController:ac animated:YES completion:nil];
}

void ShowMenu(QMenu* menu, QWidget* anchor, NSString* cancel) {
  emit menu->aboutToShow();  // wx werkt hier de staat van de items bij
  UIAlertController* ac = [UIAlertController alertControllerWithTitle:Plain(menu->title())
                                                              message:nil
                                                       preferredStyle:UIAlertControllerStyleActionSheet];
  for (QAction* a : menu->actions()) {
    if (!a->isVisible() || a->isSeparator()) continue;
    QPointer<QAction> pa(a);
    QPointer<QWidget> pw(anchor);
    if (QMenu* sub = a->menu()) {
      QPointer<QMenu> ps(sub);
      [ac addAction:[UIAlertAction actionWithTitle:[Plain(a->text()) stringByAppendingString:@" ›"]
                                             style:UIAlertActionStyleDefault
                                           handler:^(UIAlertAction*) {
                                             if (ps) ShowMenu(ps, pw, cancel);
                                           }]];
      continue;
    }
    NSString* title = Plain(a->text());
    if (a->isCheckable() && a->isChecked()) title = [@"✓ " stringByAppendingString:title];
    UIAlertAction* act = [UIAlertAction actionWithTitle:title
                                                  style:UIAlertActionStyleDefault
                                                handler:^(UIAlertAction*) {
                                                  // Pas na het sluiten van het blad starten,
                                                  // via een Qt-timer en niet in dit blok: een
                                                  // actie met een eigen venster (bijv. de
                                                  // bestandskiezer bij WR Openen) wacht in een
                                                  // eigen lus, en zolang dit blok van de
                                                  // hoofdwachtrij loopt kan iOS die kiezer
                                                  // niet tonen (12 nov 2026).
                                                  QTimer::singleShot(350, [pa] {
                                                    if (pa && pa->isEnabled()) pa->trigger();
                                                  });
                                                }];
    act.enabled = a->isEnabled();
    [ac addAction:act];
  }
  [ac addAction:[UIAlertAction actionWithTitle:cancel style:UIAlertActionStyleCancel handler:nil]];
  Present(ac, anchor);
}

}  // namespace

// Actieblad met de menu's van `menus` (de acties van een menubalk), daarna de items.
void ocpnios_ShowMenuSheet(const QList<QAction*>& menus, QWidget* anchor, const QString& cancel) {
  UIAlertController* ac =
      [UIAlertController alertControllerWithTitle:nil
                                          message:nil
                                   preferredStyle:UIAlertControllerStyleActionSheet];
  NSString* cancelText = cancel.toNSString();
  for (QAction* top : menus) {
    if (!top->isVisible() || !top->menu()) continue;
    QPointer<QMenu> pm(top->menu());
    QPointer<QWidget> pw(anchor);
    [ac addAction:[UIAlertAction actionWithTitle:Plain(top->text())
                                           style:UIAlertActionStyleDefault
                                         handler:^(UIAlertAction*) {
                                           if (pm) ShowMenu(pm, pw, cancelText);
                                         }]];
  }
  [ac addAction:[UIAlertAction actionWithTitle:cancelText style:UIAlertActionStyleCancel handler:nil]];
  Present(ac, anchor);
}

// Afgeronde hoeken voor een los Qt-venster (instrumentpaneel), op de laag van het UIView
// zodat iOS de rand vloeiend tekent, met een dunne rand zoals bij iOS-kaarten.
void ocpnios_RoundWindow(QWidget* w, double radius) {
  if (!w || !w->windowHandle()) return;
  UIView* v = reinterpret_cast<UIView*>(w->windowHandle()->winId());
  if (!v) return;
  v.layer.cornerRadius = radius;
  v.layer.masksToBounds = YES;
  v.layer.borderWidth = 0.5;
  v.layer.borderColor = [UIColor colorWithWhite:0.5 alpha:0.5].CGColor;
}
