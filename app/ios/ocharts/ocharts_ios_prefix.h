// o-charts_pi op iOS: geforceerd ingesloten in elk bronbestand van de plugin
// (app/cmake/ios-ocharts.cmake), vóór alle andere headers.
//
// Op iOS kan een app geen programma starten. De plugin roept oexserverd aan met
// wxExecute; door deze naam vóór wx/utils.h te vervangen, declareert en gebruikt de plugin
// OcIosWxExecute met precies dezelfde parameters. ocharts_ios_glue.cpp levert die functies
// en stuurt aanroepen van OCIOS_SENCUTIL_SENTINEL door naar ocharts_ios_backend.h.
// Andere aanroepen (bijv. "open -R" om een map te tonen) geven op iOS een nette fout.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef __cplusplus
#define wxExecute OcIosWxExecute
#endif
