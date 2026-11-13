// Kleuren van menubalk, statusregel en de iOS-statusbalk volgens het
// kleurenschema van OpenCPN (dag/schemer/nacht).
//
// OpenCPN kleurt bij een ander schema de kaart, de werkbalk en dialogen, maar niet
// de menubalk en statusregel: die tekent Qt zelf, en op iOS bleven ze (en de
// iOS-statusbalk met klok en batterij) fel wit in de nachtstand (8 nov 2026).
// SPDX-License-Identifier: GPL-2.0-or-later

#import <UIKit/UIKit.h>

#include <QApplication>
#include <QMainWindow>
#include <QScrollArea>
#include <QString>
#include <QWidget>

#include "color_scheme.h"

void ocpnios_RefreshSheetLooks();  // fit_width.cpp

void OcpnIosApplyUiColors(QWidget* mainWindow, int bgR, int bgG, int bgB, int fgR,
                          int fgG, int fgB, bool dark) {
  if (!mainWindow) return;
  const QString bg = QString("rgb(%1,%2,%3)").arg(bgR).arg(bgG).arg(bgB);
  const QString fg = QString("rgb(%1,%2,%3)").arg(fgR).arg(fgG).arg(fgB);
  // Op het hoofdvenster; Qt laat de regels doorwerken naar menubalk, statusregel en
  // de uitklapmenu's. De eigen stylesheet van de menubalk (padding voor de veilige
  // zone, window_fit.cpp) blijft daarnaast gelden.
  mainWindow->setStyleSheet(
      QString("QMainWindow { background-color: %1; }"
              " QMenuBar { background-color: %1; color: %2; }"
              " QMenuBar::item { background: transparent; color: %2; }"
              " QMenu { background-color: %1; color: %2; }"
              " QStatusBar { background-color: %1; }"
              " QStatusBar QLabel { color: %2; }")
          .arg(bg, fg));

  // Voor touch_ui.cpp: schuifvlakken alleen zelf laten vullen bij een donker
  // schema. Bestaande vlakken hier meteen bijwerken.
  QCoreApplication::instance()->setProperty("ocpnios_dark", dark);
  for (QWidget* top : QApplication::topLevelWidgets()) {
    for (QScrollArea* sa : top->findChildren<QScrollArea*>()) {
      sa->setAutoFillBackground(dark);
      if (sa->viewport()) sa->viewport()->setAutoFillBackground(dark);
      sa->update();
    }
  }
  // Open bladen (Stuurautomaat-paneel, Routebeheer) meteen in de nieuwe kleuren; ze
  // bleven licht tot ze opnieuw opengingen (iPad-simulator, 10 nov 2026).
  ocpnios_RefreshSheetLooks();

  // iOS-statusbalk (klok, batterij): lichte tekst bij een donkere achtergrond. Via
  // de interface-stijl van de vensters van de app; de Qt-widgets houden hun eigen
  // lichte palet (window_fit.cpp zet Qt op Light).
  const UIUserInterfaceStyle style =
      dark ? UIUserInterfaceStyleDark : UIUserInterfaceStyleLight;
  for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]]) continue;
    for (UIWindow* w in ((UIWindowScene*)scene).windows) {
      w.overrideUserInterfaceStyle = style;
      [w.rootViewController setNeedsStatusBarAppearanceUpdate];
    }
  }
}
