// Wachten tot iOS de app "actief" noemt voordat OpenCPN vensters maakt.
//
// Bij een start vanaf het beginscherm maakt OpenCPN zijn hoofdvenster terwijl iOS de app
// nog naar voren haalt (applicationState Inactive of Hidden). OpenGL ES mag dan niet;
// Qt meldt "OpenGL ES calls are not allowed while an application is backgrounded" en
// "Failed to create QRhi for QBackingStoreRhiSupport", en het venster blijft voorgoed
// zwart. Gemeten in de simulator en op de iPhone, 9 nov 2026. Zat verborgen als eerst de
// dialoog "Safe Restart" kwam: die wacht op een tik, en dan is de app al actief.
//
// Alleen wachten werkt niet: zonder venster op het scherm wordt de app nooit actief
// (gemeten: eeuwig wachten, zwart). Daarom eerst een gewoon venster zonder OpenGL tonen,
// net als de Safe Restart-dialoog deed: de eventlus draaien tot de app actief is, dat
// venster weer weghalen, en pas dan maakt OpenCPN zijn eigen vensters.
// Geen tijdslimiet: wordt de app op de achtergrond gestart (locatie op de achtergrond),
// dan wacht hij tot de gebruiker hem opent. Uitzetten met env OCPNIOS_NO_WAIT_ACTIVE.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QWidget>

#include <wx/log.h>

#include <cstdlib>

void ocpnios_WaitUntilActive() {
  if (std::getenv("OCPNIOS_NO_WAIT_ACTIVE")) return;
  if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
    wxLogMessage("opencpn-ios: app al actief bij start");
    return;
  }
  QElapsedTimer t;
  t.start();
  // Gewoon (raster)venster, schermvullend, in de achtergrondkleur van het systeem.
  QWidget placeholder;
  placeholder.setAutoFillBackground(true);
  placeholder.showFullScreen();
  // Een echte eventlus (zoals QDialog::exec), geen processEvents-lus: met die laatste
  // bleef de app op iOS "inactief" (gemeten in de simulator, 9 nov 2026).
  QEventLoop loop;
  QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, &loop,
                   [&loop](Qt::ApplicationState st) {
                     if (st == Qt::ApplicationActive) loop.quit();
                   });
  if (QGuiApplication::applicationState() != Qt::ApplicationActive) loop.exec();
  placeholder.hide();
  QCoreApplication::processEvents();
  wxLogMessage("opencpn-ios: %lld ms gewacht tot de app actief was", (long long)t.elapsed());
}
