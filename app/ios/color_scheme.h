// Kleuren van menubalk, statusregel en iOS-statusbalk (zie color_scheme.mm).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

class QWidget;

// Achtergrond (bg) en tekst (fg) voor menubalk, statusregel en uitklapmenu's van het
// hoofdvenster; `dark` zet de iOS-statusbalk op lichte tekst.
void OcpnIosApplyUiColors(QWidget* mainWindow, int bgR, int bgG, int bgB, int fgR,
                          int fgG, int fgB, bool dark);
