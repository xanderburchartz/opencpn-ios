// Gedeelde OpenGL-contexts voor alle QOpenGLWidgets.
//
// Onder wxQt is elk wxGLCanvas een QOpenGLWidget met een eigen context. OpenCPN 5.14 maakt
// het kaartvenster na de eerste start opnieuw; shaders en texturen (kompas, kaarttegels) waren
// dan in de context van het oude venster gemaakt en verwezen in de nieuwe naar niets of naar
// iets anders: reuzenkompas en rommel op de kaart, vooral met het radarvenster open
// (simulator, 10 nov 2026, gemeten met lldb). Met gedeelde contexts horen texturen en
// shaderprogramma's bij alle GL-vensters samen, zoals OpenCPN aanneemt.
//
// Moet vóór het aanmaken van de QApplication (door wxWidgets) gezet worden; daarom als
// statische initialisatie.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCoreApplication>

namespace {
const bool kShareGl = [] {
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  return true;
}();
}  // namespace
