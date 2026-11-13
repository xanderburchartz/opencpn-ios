// Omweg voor een crash in Qt 6.12.0 op iOS (apparaat, niet de simulator):
// bij het vernietigen van een widget vraagt QIOSPlatformAccessibility de rol op van een
// object dat al half afgebroken is (QAccessibleCache::sendObjectDestroyedEvent ->
// QAccessibleButton::role -> segfault). Opgelost in Qt 6.12.1. Tot dan geven we
// ObjectDestroyed niet door aan de iOS-laag; de rest wel. Zelfde omweg als in
// spikes/wx-minimal (fase 1). Uitzetten met env OCPNIOS_NO_A11Y_WORKAROUND.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAccessible>
#include <QtGlobal>

#include <cstdlib>

namespace {

QAccessible::UpdateHandler g_prevHandler = nullptr;

void FilterA11yUpdate(QAccessibleEvent *event) {
  if (event->type() == QAccessible::ObjectDestroyed) return;
  if (g_prevHandler) {
    g_prevHandler(event);
    return;
  }
  // Geen eerdere handler: Qt stuurt zelf door naar het platform als er geen handler is.
  QAccessible::installUpdateHandler(nullptr);
  QAccessible::updateAccessibility(event);
  QAccessible::installUpdateHandler(FilterA11yUpdate);
}

// Statische initialisatie: installUpdateHandler zet alleen een statische pointer en mag
// voor het aanmaken van QApplication.
struct Installer {
  Installer() {
#if QT_VERSION < QT_VERSION_CHECK(6, 12, 1)
    if (!std::getenv("OCPNIOS_NO_A11Y_WORKAROUND"))
      g_prevHandler = QAccessible::installUpdateHandler(FilterA11yUpdate);
#endif
  }
} g_installer;

}  // namespace
