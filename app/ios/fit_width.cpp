// Inhoud van een venster op schermbreedte brengen (iOS-blad, zie window_fit.cpp).
//
// Dialogen van OpenCPN en van plugins zijn op desktopmaat ontworpen: vaste
// minimumbreedtes (bv. een vinkje van 240 pt), lijsten met een vaste kolombreedte
// (598 pt), labels op één regel. Gemeten in het voorkeurenvenster van Dashboard
// (10 nov 2026): paneel 719 pt breed op een scherm van 440 pt, rechts afgekapt.
//
// Werkwijze, in een paar rondes (na elke ronde schuift de indeling op):
//  - een element dat rechts buiten zijn ouder valt:
//    - tekst (label, vinkje, rondje): op woordgrenzen afbreken tot de beschikbare
//      breedte; de oorspronkelijke tekst bewaard in de naam ("ocpnios:" + tekst);
//    - vaste minimumbreedte groter dan nodig: loslaten;
//    - overige (lijst, invulvak, keuzelijst): minimumbreedte = wat er nog past;
//  - daarna opnieuw indelen; schuifvensters zonder horizontale schuifruimte.
// Zelfde aanpak als IosFitPage in OpenCPN's options.cpp, maar voor elk venster.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/notebook.h>
#include <wx/radiobut.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/window.h>
#include <wx/splitter.h>
#include <wx/slider.h>
#include <wx/dirctrl.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>
#include <wx/glcanvas.h>
#include <wx/bookctrl.h>
#include <wx/treectrl.h>
#include <wx/listbox.h>
#include <wx/pickerbase.h>
#include <wx/checklst.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/imaglist.h>
#include <wx/bmpbuttn.h>
#include <wx/panel.h>
#include <wx/tglbtn.h>
#include <wx/image.h>

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QScroller>
#include <QListWidget>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QVariant>
#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdlib>
#include <wx/log.h>
#include <functional>

namespace {

bool IsContainer(wxWindow* w) {
  return w->GetChildren().GetCount() > 0 || dynamic_cast<wxStaticBox*>(w) ||
         dynamic_cast<wxBookCtrlBase*>(w);
}

bool IsTextLike(wxWindow* w) {
  return dynamic_cast<wxStaticText*>(w) || dynamic_cast<wxCheckBox*>(w) ||
         dynamic_cast<wxRadioButton*>(w);
}

// Tekstbreedte zoals Qt hem tekent. wx' GetTextExtent viel op iOS ~15% te smal uit
// (lettertype na de stylesheet), waardoor een rondje in GRIB toch afgekapt bleef.
int TextWidth(wxWindow* w, const wxString& t) {
  if (QWidget* q = w->GetHandle()) {
    q->ensurePolished();
    return QFontMetrics(q->font()).horizontalAdvance(QString::fromUtf8(t.utf8_string().c_str()));
  }
  return w->GetTextExtent(t).x;
}

// Op woordgrenzen afbreken tot breedte `budget` (in het lettertype van w).
wxString WrapWords(wxWindow* w, const wxString& text, int budget) {
  wxString out, line;
  for (wxString word : wxSplit(text, ' ', '\0')) {
    // Woord breder dan de regel (een link zonder spaties, Watchdog): per teken.
    while (TextWidth(w, word) > budget && word.length() > 1) {
      if (!line.empty()) {
        out += line + "\n";
        line.clear();
      }
      size_t n = 1;
      while (n < word.length() && TextWidth(w, word.Left(n + 1)) <= budget) n++;
      out += word.Left(n) + "\n";
      word = word.Mid(n);
    }
    const wxString trial = line.empty() ? word : line + " " + word;
    if (!line.empty() && TextWidth(w, trial) > budget) {
      out += line + "\n";
      line = word;
    } else {
      line = trial;
    }
  }
  return out + line;
}

// Oorspronkelijke minimumbreedte onthouden (als Qt-eigenschap van het widget), zodat
// een volgende passing (bv. na draaien naar liggend) eerst kan terugzetten.
const char* const kOrigMinProp = "ocpnios_origmin";
void SetMinWidth(wxWindow* c, int w) {
  // Knoppen niet smaller dan hun tekst (Tactics: "oevoege", "epass").
  if (dynamic_cast<wxButton*>(c) && w >= 0 && w < c->GetBestSize().x) return;
  // Keuzelijsten niet smaller dan hun langste keuze plus pijltje (Dashboard liggend:
  // "Vertic" in plaats van "Verticaal", 10 nov 2026).
  if (auto* ch = dynamic_cast<wxItemContainerImmutable*>(c)) {
    if (w >= 0 && (dynamic_cast<wxChoice*>(c) || dynamic_cast<wxComboBox*>(c))) {
      int need = 0;
      for (unsigned int k = 0; k < ch->GetCount(); k++)
        need = std::max(need, TextWidth(c, ch->GetString(k)));
      if (w < need + 50) return;
    }
  }
  QWidget* q = c->GetHandle();
  if (q && !q->property(kOrigMinProp).isValid()) q->setProperty(kOrigMinProp, c->GetMinSize().x);
  c->SetMinSize(wxSize(w, c->GetMinSize().y));
}

// Alles terug naar de oorspronkelijke tekst en minimumbreedte.
void Restore(wxWindow* w) {
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    if (c->GetName().StartsWith("ocpnios:")) {
      const wxString orig = c->GetName().Mid(8);
      if (c->GetLabel() != orig) c->SetLabel(orig);
      c->InvalidateBestSize();
    }
    if (QWidget* q = c->GetHandle()) {
      const QVariant v = q->property(kOrigMinProp);
      if (v.isValid()) c->SetMinSize(wxSize(v.toInt(), c->GetMinSize().y));
    }
    Restore(c);
  }
}

// Rijen en rasters van blokken (sizers, panelen) die naast elkaar staan en samen
// breder zijn dan het scherm, onder elkaar zetten (Autopilot Route: blokken Mode,
// NMEA en Bearing in twee kolommen, rechts buiten beeld). Oorspronkelijke stand
// onthouden zodat liggend weer naast elkaar kan.
std::map<wxSizer*, int> g_origLayout;  // box: oriëntatie, raster: aantal kolommen

int CountSections(wxSizer* sz) {
  int n = 0;
  for (wxSizerItem* item : sz->GetChildren()) {
    if (item->IsSizer()) n++;
    else if (item->IsWindow() && item->GetWindow()->GetChildren().GetCount() > 0) n++;
  }
  return n;
}

void ForEachSizer(wxWindow* w, const std::function<void(wxSizer*)>& fn) {
  std::function<void(wxSizer*)> walk = [&](wxSizer* sz) {
    for (wxSizerItem* item : sz->GetChildren())
      if (item->IsSizer()) walk(item->GetSizer());
    fn(sz);
  };
  if (w->GetSizer()) walk(w->GetSizer());
  for (wxWindow* c : w->GetChildren())
    if (!c->IsTopLevel()) ForEachSizer(c, fn);
}

void RestoreSizers(wxWindow* root) {
  ForEachSizer(root, [](wxSizer* sz) {
    auto it = g_origLayout.find(sz);
    if (it == g_origLayout.end()) return;
    if (auto* fg = dynamic_cast<wxFlexGridSizer*>(sz)) {
      if (it->second == -1000) {
        fg->SetCols(0);
        fg->SetRows(1);
        return;
      }
      fg->SetCols(it->second);
    } else if (auto* bs = dynamic_cast<wxBoxSizer*>(sz)) {
      bs->SetOrientation(it->second);
    }
  });
}

  // Raster met alleen schakelaars in meer kolommen (WeatherRouting › Opties: "Detecteer
  // land | Detecteer gebiedsgrens"): op een smal scherm één kolom, zodat ze iOS-regels
  // worden (tekst links, schakelaar rechts) in plaats van twee schakelaars naast elkaar
  // met afgebroken tekst (11 nov 2026).
bool SwitchGridsToColumn(wxWindow* root, int limit) {
  bool changed = false;
  if (limit < 600)
    ForEachSizer(root, [&](wxSizer* sz) {
      auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
      if (!fg || fg->GetEffectiveColsCount() <= 1) return;
      // Ook schakelaar naast een groepsvak (WeatherRouting › Gegevensbron: "GRIB" naast
      // "Klimatologie"): onder elkaar.
      int n = 0, boxes = 0;
      for (wxSizerItem* it : fg->GetChildren()) {
        if (!it->IsShown()) continue;
        if (it->IsSpacer()) continue;
        if (it->IsSizer() && dynamic_cast<wxStaticBoxSizer*>(it->GetSizer())) {
          boxes++;
          continue;
        }
        if (!it->IsWindow() || !dynamic_cast<wxCheckBox*>(it->GetWindow())) return;
        n++;
      }
      if (n < 1 || n + boxes < 2) return;
      if (!g_origLayout.count(sz)) g_origLayout[sz] = fg->GetCols();
      for (int i = 15; i >= 0; i--)
        if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
      if (fg->GetRows() > 0) fg->SetRows(0);
      fg->SetCols(1);
      fg->AddGrowableCol(0);
      for (wxSizerItem* it : fg->GetChildren()) it->SetFlag(it->GetFlag() | wxEXPAND);
      changed = true;
    });
  return changed;
}

bool ReflowSections(wxWindow* root, int limit) {
  bool changed = false;
  // Raster van één rij met groeiend aantal kolommen (pypilot: Config, Gains, Calibration,
  // Settings, Stats, X; "Settings" viel rechts weg): over meer regels verdelen.
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg || fg->GetRows() != 1 || fg->GetCols() != 0) return;
    int n = 0, widest = 0;
    for (wxSizerItem* it : fg->GetChildren())
      if (it->IsShown()) {
        n++;
        widest = std::max(widest, it->CalcMin().x + 2 * it->GetBorder());
      }
    if (n < 2 || fg->CalcMin().x <= limit) return;
    int cols = std::max(1, std::min(n, limit / std::max(1, widest)));
    // Alleen knoppen (WeatherRouting: Berekenen, Als track, Als route, Exporteer): even
    // brede knoppen, twee naast elkaar als ze passen, anders één per regel over de
    // volle breedte. Ze stonden links uitgelijnd in wisselende breedtes onder elkaar en
    // vielen onder de kaart weg (11 nov 2026).
    bool buttons = true;
    int widestBtn = 0;
    for (wxSizerItem* it : fg->GetChildren()) {
      if (!it->IsShown()) continue;
      if (!it->IsWindow() || !(dynamic_cast<wxButton*>(it->GetWindow()) ||
                               dynamic_cast<wxGauge*>(it->GetWindow()))) {
        buttons = false;
        break;
      }
      if (dynamic_cast<wxButton*>(it->GetWindow()))
        widestBtn = std::max(widestBtn, it->GetWindow()->GetBestSize().x + 2 * it->GetBorder());
    }
    if (buttons) {
      cols = widestBtn * 2 <= limit - 8 ? 2 : 1;
      for (int i = 15; i >= 0; i--)  // GetCols() is 0 bij een rij met groeiend aantal kolommen
        if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
      for (wxSizerItem* it : fg->GetChildren()) it->SetFlag(it->GetFlag() | wxEXPAND);
    }
    if (!g_origLayout.count(sz)) g_origLayout[sz] = -1000;  // markering: 1 rij
    fg->SetRows(0);
    fg->SetCols(cols);
    if (buttons)
      for (int i = 0; i < cols; i++)
        if (!fg->IsColGrowable(i)) fg->AddGrowableCol(i, 1);
    changed = true;
  });
  // Raster van drietallen "label | veld | eenheid" naast elkaar (WeatherRouting ›
  // Motoring: "Drempel [2,0] knopen  Motorsnelheid [5,0] knopen") dat te breed is: één
  // drietal per regel.
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg || fg->GetCols() < 6 || fg->GetCols() % 3 != 0 || fg->CalcMin().x <= limit) return;
    std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
    for (size_t i = 0; i < v.size(); i++) {
      wxWindow* w = v[i]->IsWindow() ? v[i]->GetWindow() : nullptr;
      const bool text = w && dynamic_cast<wxStaticText*>(w);
      if ((i % 3 == 1) == text) return;  // midden een veld, buiten een tekst
    }
    if (!g_origLayout.count(sz)) g_origLayout[sz] = fg->GetCols();
    for (int i = 15; i >= 0; i--)
      if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
    if (fg->GetRows() > 0) fg->SetRows(0);
    fg->SetCols(3);
    changed = true;
  });
  // Raster met een heel breed element (Raymarine: label naast een keuzelijst met een
  // lange tekst; het label werd overlapt): één kolom, label erboven.
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg || fg->GetCols() <= 1) return;
    bool wide = false;
    for (wxSizerItem* it : fg->GetChildren())
      if (it->IsWindow() && it->IsShown() && !IsContainer(it->GetWindow()) &&
          !IsTextLike(it->GetWindow()) && it->GetWindow()->GetBestSize().x > limit * 6 / 10)
        wide = true;
    if (!wide) return;
    if (!g_origLayout.count(sz)) g_origLayout[sz] = fg->GetCols();
    for (int i = fg->GetCols() - 1; i >= 1; i--)
      if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
    fg->SetCols(1);
    changed = true;
  });
  ForEachSizer(root, [&](wxSizer* sz) {
    if (CountSections(sz) < 2 || sz->CalcMin().x <= limit) return;
    if (auto* fg = dynamic_cast<wxFlexGridSizer*>(sz)) {
      if (fg->GetCols() <= 1) return;
      // Zoveel kolommen als er passen (het breedste kader telt), niet meteen één: op de
      // iPad bleef bij de radarvoorkeuren twee derde van de breedte leeg (10 nov 2026).
      int widest = 1;
      for (wxSizerItem* it : fg->GetChildren())
        if (it->IsShown()) widest = std::max(widest, it->CalcMin().x + 2 * it->GetBorder());
      const int cols = std::max(1, std::min(fg->GetCols() - 1, limit / widest));
      if (!g_origLayout.count(sz)) g_origLayout[sz] = fg->GetCols();
      // Groeibare kolommen buiten het nieuwe aantal bestaan niet meer.
      for (int i = fg->GetCols() - 1; i >= cols; i--)
        if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
      if (fg->GetRows() > 0) fg->SetRows(0);  // anders te weinig cellen
      fg->SetCols(cols);
      changed = true;
    } else if (auto* bs = dynamic_cast<wxBoxSizer*>(sz)) {
      if (bs->GetOrientation() != wxHORIZONTAL) return;
      if (!g_origLayout.count(sz)) g_origLayout[sz] = wxHORIZONTAL;
      bs->SetOrientation(wxVERTICAL);
      changed = true;
    }
  });
  return changed;
}

// Een sizer in de boom vervangen (wx: Replace zoekt alleen binnen één venster).
bool ReplaceSizer(wxWindow* w, wxSizer* old, wxSizer* repl) {
  if (w->GetSizer() == old) {
    w->SetSizer(repl, true);
    return true;
  }
  if (w->GetSizer() && w->GetSizer()->Replace(old, repl, true)) return true;
  for (wxWindow* c : w->GetChildren())
    if (!c->IsTopLevel() && ReplaceSizer(c, old, repl)) return true;
  return false;
}

// Rij met veel losse elementen (Polar: "Ringen [9] Graden [10] Wind [elke] ..."):
// doorlopen naar een volgende regel (wxWrapSizer) in plaats van alles onder elkaar.
// Een label blijft bij het element erna; scheidingsstreepjes en rek vervallen.
// Blijvend: een wrap-rij past zich zelf aan staand en liggend aan.
void WrapRow(wxWindow* root, wxBoxSizer* bs) {
  struct Part { wxWindow* w; wxSizer* s; int flag; int border; };
  std::vector<Part> parts;
  for (wxSizerItem* it : bs->GetChildren()) {
    if (it->IsSpacer()) continue;
    if (it->IsWindow()) {
      if (dynamic_cast<wxStaticLine*>(it->GetWindow())) {
        it->GetWindow()->Hide();
        continue;
      }
      parts.push_back({it->GetWindow(), nullptr, it->GetFlag(), it->GetBorder()});
    } else if (it->IsSizer()) {
      parts.push_back({nullptr, it->GetSizer(), it->GetFlag(), it->GetBorder()});
    }
  }
  for (const Part& p : parts) {
    if (p.w) bs->Detach(p.w);
    else bs->Detach(p.s);
  }
  auto* wrap = new wxWrapSizer(wxHORIZONTAL);
  for (size_t i = 0; i < parts.size(); i++) {
    const Part& p = parts[i];
    const int flag = (p.flag & ~wxEXPAND) | wxALIGN_CENTER_VERTICAL;
    if (p.w && dynamic_cast<wxStaticText*>(p.w) && i + 1 < parts.size() && parts[i + 1].w &&
        !dynamic_cast<wxStaticText*>(parts[i + 1].w)) {
      auto* pair = new wxBoxSizer(wxHORIZONTAL);
      pair->Add(p.w, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      pair->Add(parts[i + 1].w, 0, wxALIGN_CENTER_VERTICAL);
      wrap->Add(pair, 0, wxALL, 4);
      i++;
    } else if (p.w) {
      wrap->Add(p.w, 0, flag, p.border);
    } else {
      wrap->Add(p.s, 0, flag, p.border);
    }
  }
  g_origLayout.erase(bs);
  if (!ReplaceSizer(root, bs, wrap)) {
    // Niet gevonden (mag niet): terugzetten in de oude rij.
    for (const Part& p : parts) {
      if (p.w) bs->Add(p.w, 0, p.flag, p.border);
      else bs->Add(p.s, 0, p.flag, p.border);
    }
    delete wrap;
  }
}

// Scheidingsbalk van een wx-splitter (generiek getekend: dikke zwarte balk met
// stippels, WeatherRouting 11 nov 2026) als iOS-scheiding: achtergrondkleur met een dun
// lijntje en een kleine grijze greep in het midden. Na het tekenen van wx eroverheen.
class SashLook : public QObject {
 public:
  explicit SashLook(wxSplitterWindow* sp) : QObject(sp->GetHandle()), m_sp(sp) {}
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::Paint || m_busy || !m_sp->IsSplit()) return false;
    auto* w = static_cast<QWidget*>(obj);
    m_busy = true;
    QCoreApplication::sendEvent(w, ev);
    m_busy = false;
    const bool horiz = m_sp->GetSplitMode() == wxSPLIT_HORIZONTAL;
    const int pos = m_sp->GetSashPosition();
    const int size = std::max(4, m_sp->GetSashSize());
    const QRect r = horiz ? QRect(0, pos, w->width(), size) : QRect(pos, 0, size, w->height());
    const bool dark = QCoreApplication::instance() &&
                      QCoreApplication::instance()->property("ocpnios_dark").toBool();
    QPainter p(w);
    p.fillRect(r, dark ? QColor(0, 0, 0) : QColor(0xf2, 0xf2, 0xf7));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(dark ? QColor(0x48, 0x48, 0x4a) : QColor(0xc7, 0xc7, 0xcc));
    const QPoint c = r.center();
    const QRectF grip = horiz ? QRectF(c.x() - 18, c.y() - 2.5, 36, 5)
                              : QRectF(c.x() - 2.5, c.y() - 18, 5, 36);
    p.drawRoundedRect(grip, 2.5, 2.5);
    return true;
  }

 private:
  wxSplitterWindow* m_sp;
  bool m_busy = false;
};

// Splitter met twee vensters naast elkaar (Polar: diagram | tabel): op een smal
// scherm boven elkaar, breed weer naast elkaar.
void FitSplitters(wxWindow* w, int limit) {
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    if (auto* sp = dynamic_cast<wxSplitterWindow*>(c)) {
      QWidget* q = sp->GetHandle();
      if (q && !q->property("ocpnios_sash").toBool()) {
        q->setProperty("ocpnios_sash", true);
        q->installEventFilter(new SashLook(sp));
        q->update();
      }
      if (sp->IsSplit() && q) {
        wxWindow* a = sp->GetWindow1();
        wxWindow* b = sp->GetWindow2();
        const bool narrow = limit < 600;
        if (narrow && sp->GetSplitMode() == wxSPLIT_VERTICAL) {
          q->setProperty("ocpnios_split_v", true);
          sp->Unsplit(b);
          b->Show();
          sp->SplitHorizontally(a, b, 0);
        } else if (!narrow && sp->GetSplitMode() == wxSPLIT_HORIZONTAL &&
                   q->property("ocpnios_split_v").toBool()) {
          sp->Unsplit(b);
          b->Show();
          sp->SplitVertically(a, b, 0);
        }
      }
    }
    FitSplitters(c, limit);
  }
}

int GetCharW(wxWindow* c) { return std::max(6, c->GetCharWidth()); }

bool FitText(wxWindow* c, int avail) {
  wxString orig = c->GetName().StartsWith("ocpnios:") ? c->GetName().Mid(8) : c->GetLabel();
  if (!c->GetName().StartsWith("ocpnios:")) c->SetName("ocpnios:" + orig);
  orig.Replace("\n", " ");
  int budget = avail;
  if (!dynamic_cast<wxStaticText*>(c)) budget -= 70;  // schakelaar/rondje + ruimte
  if (budget < 60) return false;
  // Een woord dat net niet past niet midden in het woord breken ("Cursorgegev / ens",
  // GRIB › Instellingen, 11 nov 2026): de ruimte iets groter maken; de kolom groeit
  // mee. Alleen bij een kleine overschrijding; een lange link (Watchdog) blijft per
  // teken afgebroken.
  {
    int longest = 0;
    for (const wxString& word : wxSplit(orig, ' ', '\0'))
      longest = std::max(longest, TextWidth(c, word));
    if (longest > budget && longest <= budget * 14 / 10) budget = longest;
  }
  const wxString wrapped = WrapWords(c, orig, budget);
  bool changed = false;
  if (wrapped != c->GetLabel()) {
    c->SetLabel(wrapped);
    changed = true;
  }
  if (c->GetMinSize().x > 0) {
    SetMinWidth(c, -1);
    changed = true;
  }
  c->InvalidateBestSize();
  return changed;
}

// Keuzelijsten: minimumbreedte ook op het Qt-widget zetten. wx bewaart zijn minimum
// alleen voor zijn eigen indeling; bij een latere indeling (na draaien) maakte Qt de
// lijst toch smaller (gemeten 120 → 66 pt, "Vertic", 10 nov 2026).
void LockChoiceWidths(wxWindow* w, int limit) {
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    auto* ch = dynamic_cast<wxItemContainerImmutable*>(c);
    // Formulierwaarde (FormValueText): vult de kolom en kort de tekst zelf in; met de
    // breedte van de langste keuze werd Routes breder dan het scherm (pictogramnamen).
    if (ch && c->GetHandle() && c->GetHandle()->property("ocpnios_formtext").toBool()) {
      c->GetHandle()->setMinimumWidth(0);
      c->SetMinSize(wxSize(80, c->GetMinSize().y));
    } else if (ch && (dynamic_cast<wxChoice*>(c) || dynamic_cast<wxComboBox*>(c)) &&
               c->GetHandle()) {
      int need = 0;
      for (unsigned int k = 0; k < ch->GetCount(); k++)
        need = std::max(need, TextWidth(c, ch->GetString(k)));
      // Grens vanaf de plaats van de lijst, niet vanaf de linkerrand: in een kaart (GRIB ›
      // Instellingen: "Wind") werd de lijst anders 30 pt breder dan de pagina (11 nov 2026).
      int cap = limit - 20;
      if (QWidget* top = c->GetHandle()->window()) {
        const int x = c->GetHandle()->mapTo(top, QPoint(0, 0)).x();
        if (x > 0 && x < limit) cap = limit - 20 - x;
      }
      need = std::min(std::max(need + 50, c->GetMinSize().x), std::max(60, cap));
      c->GetHandle()->setMinimumWidth(need);
      // Ook in wx, anders reserveert de indeling minder ruimte dan Qt tekent en
      // overlapt de lijst het volgende label (Polar, 10 nov 2026).
      if (c->GetMinSize().x < need) {
        QWidget* q = c->GetHandle();
        if (!q->property(kOrigMinProp).isValid()) q->setProperty(kOrigMinProp, c->GetMinSize().x);
        c->SetMinSize(wxSize(need, c->GetMinSize().y));
      }
    }
    LockChoiceWidths(c, limit);
  }
}

}  // namespace

// In een horizontale rij staan onderdelen zonder eigen verticale uitlijning bovenaan
// (wx-standaard). Met de hogere iOS-velden en -knoppen stond tekst er zo boven naast:
// "Verberg werkbalk na [2] seconden", "Voorbeeld" naast de lettertypeknoppen (iPad,
// 10 nov 2026). Zulke onderdelen in het midden zetten.
void CenterRowItems(wxWindow* root) {
  // Rasters (label | veld): een label naast één venster (veld, keuzelijst) in het midden
  // van zijn cel. Labels met wxEXPAND vulden de cel en tekenden hun tekst bovenaan
  // (Eenheden, Eigen Schip, Verbindingen; iPad 10 nov 2026). Staat er een sizer naast
  // (meerdere schakelaars onder elkaar), dan blijft het label bovenaan.
  ForEachSizer(root, [](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg) return;
    const int cols = fg->GetEffectiveColsCount();
    if (cols < 2) return;
    wxSizerItemList& items = fg->GetChildren();
    std::vector<wxSizerItem*> v(items.begin(), items.end());
    for (size_t i = 0; i < v.size(); i++) {
      wxSizerItem* it = v[i];
      if (!it->IsWindow() || !dynamic_cast<wxStaticText*>(it->GetWindow())) continue;
      int f = it->GetFlag();
      if (f & (wxALIGN_CENTER_VERTICAL | wxALIGN_BOTTOM)) continue;
      const size_t rowStart = i - i % cols;
      bool neighbourIsWindow = false, neighbourIsSizer = false;
      for (size_t j = rowStart; j < std::min(v.size(), rowStart + cols); j++) {
        if (j == i) continue;
        if (v[j]->IsSizer()) {
          // Eén rij naast elkaar (keuzerondjes "Locale Tijd ○ UTC") telt als een veld.
          auto* hb = dynamic_cast<wxBoxSizer*>(v[j]->GetSizer());
          if (hb && hb->GetOrientation() == wxHORIZONTAL &&
              !dynamic_cast<wxWrapSizer*>(v[j]->GetSizer()))
            neighbourIsWindow = true;
          else
            neighbourIsSizer = true;
        }
        else if (v[j]->IsWindow() && !dynamic_cast<wxStaticText*>(v[j]->GetWindow()))
          neighbourIsWindow = true;
      }
      if (!neighbourIsWindow || neighbourIsSizer) continue;
      if (f & wxEXPAND) {
        f &= ~wxEXPAND;
        if (it->GetWindow()->GetWindowStyleFlag() & wxALIGN_RIGHT) f |= wxALIGN_RIGHT;
      }
      it->SetFlag(f | wxALIGN_CENTER_VERTICAL);
    }
  });
  ForEachSizer(root, [](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxHORIZONTAL) return;
    wxSizerItemList& kids = bs->GetChildren();
    for (auto node = kids.begin(); node != kids.end(); ++node) {
      wxSizerItem* it = *node;
      if (!it->IsWindow()) continue;
      // Keuzerondjes naast elkaar: ruimte rechts, anders plakte de tekst tegen het
      // volgende rondje ("Locale Tijd ○ UTC", Eenheden, iPad 10 nov 2026).
      if (dynamic_cast<wxRadioButton*>(it->GetWindow()) && std::next(node) != kids.end() &&
          !(it->GetWindow()->GetHandle() &&
            it->GetWindow()->GetHandle()->property("ocpnios_segment").toBool()) &&
          !((it->GetFlag() & wxRIGHT) && it->GetBorder() >= 12)) {
        it->SetFlag(it->GetFlag() | wxRIGHT);
        it->SetBorder(std::max(it->GetBorder(), 16));
      }
      const int f = it->GetFlag();
      if (f & (wxEXPAND | wxALIGN_CENTER_VERTICAL | wxALIGN_BOTTOM)) continue;
      it->SetFlag(f | wxALIGN_CENTER_VERTICAL);
    }
  });
}

// Bladen zijn schermhoog: een blok met uitrekfactor in een verticale indeling vulde de
// rest met lege ruimte (GRIB-voorkeuren: kader met zes schakelaars tot halverwege de
// pagina, iPad 10 nov 2026). Alleen laten uitrekken wat er baat bij heeft.
bool WantsSpace(wxWindow* w) {
  if (!w || !w->IsShown()) return false;
  // Korte lijst met vinkjes: wordt uitgevouwen (FlattenInnerScroll), geen lege rest.
  if (auto* cl = dynamic_cast<wxCheckListBox*>(w))
    if (cl->GetCount() > 0 && cl->GetCount() <= 40) return false;
  if (dynamic_cast<wxListCtrl*>(w) || dynamic_cast<wxListBox*>(w) ||
      dynamic_cast<wxTreeCtrl*>(w) || dynamic_cast<wxBookCtrlBase*>(w) ||
      dynamic_cast<wxScrolledWindow*>(w) || dynamic_cast<wxSplitterWindow*>(w) ||
      dynamic_cast<wxGLCanvas*>(w))
    return true;
  if (auto* t = dynamic_cast<wxTextCtrl*>(w)) return t->IsMultiLine();
  if (dynamic_cast<wxControl*>(w)) return false;  // knop, schakelaar, label, keuzelijst
  // Eigen tekenvlak (polair diagram, grafiek): een venster zonder kinderen.
  bool any = false;
  for (wxWindow* c : w->GetChildren()) {
    if (c->IsTopLevel()) continue;
    any = true;
    if (WantsSpace(c)) return true;
  }
  return !any;
}

bool ItemWantsSpace(wxSizerItem* it) {
  if (it->IsWindow()) return WantsSpace(it->GetWindow());
  if (it->IsSizer()) {
    if (auto* sb = dynamic_cast<wxStaticBoxSizer*>(it->GetSizer()))
      if (!sb->GetStaticBox()->IsShown()) return false;
    for (wxSizerItem* c : it->GetSizer()->GetChildren())
      if (c->IsShown() && ItemWantsSpace(c)) return true;
  }
  return false;
}

void CollapseStretch(wxWindow* root) {
  ForEachSizer(root, [](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxVERTICAL) return;
    for (wxSizerItem* it : bs->GetChildren()) {
      if (it->GetProportion() <= 0 || it->IsSpacer()) continue;
      if (!ItemWantsSpace(it)) it->SetProportion(0);
    }
  });
}

// Lijst met vinkjes (wxCheckListBox): een tik op de hele regel zet het vinkje om, zoals
// in iOS. Qt wisselde bij een tik op de tekst alleen de selectie; alleen het rondje zelf
// zette het vinkje om (WR-importvraag, iPad 11 nov 2026). Vegen gaat via touch_ui.cpp,
// dat bij slepen een loslating ver buiten de lijst stuurt: die raakt geen regel.
class CheckRowTap : public QObject {
 public:
  explicit CheckRowTap(QListWidget* lw) : QObject(lw), m_lw(lw) {}

 protected:
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() != QEvent::MouseButtonPress && ev->type() != QEvent::MouseButtonRelease &&
        ev->type() != QEvent::MouseButtonDblClick)
      return false;
    auto* me = static_cast<QMouseEvent*>(ev);
    QListWidgetItem* it = m_lw->itemAt(me->position().toPoint());
    if (ev->type() == QEvent::MouseButtonPress) {
      m_pressed = it;
      return it != nullptr;
    }
    if (ev->type() == QEvent::MouseButtonRelease && it && it == m_pressed &&
        (it->flags() & Qt::ItemIsUserCheckable))
      it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    m_pressed = nullptr;
    return it != nullptr;
  }

 private:
  QListWidget* m_lw;
  QListWidgetItem* m_pressed = nullptr;
};

// Schuifvlak in een schuifvlak: in een blad (dat zelf schuift) gaf een wxScrolledWindow
// in een kader een eigen, grijs schuifvak met afgesneden velden, en een lijst met
// vinkjes een tweede schuifvak (Weerrouterings-instellingen, Xander 11 nov 2026:
// "ziet er niet uit"). Uitvouwen tot de volle inhoud; het blad schuift dan als geheel.
void FlattenInnerScroll(wxWindow* root) {
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(c)) {
        if (dynamic_cast<wxStaticBox*>(sw->GetParent()) && sw->GetSizer()) {
          sw->EnableScrolling(false, false);
          sw->SetScrollRate(0, 0);
          // Eén kolom zonder groeikolom: de regels bleven zo smal als hun inhoud, met
          // schakelaars en velden midden in de kaart in plaats van rechts.
          if (auto* fg = dynamic_cast<wxFlexGridSizer*>(sw->GetSizer()))
            if (fg->GetCols() == 1 && !fg->IsColGrowable(0)) fg->AddGrowableCol(0);
          sw->Layout();
          const wxSize need = sw->GetSizer()->GetMinSize();
          sw->SetMinSize(wxSize(-1, need.y));
          // wx tekent zelf de achtergrond van het schuifvenster (systeemgrijs): de
          // kleur van de kaart (ocpnios_SheetStyle, QGroupBox).
          {
            const bool dark = QCoreApplication::instance() &&
                              QCoreApplication::instance()->property("ocpnios_dark").toBool();
            sw->SetBackgroundColour(dark ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE);
          }
          if (QWidget* q = sw->GetHandle()) {
            // Doorzichtig: de witte kaart (kader) erachter, niet het grijs van het blad.
            q->setAutoFillBackground(false);
            q->setStyleSheet("QAbstractScrollArea { background: transparent; border: none; }");

            if (auto* sa = qobject_cast<QAbstractScrollArea*>(q)) {
              sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
              sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
              if (QWidget* vp = sa->viewport()) {
                vp->setAutoFillBackground(false);
                for (QWidget* k : vp->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                  k->setAutoFillBackground(false);
              }
            }
          }
        }
      }
      // Tabbladen binnen een kaart (Kaart-downloader › Nieuwe kaartbron): de pagina's
      // hadden systeemgrijs en gaven een grijze rand om de lijst (iPhone 12 nov 2026).
      if (auto* bk = dynamic_cast<wxBookCtrlBase*>(c)) {
        if (dynamic_cast<wxStaticBox*>(bk->GetParent()) ||
            dynamic_cast<wxStaticBoxSizer*>(bk->GetContainingSizer())) {
          const bool dark = QCoreApplication::instance() &&
                            QCoreApplication::instance()->property("ocpnios_dark").toBool();
          const wxColour card = dark ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE;
          for (size_t pi = 0; pi < bk->GetPageCount(); pi++)
            if (wxWindow* pg = bk->GetPage(pi)) {
              pg->SetBackgroundColour(card);
              pg->Refresh();
            }
        }
      }
      if (auto* cl = dynamic_cast<wxCheckListBox*>(c)) {
        const unsigned n = cl->GetCount();
        if (n > 0 && n <= 40) {
          int rowH = 0;
          if (auto* lv = qobject_cast<QAbstractItemView*>(cl->GetHandle()))
            rowH = lv->sizeHintForRow(0);
          if (rowH <= 0) rowH = cl->GetCharHeight() + 12;
          cl->SetMinSize(wxSize(-1, rowH * static_cast<int>(n) + 8));
          // Elke regel een vinkjesstaat: zonder (nooit aangevinkt) tekende Qt geen rondje.
          for (unsigned i = 0; i < n; i++) cl->Check(i, cl->IsChecked(i));
          if (auto* lw = qobject_cast<QListWidget*>(cl->GetHandle()))
            if (!lw->property("ocpnios_checktap").toBool()) {
              lw->setProperty("ocpnios_checktap", true);
              lw->viewport()->installEventFilter(new CheckRowTap(lw));
            }
          if (auto* sa = qobject_cast<QAbstractScrollArea*>(cl->GetHandle())) {
            sa->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            // Geen blauwe selectiebalk: in een lijst met vinkjes is het vinkje de keuze.
            if (auto* iv = qobject_cast<QAbstractItemView*>(sa)) {
              iv->setSelectionMode(QAbstractItemView::NoSelection);
              iv->clearSelection();
              iv->setCurrentIndex(QModelIndex());
            }
            // Vegen op de lijst schuift het blad (touch_ui.cpp kijkt naar ocpnios_flat);
            // de lijst pakte de veeg zelf en het blad bleef staan (11 nov 2026).
            if (QWidget* vp = sa->viewport()) {
              vp->setProperty("ocpnios_flat", true);
              if (vp->property("ocpnios_kinetic").toBool()) {
                QScroller::ungrabGesture(vp);
                vp->setProperty("ocpnios_kinetic", false);
              }
            }
          }
        }
      }
      visit(c);
    }
  };
  visit(root);
}

// Rand die links/rechts al gold (zie ook SideBorder verderop).
int SideBorderOf(wxSizerItem* it) {
  return (it->GetFlag() & (wxLEFT | wxRIGHT)) ? it->GetBorder() : 0;
}

// Knoppen en schakelaars die tegen de schermrand staan (geen rand aan die kant): ruimte
// ertussen. o-charts winkelpaneel: "Kaartenlijst vernieuwen" en "Toon kaartinformatie"
// rechts tegen de rand, "Toon verlopen kaarten" links (iPhone 11 nov 2026). Ook voor
// pagina's in Instellingen (options.cpp, IosFitPage).
void ocpnios_EdgeMargins(wxWindow* root) {
  if (!root) return;
  const int client = root->GetClientSize().x;
  if (client < 100) return;
  auto rootRect = [root](wxWindow* c) {
    wxPoint p(0, 0);
    for (wxWindow* w = c; w && w != root; w = w->GetParent()) p += w->GetPosition();
    return wxRect(p, c->GetSize());
  };
  bool moved = false;
  std::set<wxWindow*> parents;
  ForEachSizer(root, [&](wxSizer* sz) {
    for (wxSizerItem* it : sz->GetChildren()) {
      if (!it->IsWindow() || !it->IsShown()) continue;
      wxWindow* w = it->GetWindow();
      if (!dynamic_cast<wxButton*>(w) && !dynamic_cast<wxCheckBox*>(w)) continue;
      const wxRect r = rootRect(w);
      int f = it->GetFlag();
      const int b = SideBorderOf(it);
      const int right = client;
      // Ook een te kleine rand aan die kant (o-charts: wxRIGHT met 5 pt).
      if (r.GetRight() > right - 14 && (!(f & wxRIGHT) || b < 16)) f |= wxRIGHT;
      else if (r.x < 6 && (!(f & wxLEFT) || b < 16)) f |= wxLEFT;
      else continue;
      it->SetFlag(f);
      it->SetBorder(std::max(b, 16));  // gelijk met de kaarten (kCardSide)
      parents.insert(w->GetParent());
      moved = true;
    }
  });
  if (!moved) return;
  for (wxWindow* p : parents) p->Layout();
  root->Layout();
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
}

void ocpnios_CenterRowItems(wxWindow* root) { CenterRowItems(root); }

void ocpnios_SheetLook(wxWindow* root);  // verderop
void ocpnios_EdgeMargins(wxWindow* root);
void ocpnios_FitColumns(QWidget* q);      // verderop

static void RadioGrids(wxWindow* root);
void ocpnios_FitToWidth(wxWindow* root) {
  if (!root) return;
  constexpr int kMargin = 6;
  // Eerst terug naar de oorspronkelijke maten: liggend is er meer ruimte dan staand.
  Restore(root);
  RestoreSizers(root);
  CenterRowItems(root);
  CollapseStretch(root);
  FlattenInnerScroll(root);
  RadioGrids(root);  // before wrapping: one radio button per row needs no wrap
  // Opnieuw indelen, van binnen naar buiten.
  std::function<void(wxWindow*)> relayout = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren())
      if (!c->IsTopLevel() && IsContainer(c)) relayout(c);
    // wx onthoudt de beste maat van een paneel; na kleinere kinderen opnieuw rekenen
    // (Dashboard: tabblad-pagina bleef 775 pt terwijl de inhoud 414 paste).
    w->InvalidateBestSize();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(w)) {
      sw->Layout();
      sw->FitInside();
    } else if (w->GetSizer()) {
      w->Layout();
    }
  };
  relayout(root);
  // Posities ten opzichte van het venster zelf (som van GetPosition), niet op het
  // scherm: verborgen tabbladen hebben geen bruikbare schermpositie, maar bepalen wel
  // mee hoe breed het tabblad-element wordt (Dashboard: tab "Lettertypes", 593 pt).
  auto rootRect = [root](wxWindow* c) {
    wxPoint p(0, 0);
    for (wxWindow* w = c; w && w != root; w = w->GetParent()) p += w->GetPosition();
    return wxRect(p, c->GetSize());
  };
  // Grens = rechterrand van het venster minus ruimte voor een schuifbalk. Niet de
  // breedte van de ouder: een paneel kan zelf al te breed zijn (Dashboard: 719 pt).
  const int limit = root->GetClientSize().x - 14;
  bool anyOver = false;
  std::function<void(wxWindow*)> findOver = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel() || !c->IsShown()) continue;
      if (rootRect(c).GetRight() > limit) anyOver = true;
      if (IsContainer(c)) findOver(c);
    }
  };
  findOver(root);
  // Vaste minimumbreedtes van teksten die groter zijn dan nodig (Dashboard: vinkje
  // "Toon dit paneel" 240 pt) bepalen de breedte van een hele kolom: loslaten.
  std::function<void(wxWindow*)> release = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      // Nodige breedte zelf meten: Qt maakt de gevraagde maat gelijk aan het minimum.
      if (IsTextLike(c) && c->GetMinSize().x > 0) {
        int need = 0;
        for (const wxString& line : wxSplit(c->GetLabel(), '\n', '\0'))
          need = std::max(need, TextWidth(c, line));
        if (!dynamic_cast<wxStaticText*>(c)) need += 70;
        if (c->GetMinSize().x > need + 8) SetMinWidth(c, -1);
      }
      // Knoppen met een vaste, te kleine breedte (Tactics: wxSize(20,-1) voor
      // "Toevoegen"): minstens hun tekst plus de opvulling van de iOS-stijl.
      if (auto* b = dynamic_cast<wxButton*>(c)) {
        const int need = TextWidth(b, b->GetLabel()) + 34;
        if (!b->GetLabel().empty() && b->GetSize().x < need) {
          QWidget* q = b->GetHandle();
          if (q && !q->property(kOrigMinProp).isValid())
            q->setProperty(kOrigMinProp, b->GetMinSize().x);
          b->SetMinSize(wxSize(need, b->GetMinSize().y));
          b->InvalidateBestSize();
        }
      }
      // Invulvakken en keuzelijsten met een vaste desktopmaat (Raymarine: 40x20 pt):
      // te laag voor de iOS-opvulling, de tekst viel weg. Minstens Qt's maat.
      if ((dynamic_cast<wxTextCtrl*>(c) || dynamic_cast<wxChoice*>(c)) && c->GetHandle()) {
        const QSize hint = c->GetHandle()->sizeHint();
        const wxSize m = c->GetMinSize();
        const wxSize cur = c->GetSize();
        int h = m.y, w = m.x;
        if (cur.y < hint.height() - 2) h = hint.height();
        if (auto* tc = dynamic_cast<wxTextCtrl*>(c)) {
          // Minstens 4 tekens, en de huidige inhoud moet passen (Raymarine: "$STALK" in
          // een vak van 40 pt toonde "ALK").
          const int need = std::max(4 * GetCharW(c), TextWidth(c, tc->GetValue()) + 24);
          if (cur.x < need && !tc->IsMultiLine()) w = need;
        }
        if (h != m.y || w != m.x) {
          QWidget* q = c->GetHandle();
          if (!q->property(kOrigMinProp).isValid()) q->setProperty(kOrigMinProp, m.x);
          c->SetMinSize(wxSize(w, h));
          c->SetSize(std::max(cur.x, w), std::max(cur.y, h));
        }
      }
      if (IsContainer(c)) release(c);
    }
  };
  // Vaste minimumhoogte van rijen (Raymarine: 40 pt) loslaten: na afbreken is een
  // label hoger en overlapte de keuzelijst ernaast.
  ForEachSizer(root, [](wxSizer* sz) {
    if (sz->GetMinSize().y > 0 && sz->GetMinSize().x <= 0) sz->SetMinSize(wxSize(-1, -1));
  });
  release(root);
  // Knop met een tekst breder dan het scherm: over twee regels op een woordgrens. Qt kan
  // een knoptekst niet zelf afbreken; de knop liep rechts buiten beeld (o-charts
  // Voorkeuren, "Eindgebruikers-licentieovereenkomst (EULA) weergeven", iPhone 11 nov 2026).
  {
    const int maxW = limit - 2 * kMargin - 24;
    std::function<void(wxWindow*)> wrapBtn = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        auto* b = dynamic_cast<wxButton*>(c);
        const wxString lab = b ? b->GetLabel() : wxString();
        // In een raster van twee knopkolommen is de helft beschikbaar (Routebeheer ›
        // Markeringen: "Verstuur naar 2e OpenCPN" maakte de rechterkolom te breed en werd
        // afgesneden, iPhone 12 nov 2026).
        int bw = maxW;
        if (b)
          if (auto* fgs = dynamic_cast<wxFlexGridSizer*>(b->GetContainingSizer()))
            if (fgs->GetCols() == 2) bw = (maxW - 3 * kMargin) / 2;
        if (b && !lab.empty() && !lab.Contains("\n") &&
            TextWidth(b, lab) + 34 > bw) {
          // Breekpunt: de spatie waarbij de langste regel het kortst is.
          size_t best = wxString::npos;
          int bestW = 1 << 30;
          for (size_t i = 0; i < lab.length(); i++) {
            if (lab[i] != ' ') continue;
            const int w1 = std::max(TextWidth(b, lab.Left(i)), TextWidth(b, lab.Mid(i + 1)));
            if (w1 < bestW) { bestW = w1; best = i; }
          }
          if (best != wxString::npos && bestW + 34 <= bw) {
            if (!b->GetName().StartsWith("ocpnios:")) b->SetName("ocpnios:" + lab);
            b->SetLabel(lab.Left(best) + "\n" + lab.Mid(best + 1));
            if (QWidget* q = b->GetHandle())
              if (!q->property(kOrigMinProp).isValid())
                q->setProperty(kOrigMinProp, b->GetMinSize().x);
            b->SetMinSize(wxSize(bestW + 34, -1));
            b->InvalidateBestSize();
          }
        }
        wrapBtn(c);
      }
    };
    wrapBtn(root);
  }
  FitSplitters(root, limit);
  relayout(root);
  if (SwitchGridsToColumn(root, limit)) relayout(root);
  anyOver = false;
  findOver(root);
  if (!anyOver) {
    LockChoiceWidths(root, limit);
    relayout(root);
    ocpnios_SheetLook(root);
    return;
  }
  // Blokken naast elkaar die niet passen: onder elkaar.
  if (ReflowSections(root, limit)) relayout(root);
  // Rijen met knoppen of velden die te breed zijn (vóór het afbreken van teksten:
  // anders braken labels al op een smalle breedte, "Graadverdeli ng") (Route- en
  // markbeheer: "Importeer GPX-bestand…" naast "Exporteer wat zichtbaar is"):
  // onder elkaar.
  bool reflowed = false;
  std::vector<wxBoxSizer*> toWrap;
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxHORIZONTAL) return;
    // Werkelijke breedte van de rij (zichtbare elementen), niet CalcMin: wx' minimum
    // van een knop kan veel groter zijn dan wat er staat (Datamonitor: rij van vijf
    // knoppen werd onterecht onder elkaar gezet, 10 nov 2026).
    int rowW = 0;
    for (wxSizerItem* it : bs->GetChildren()) {
      if (!it->IsShown()) continue;
      int bw = 0;
      if (it->GetFlag() & wxLEFT) bw += it->GetBorder();
      if (it->GetFlag() & wxRIGHT) bw += it->GetBorder();
      if (it->IsWindow())
        rowW += it->GetWindow()->GetBestSize().x + bw;
      else if (!it->IsSpacer() || it->GetProportion() == 0)
        rowW += it->CalcMin().x + bw;
    }
    if (rowW <= limit) return;
    int windows = 0;
    for (wxSizerItem* it : bs->GetChildren())
      if (it->IsWindow() && it->GetWindow()->IsShown()) windows++;
    if (windows < 2) return;
    // Veel losse elementen in een gewone rij: laten doorlopen (WrapRow, na de
    // doorloop; de boom niet wijzigen tijdens het doorlopen).
    if (windows >= 3 && !dynamic_cast<wxStaticBoxSizer*>(bs) &&
        !dynamic_cast<wxStdDialogButtonSizer*>(bs) && !dynamic_cast<wxWrapSizer*>(bs)) {
      toWrap.push_back(bs);
      return;
    }
    if (!g_origLayout.count(sz)) g_origLayout[sz] = wxHORIZONTAL;
    bs->SetOrientation(wxVERTICAL);
    reflowed = true;
  });
  for (wxBoxSizer* bs : toWrap) WrapRow(root, bs);
  if (reflowed || !toWrap.empty()) relayout(root);
  // Elke pagina van een tabblad-element op de doelbreedte zetten en indelen, zodat ook
  // verborgen pagina's echte posities krijgen.
  std::function<void(wxWindow*)> sizePages = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      if (auto* book = dynamic_cast<wxBookCtrlBase*>(c)) {
        const int target = limit - rootRect(book).x - 10;
        for (size_t i = 0; i < book->GetPageCount(); i++) {
          wxWindow* page = book->GetPage(i);
          // Ook smallere pagina's: een verborgen pagina stond op 186 pt (nooit door
          // het tabblad-element ingedeeld) en gaf zo geen bruikbare posities.
          if (target > 100 && page->GetSize().x != target) {
            page->SetSize(target, page->GetSize().y);
            page->Layout();
          }
        }
      }
      if (IsContainer(c)) sizePages(c);
    }
  };
  for (int round = 0; round < 6; round++) {
    bool changed = false;
    sizePages(root);
    std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        // Pagina's van een tabblad tellen mee, ook als ze verborgen zijn.
        if (!c->IsShown() && !dynamic_cast<wxBookCtrlBase*>(w)) continue;
        const wxRect r = rootRect(c);
        const int x = std::max(r.x, 0);
        // Tekst die wx in de rij heeft samengeknepen (VDR: "knopen" 2 pt breed,
        // "minuten snelheid onder" 173 van 285 pt) valt niet buiten de rand maar wordt
        // afgekapt. Ook die meenemen.
        int squeeze = 0;
        if (IsTextLike(c) && !c->GetLabel().empty() && c->GetHandle()) {
          // Qt's eigen maat voor de huidige tekst (incl. schakelaar of rondje). Bij een
          // QLabel met woordafbreking is de sizeHint al afgebroken en te smal; dan de
          // langste regel in de Qt-letter (WeatherRouting-importvraag: regels van 399 pt
          // in een kolom van 350, rechts afgekapt, 11 nov 2026).
          int need = c->GetHandle()->sizeHint().width();
          if (dynamic_cast<wxStaticText*>(c))
            for (const wxString& line : wxSplit(c->GetLabel(), '\n', '\0'))
              need = std::max(need, TextWidth(c, line) + 4);
          if (r.width < need - 4) squeeze = need - r.width;
        }
        if (squeeze > 0 && r.GetRight() <= limit - kMargin) {
          // Ruimte tot de rechterrand, min wat er rechts naast staat. Niet de breedte die
          // wx toevallig gaf: die kon klein zijn ("AUTO-COG-fu nctie", Raymarine).
          int rightOf = 0;
          for (wxWindow* sib : c->GetParent()->GetChildren()) {
            if (sib == c || sib->IsTopLevel() || !sib->IsShown()) continue;
            if (dynamic_cast<wxStaticBox*>(sib)) continue;
            const wxRect sr = rootRect(sib);
            if (sr.x >= r.GetRight() - 2 && sr.y < r.GetBottom() && sr.GetBottom() > r.y)
              rightOf = std::max(rightOf, sr.GetRight() - r.GetRight());
          }
          const int room = std::max(r.width, limit - kMargin - std::max(r.x, 0) - rightOf);
          if (room >= 60) {
            changed |= FitText(c, room);
          } else if (c->GetParent()) {
            // Te smal om af te breken: iets links ervan in de rij smaller maken.
            wxWindow* widest = nullptr;
            wxRect wr;
            for (wxWindow* sib : c->GetParent()->GetChildren()) {
              if (sib == c || sib->IsTopLevel() || !sib->IsShown() || IsContainer(sib)) continue;
              const wxRect sr = rootRect(sib);
              if (sr.GetRight() <= r.x + 4 && sr.y < r.GetBottom() && sr.GetBottom() > r.y &&
                  IsTextLike(sib) && (!widest || sr.width > wr.width)) {
                widest = sib;
                wr = sr;
              }
            }
            if (widest) changed |= FitText(widest, wr.width - squeeze);
          }
        }
        if (r.GetRight() > limit - kMargin) {
          const int avail = limit - kMargin - x;
          // Begint het element zo ver rechts dat het niet meer past, dan moet iets links
          // ervan in dezelfde rij smaller (Dashboard: invulvak rechts naast het vinkje
          // "Use internal Sumlog").
          const int needed = IsTextLike(c) ? 130 : 40;
          if (avail < needed && c->GetParent()) {
            const int over = r.GetRight() - (limit - kMargin);
            // Laatste redmiddel: het element zelf smaller (invulvak, keuzelijst).
            if (!IsTextLike(c) && !IsContainer(c) && r.width > 80) {
              const int w = std::max(60, r.width - over);
              if (c->GetMinSize().x != w) {
                SetMinWidth(c, w);
                changed = true;
              }
            }
            // Alles links ervan smaller maken, ook op andere rijen: in een raster bepaalt
            // het breedste element de kolom (Dashboard: het vinkje "Gebruik N2K SignalK"
            // op de volgende rij duwde het sumlog-vak buiten beeld).
            for (wxWindow* sib : c->GetParent()->GetChildren()) {
              if (sib == c || sib->IsTopLevel() || !sib->IsShown() || IsContainer(sib)) continue;
              // Alleen in hetzelfde raster: Raymarine hangt alles direct aan de dialoog,
              // en labels op andere rijen werden zo onnodig smal ("AUTO-COG-fu nctie").
              if (sib->GetContainingSizer() != c->GetContainingSizer()) continue;
              const wxRect sr = rootRect(sib);
              if (sr.GetRight() > r.x + 4 || sr.width <= 100) continue;
              const int target = sr.width - over;
              if (IsTextLike(sib))
                changed |= FitText(sib, target);
              else if (target >= 40 && sib->GetMinSize().x != target) {
                SetMinWidth(sib, target);
                changed = true;
              }
            }
          }
          if (IsTextLike(c)) {
            changed |= FitText(c, avail);
          } else if (!IsContainer(c)) {
            // Wat rechts ernaast in dezelfde rij staat (bv. de knoppenkolom naast de
            // instrumentenlijst van Dashboard) moet er ook nog naast passen.
            int rightOf = 0;
            for (wxWindow* sib : c->GetParent()->GetChildren()) {
              if (sib == c || sib->IsTopLevel() || !sib->IsShown()) continue;
              if (dynamic_cast<wxStaticBox*>(sib)) continue;
              const wxRect sr = rootRect(sib);
              if (sr.x >= r.x + r.width / 2 && sr.y < r.GetBottom() && sr.GetBottom() > r.y)
                rightOf = std::max(rightOf, sr.GetRight() - r.GetRight());
            }
            const int w = avail - std::max(0, rightOf);
            if (w >= 40 && c->GetMinSize().x != w) {
              SetMinWidth(c, w);
              changed = true;
            }
          } else {
            // Vaste minimumbreedte van een paneel loslaten; de kinderen bepalen het.
            if (c->GetMinSize().x > 0) {
              SetMinWidth(c, -1);
              changed = true;
            }
          }
        }
        if (IsContainer(c)) visit(c);
      }
    };
    visit(root);
    relayout(root);
    if (!changed) break;
  }
  // Containers die nog te breed zijn (wx rekende voor een Dashboard-pagina 775 pt terwijl
  // de inhoud op 414 paste): breedte vastleggen op wat er is. Een vaste minimumbreedte
  // gaat in wx vóór de berekende "beste" maat.
  std::function<void(wxWindow*)> clamp = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel() || !IsContainer(c)) continue;
      const wxRect r = rootRect(c);
      const int avail = limit - std::max(r.x, 0);
      if (avail > 100 && (r.GetRight() > limit || c->GetBestSize().x > avail))
        SetMinWidth(c, avail);
      clamp(c);
    }
  };
  clamp(root);
  relayout(root);
  LockChoiceWidths(root, limit);
  relayout(root);
  // Niet horizontaal schuiven (op een telefoon onverwacht): terug naar links.
  std::function<void(wxWindow*)> unscroll = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(c)) {
        int vx, vy;
        sw->GetViewStart(&vx, &vy);
        if (vx) sw->Scroll(0, vy);
      }
      unscroll(c);
    }
  };
  unscroll(root);
  ocpnios_EdgeMargins(root);
  // Hoogte van uitgevouwen schuifvlakken opnieuw: na het afbreken van teksten zijn de
  // regels hoger (laatste regel half zichtbaar, WR-instellingen 11 nov 2026).
  FlattenInnerScroll(root);
  relayout(root);
  ocpnios_SheetLook(root);
  // De formulierregels (in SheetLook) veranderen de regelhoogtes nog: daarna de hoogte
  // van uitgevouwen schuifvlakken nog een keer (lege strook onderin, 11 nov 2026).
  FlattenInnerScroll(root);
  relayout(root);
}

// ---------------------------------------------------------------------------------
// iOS-secties voor rasters "kop | onderdelen" (Instellingen > Weergaveopties >
// Algemeen, Eenheden): links een label, rechts een groep schakelaars. Op iOS: het label
// als grijze kop, de groep eronder op een witte afgeronde kaart (Xander, 10 nov 2026:
// "twee designstijlen door elkaar"). De kaarten worden achter de onderdelen getekend.
// ---------------------------------------------------------------------------------
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QRadioButton>
#include <QTabBar>
#include <QTabWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QHeaderView>
#include <QTreeView>
#include <QPushButton>
#include <QPalette>
#include <QEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionButton>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QWidget>
#include <QCoreApplication>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QFontMetrics>
#include <QIcon>
#include <QAbstractButton>
#include <QPixmap>
#include <QStyleOptionComboBox>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QImage>
#include <QTimer>
#include <set>
#include <climits>

namespace {

constexpr int kCardSide = 16;   // kaart t.o.v. de rand
constexpr int kCardInner = 14;  // onderdelen t.o.v. de kaart

std::set<wxSizer*> g_sectionGrids;

class CardPainter : public QObject {
 public:
  explicit CardPainter(QWidget* target) : QObject(target), m_target(target) {}
  struct Card {
    wxSizerItem* item = nullptr;  // groep onder een kop
    wxSizer* sizer = nullptr;     // of een heel formulier
    wxWindow* any = nullptr;      // venster erin, voor de coördinaten
    std::vector<wxWindow*> rows;  // schakelaarregels in de groep (lijntjes ertussen)
  };
  std::vector<Card> cards;

  void Watch(wxWindow* w) {
    QWidget* q = w ? w->GetHandle() : nullptr;
    if (!q || m_watched.count(q)) return;
    m_watched.insert(q);
    q->installEventFilter(this);
  }

  bool eventFilter(QObject* obj, QEvent* ev) override {
    // Scrollen verschuift de onderdelen, niet het vlak waarop de kaarten en lijntjes
    // getekend worden; zonder opnieuw tekenen bleven de lijntjes staan en schoof de tekst
    // eroverheen (iPhone, 10 nov 2026). Bij elke verplaatsing van een onderdeel opnieuw.
    if (obj != m_target) {
      if (ev->type() == QEvent::Move) m_target->update();
      return false;
    }
    if (ev->type() != QEvent::Paint) return false;
    for (const auto& c : cards) {
      for (wxWindow* w : c.rows) Watch(w);
      Watch(c.any);
    }
    QPainter p(m_target);
    p.fillRect(m_target->rect(), m_target->palette().window());
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = QCoreApplication::instance() &&
                      QCoreApplication::instance()->property("ocpnios_dark").toBool();
    const QColor card = dark ? QColor(0x1c, 0x1c, 0x1e) : QColor(Qt::white);
    for (const auto& c : cards) {
      wxWindow* any = c.any;
      if (!any || !any->GetHandle()) continue;
      if (c.item && !c.item->IsShown()) continue;
      // Van sizer- naar Qt-coördinaten via een venster in de groep.
      // Verschuiving = waar het venster nu echt staat min waar de sizer het neerzette;
      // na scrollen verschuift alleen het eerste (lijntjes bleven staan bij Eenheden,
      // iPhone 10 nov 2026).
      QPoint off = any->GetHandle()->pos() -
                   QPoint(any->GetPosition().x, any->GetPosition().y);
      if (wxSizer* cs = any->GetContainingSizer())
        if (wxSizerItem* ai = cs->GetItem(any))
          off = any->GetHandle()->mapTo(m_target, QPoint(0, 0)) -
                QPoint(ai->GetRect().x, ai->GetRect().y);
      const wxRect r = c.item ? c.item->GetRect()
                              : wxRect(c.sizer->GetPosition(), c.sizer->GetSize());
      // Groep: de kaart steekt 8 pt boven en onder uit; formulier: de regels hebben al
      // ruimte (rand) binnen het raster.
      const int pad = c.item ? 8 : 14;
      QRectF rr(r.x + off.x() - kCardInner, r.y + off.y() - pad, 0, r.height + 2 * pad);
      rr.setRight(m_target->width() - kCardSide);
      rr.setLeft(kCardSide);
      QPainterPath path;
      path.addRoundedRect(rr, 10, 10);
      p.fillPath(path, card);
      // Groep met schakelaarregels: lijntje tussen twee opeenvolgende regels.
      {
        const QPen line(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.6);
        QWidget* prev = nullptr;
        for (wxWindow* rw : c.rows) {
          QWidget* q = rw && rw->IsShown() ? rw->GetHandle() : nullptr;
          if (!q || q->parentWidget() != m_target) continue;
          if (prev) {
            const double ly = (prev->geometry().bottom() + q->geometry().top()) / 2.0 + 1;
            p.setPen(line);
            p.drawLine(QPointF(q->geometry().left(), ly), QPointF(rr.right(), ly));
          }
          prev = q;
        }
      }
      // Formulier: dunne lijn tussen de regels, vanaf de tekst (zoals iOS). Per regel de
      // bovenkant en onderkant uit de onderdelen zelf.
      if (auto* fg = dynamic_cast<wxFlexGridSizer*>(c.sizer)) {
        std::vector<std::pair<int, int>> rowsY;  // boven, onder
        const int cols = std::max(1, fg->GetEffectiveColsCount());
        int i = 0, top = INT_MAX, bot = INT_MIN;
        for (wxSizerItem* it : fg->GetChildren()) {
          if (it->IsShown() && it->GetRect().height > 0) {
            top = std::min(top, it->GetRect().y);
            bot = std::max(bot, it->GetRect().GetBottom());
          }
          if (++i % cols == 0) {
            if (top != INT_MAX) rowsY.push_back({top, bot});
            top = INT_MAX;
            bot = INT_MIN;
          }
        }
        p.setPen(QPen(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.6));
        for (size_t k = 1; k < rowsY.size(); k++) {
          const double ly = (rowsY[k - 1].second + rowsY[k].first) / 2.0 + off.y() + 0.5;
          p.drawLine(QPointF(kCardSide + kCardInner, ly), QPointF(rr.right(), ly));
        }
      }
    }
    return true;
  }

 private:
  QWidget* m_target;
  std::set<QObject*> m_watched;
};

wxWindow* FirstWindowIn(wxSizer* sz) {
  for (wxSizerItem* it : sz->GetChildren()) {
    if (it->IsWindow()) return it->GetWindow();
    if (it->IsSizer())
      if (wxWindow* w = FirstWindowIn(it->GetSizer())) return w;
  }
  return nullptr;
}

// Schakelaarregel zoals de Instellingen-app van iOS: tekst links, schakelaar rechts
// tegen de kaartrand, de hele regel aantikbaar (Xander, 10 nov 2026: "Rechts, zoals
// iOS"). Het stylesheet kan de schakelaar wel rechts zetten, maar laat de tekst dan
// weg (QStyleSheetStyle zet de tekst altijd ná de schakelaar), dus zelf tekenen.
class RowSwitch : public QObject {
 public:
  // De regel mag smaller worden dan tekst + schakelaar op één regel: de tekst breekt in
  // paint() zelf af. Anders werd de pagina breder dan het scherm en schoof Eenheden
  // horizontaal (iPhone, 11 nov 2026).
  explicit RowSwitch(QObject* parent) : QObject(parent) {
    if (auto* q = qobject_cast<QWidget*>(parent)) {
      q->setMinimumWidth(0);
      if (wxWindow* w = wxWindow::QtRetrieveWindowPointer(q))
        w->SetMinSize(wxSize(std::min(w->GetBestSize().x, 120), w->GetMinSize().y));
    }
  }
  bool eventFilter(QObject* obj, QEvent* ev) override {
    auto* cb = qobject_cast<QCheckBox*>(obj);
    if (!cb) return false;
    switch (ev->type()) {
      case QEvent::Paint: {
        QPainter p(cb);
        QStyleOptionButton opt;
        opt.initFrom(cb);
        opt.state |= cb->isChecked() ? QStyle::State_On : QStyle::State_Off;
        if (cb->isDown()) opt.state |= QStyle::State_Sunken;
        const QRect ind = cb->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, cb);
        const int iw = ind.width() > 0 ? ind.width() : 46;
        const int ih = ind.height() > 0 ? ind.height() : 28;
        opt.rect = QRect(cb->width() - iw - 6, (cb->height() - ih) / 2, iw, ih);
        cb->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &opt, &p, cb);
        p.setFont(cb->font());
        p.setPen(cb->palette().color(cb->isEnabled() ? QPalette::Active : QPalette::Disabled,
                                     QPalette::WindowText));
        const QRect tr(0, 0, cb->width() - iw - 18, cb->height());
        // Harde regelovergangen van het afbreken (fit_width: WrapWords) weg; hier breekt
        // de tekst zelf af op de breedte naast de schakelaar.
        QString text = cb->text();
        text.replace('\n', ' ');
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
        return true;
      }
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonDblClick:
        if (cb->isEnabled()) cb->setDown(true);
        return true;
      case QEvent::MouseButtonRelease: {
        const bool was = cb->isDown();
        cb->setDown(false);
        auto* me = static_cast<QMouseEvent*>(ev);
        if (was && cb->isEnabled() && cb->rect().contains(me->position().toPoint()))
          cb->click();
        return true;
      }
      default:
        return false;
    }
  }
};

// Keuzelijst als waarde in een formulierregel: de gekozen waarde rechts, tegen het
// pijltje, zoals de Instellingen-app van iOS. QComboBox zet de tekst altijd links en
// het stylesheet kent geen tekstuitlijning; de lijsten waren elk zo breed als hun
// langste keuze, dus de waarden stonden op wisselende plaatsen (Eenheden, Xander 11 nov
// 2026: "staan niet mooi uitgelijnd"). Zelf tekenen: rand en pijl door de stijl, tekst
// (en pictogram) rechts in het tekstvak.
class FormValueText : public QObject {
 public:
  explicit FormValueText(QObject* parent) : QObject(parent) {}
  bool eventFilter(QObject* obj, QEvent* ev) override {
    auto* cb = qobject_cast<QComboBox*>(obj);
    if (!cb || ev->type() != QEvent::Paint || cb->isEditable()) return false;
    // Weer een gewone keuzelijst gemaakt (verbindingseditor: label erboven).
    if (!cb->property("ocpnios_formvalue").toBool()) return false;
    QPainter p(cb);
    QStyleOptionComboBox opt;
    opt.initFrom(cb);
    opt.editable = false;
    opt.frame = true;
    opt.subControls = QStyle::SC_All;
    cb->style()->drawComplexControl(QStyle::CC_ComboBox, &opt, &p, cb);
    QRect tr = cb->style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                           QStyle::SC_ComboBoxEditField, cb);
    tr.adjust(0, 0, -4, 0);
    const QIcon icon = cb->itemIcon(cb->currentIndex());
    const QSize is = cb->iconSize();
    const int iconW = icon.isNull() ? 0 : is.width() + 8;
    const QFontMetrics fm(cb->font());
    const QString text = fm.elidedText(cb->currentText(), Qt::ElideRight,
                                       std::max(0, tr.width() - iconW));
    const int tw = fm.horizontalAdvance(text);
    QRect textR(tr.right() - tw + 1, tr.top(), tw, tr.height());
    if (iconW) {
      const QRect ir(textR.left() - iconW, tr.top() + (tr.height() - is.height()) / 2,
                     is.width(), is.height());
      icon.paint(&p, ir);
    }
    p.setFont(cb->font());
    p.setPen(cb->palette().color(cb->isEnabled() ? QPalette::Active : QPalette::Disabled,
                                 QPalette::Text));
    p.drawText(textR, Qt::AlignVCenter | Qt::AlignRight, text);
    return true;
  }
};

void MarkFormValue(QWidget* q) {
  auto* cb = qobject_cast<QComboBox*>(q);
  if (!cb) return;
  cb->setProperty("ocpnios_formvalue", true);
  if (cb->property("ocpnios_formtext").toBool()) return;
  cb->setProperty("ocpnios_formtext", true);
  cb->installEventFilter(new FormValueText(cb));
  // Vult de kolom (de tekst staat toch rechts): zo staan alle pijltjes onder elkaar.
  // Elke lijst had zijn eigen breedte en Qt kortte sommige in zonder dat wx het wist; de
  // pijltjes sprongen heen en weer (Eenheden, 11 nov 2026).
  if (wxWindow* w = wxWindow::QtRetrieveWindowPointer(cb)) {
    w->SetMinSize(wxSize(80, w->GetMinSize().y));
    w->SetMaxSize(wxSize(-1, -1));
    cb->setMinimumWidth(0);
    cb->setMaximumWidth(QWIDGETSIZE_MAX);
    if (wxSizer* cs = w->GetContainingSizer())
      if (wxSizerItem* si = cs->GetItem(w))
        si->SetFlag((si->GetFlag() | wxEXPAND) & ~(wxALIGN_RIGHT | wxALIGN_CENTER_HORIZONTAL));
  }
  cb->update();
}

// Rechterrand voor een waarde in een regel binnen sizer `parent` (item `holder` is de
// regel of het raster): de rechterkant van de waarde gelijk aan die van de schakelaars
// in dezelfde groep. Een schakelaarregel tekent de schakelaar 6 pt binnen zijn rand
// (RowSwitch).
int ValueRightBorder(wxSizer* parent, wxSizerItem* holder) {
  int ref = -1;
  for (wxSizerItem* c : parent->GetChildren())
    if (c != holder && c->IsWindow() && dynamic_cast<wxCheckBox*>(c->GetWindow()) &&
        (c->GetFlag() & wxRIGHT)) {
      ref = c->GetBorder();
      break;
    }
  const int own = (holder->GetFlag() & wxRIGHT) ? holder->GetBorder() : 0;
  if (ref < 0) ref = own;
  return std::max(0, 6 + ref - own);
}

// Schakelaars direct in een verticale groep: over de volle breedte, als regel.
void MakeSwitchRows(wxSizer* sz, std::vector<wxWindow*>& rows) {
  auto* bs = dynamic_cast<wxBoxSizer*>(sz);
  auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
  // Een raster met één kolom (op de iPhone smal gemaakt, Routes/markeringspunten) telt
  // ook als regels onder elkaar.
  const bool column = (bs && bs->GetOrientation() == wxVERTICAL) ||
                      (fg && fg->GetEffectiveColsCount() == 1);
  if (!column) return;
  std::vector<wxSizerItem*> items(sz->GetChildren().begin(), sz->GetChildren().end());
  for (wxSizerItem* it : items) {
    if (it->IsSizer()) {
      // Regel "label  [veld]" (Kruissnelheid): over de volle breedte, het veld rechts.
      auto* hs = dynamic_cast<wxBoxSizer*>(it->GetSizer());
      if (hs && hs->GetOrientation() == wxHORIZONTAL && !dynamic_cast<wxStaticBoxSizer*>(hs)) {
        std::vector<wxWindow*> wins;
        bool stretch = false;
        for (wxSizerItem* c : hs->GetChildren()) {
          if (c->IsWindow() && c->IsShown()) wins.push_back(c->GetWindow());
          else if (c->IsSizer()) wins.push_back(nullptr);
          if (c->GetProportion() > 0) stretch = true;
        }
        if (wins.size() == 2 && wins[0] && wins[1] &&
            dynamic_cast<wxStaticText*>(wins[0]) && !dynamic_cast<wxStaticText*>(wins[1]) &&
            !wins[0]->GetHandle()->property("ocpnios_rowlabel").toBool()) {
          wins[0]->GetHandle()->setProperty("ocpnios_rowlabel", true);
          it->SetFlag((it->GetFlag() | wxEXPAND) &
                      ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
          // Regel zo hoog als een schakelaarregel (44 pt) en het label zo hoog als het
          // veld: het lijntje erboven liep door het veld (Kruissnelheid, 11 nov 2026).
          // Het veld rechts gelijk met de schakelaars erboven.
          wins[0]->SetMinSize(wxSize(wins[0]->GetMinSize().x,
                                     std::max({44, wins[1]->GetBestSize().y,
                                               wins[0]->GetBestSize().y})));
          if (auto* ql = qobject_cast<QLabel*>(wins[0]->GetHandle()))
            ql->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
          for (wxSizerItem* c : hs->GetChildren())
            if (c->IsWindow() && c->GetWindow() == wins[1]) {
              c->SetFlag((c->GetFlag() | wxRIGHT | wxALIGN_CENTER_VERTICAL) &
                         ~(wxLEFT | wxTOP | wxBOTTOM));
              c->SetBorder(ValueRightBorder(sz, it));
            } else if (c->IsWindow() && c->GetWindow() == wins[0]) {
              c->SetFlag(c->GetFlag() | wxALIGN_CENTER_VERTICAL);
            }
          if (wins[1]->GetHandle()) MarkFormValue(wins[1]->GetHandle());
          if (!stretch) hs->Insert(1, 0, 0, 1);
          rows.push_back(wins[0]);
          continue;
        }
      }
      MakeSwitchRows(it->GetSizer(), rows);
      // Blok met schakelaarregels dat zonder wxEXPAND in de kolom staat (Uitgebreid:
      // OpenGL-blok met vlag 0): de regels krimpen dan tot hun minimum en de tekst valt
      // weg ("Hardwarevers", iPhone 11 nov 2026). Het blok over de volle breedte.
      auto* vs = dynamic_cast<wxBoxSizer*>(it->GetSizer());
      if (vs && vs->GetOrientation() == wxVERTICAL && !(it->GetFlag() & wxEXPAND))
        for (wxSizerItem* c : vs->GetChildren())
          if (c->IsWindow() && c->GetWindow()->GetHandle() &&
              c->GetWindow()->GetHandle()->property("ocpnios_row").toBool()) {
            it->SetFlag((it->GetFlag() | wxEXPAND) &
                        ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
            break;
          }
      // Zelfde voor een raster met één kolom (GRIB › Voorkeuren: schakelaars in een
      // wxFlexGridSizer(0, 1)): zonder meegroeiende kolom bleef die op het minimum van
      // de regels (120 pt) en brak de tekst per woord af ("Gebru/hoge/grafisc", iPad
      // 11 nov 2026). Kolom laten groeien en het raster over de volle breedte.
      auto* cg = dynamic_cast<wxFlexGridSizer*>(it->GetSizer());
      if (cg && cg->GetEffectiveColsCount() == 1)
        for (wxSizerItem* c : cg->GetChildren())
          if (c->IsWindow() && c->GetWindow()->GetHandle() &&
              c->GetWindow()->GetHandle()->property("ocpnios_row").toBool()) {
            if (!cg->IsColGrowable(0)) cg->AddGrowableCol(0);
            it->SetFlag((it->GetFlag() | wxEXPAND) &
                        ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
            break;
          }
      continue;
    }
    auto* cb = it->IsWindow() ? dynamic_cast<wxCheckBox*>(it->GetWindow()) : nullptr;
    QWidget* q = cb ? cb->GetHandle() : nullptr;
    if (!q || q->property("ocpnios_row").toBool()) continue;
    q->setProperty("ocpnios_row", true);
    // Vaste maximale breedte (Raymarine: 680) laat de regel niet over de volle breedte
    // lopen; de schakelaar stond dan halverwege.
    if (cb->GetMaxSize().x > 0) cb->SetMaxSize(wxSize(-1, cb->GetMaxSize().y));
    q->setMaximumWidth(QWIDGETSIZE_MAX);
    // Vaste regelhoogte zoals iOS (44 pt): in de nachtstand werden de regels na het
    // wisselen van stylesheet lager dan overdag (Instellingen, 5.14.2, 10 nov 2026).
    cb->SetMinSize(wxSize(cb->GetMinSize().x, std::max(cb->GetMinSize().y, 44)));
    q->installEventFilter(new RowSwitch(q));
    q->style()->unpolish(q);
    q->style()->polish(q);
    it->SetFlag((it->GetFlag() | wxEXPAND) & ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
    rows.push_back(cb);
  }
}

}  // namespace

// Voor plugincode: een keuzelijst als waarde in een formulierregel (grijze tekst rechts,
// geen kader), zoals fit_width dat zelf doet bij "label | keuze" (GRIB Verwachtingsduur:
// op de iPad anders een breed wit vak, 13 nov 2026).
void ocpnios_MarkFormValue(wxWindow* w) {
  if (w && w->GetHandle()) MarkFormValue(w->GetHandle());
}

void ocpnios_GroupBoxCards(wxWindow* root);
void ocpnios_FitRowHeights(wxWindow* root);
void ocpnios_Segments(wxWindow* root);
void ocpnios_FixControls(wxWindow* root);
void ocpnios_PlainPageCard(wxWindow* root);
void ocpnios_FormGrids(wxWindow* root);

// Rasters "schakelaar | waarde" (AIS-objecten: "Geen CPA-waarschuwingen als ... |
// [20.0]", "Gebruik WPL ... | [keuzelijst]", "Speel geluid ... | [Test]"). Naast elkaar
// werd de tekst per woord afgebroken of overschreven door de waarde ernaast (Xander,
// 24 nov 2026). Zoals iOS: de schakelaar als regel over de volle breedte (tekst links,
// schakelaar rechts), de waarde op een eigen regel eronder rechts; een knop als eigen
// blauwe actieregel. Alleen plaatsen veranderen, de onderdelen blijven die van OpenCPN.
static std::set<wxSizer*> g_checkValueGrids;
void ocpnios_CheckValueGrids(wxWindow* root) {
  std::vector<wxFlexGridSizer*> grids;
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg || g_checkValueGrids.count(sz) || fg->GetCols() != 2) return;
    std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
    if (v.size() < 2 || v.size() % 2) return;
    bool any = false;
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
      wxWindow* l = v[i]->GetWindow();
      wxWindow* r = v[i + 1]->GetWindow();
      if (!l || !r) return;
      const bool check = dynamic_cast<wxCheckBox*>(l) != nullptr;
      const bool empty = dynamic_cast<wxStaticText*>(r) && r->GetLabel().Strip(wxString::both).empty();
      const bool value = dynamic_cast<wxTextCtrl*>(r) || dynamic_cast<wxChoice*>(r) ||
                         dynamic_cast<wxSpinCtrl*>(r) || dynamic_cast<wxButton*>(r);
      const bool emptyL = dynamic_cast<wxStaticText*>(l) && l->GetLabel().Strip(wxString::both).empty();
      if (check && (value || empty)) {
        if (value) any = true;
      } else if (!(emptyL && empty)) {
        return;  // another kind of row: not this pattern
      }
    }
    if (any) grids.push_back(fg);
  });
  for (wxFlexGridSizer* fg : grids) {
    g_checkValueGrids.insert(fg);
    std::vector<wxWindow*> wins;
    for (wxSizerItem* it : fg->GetChildren()) wins.push_back(it->GetWindow());
    for (wxWindow* w : wins) fg->Detach(w);
    fg->Clear(false);
    if (fg->IsColGrowable(1)) fg->RemoveGrowableCol(1);
    fg->SetCols(1);
    if (!fg->IsColGrowable(0)) fg->AddGrowableCol(0);
    for (size_t i = 0; i + 1 < wins.size(); i += 2) {
      wxWindow* l = wins[i];
      wxWindow* r = wins[i + 1];
      const bool emptyR = dynamic_cast<wxStaticText*>(r) && r->GetLabel().Strip(wxString::both).empty();
      if (!dynamic_cast<wxCheckBox*>(l)) {  // empty filler row
        l->Hide();
        r->Hide();
        continue;
      }
      fg->Add(l, 0, wxEXPAND | wxALL, 4);
      if (emptyR) {
        r->Hide();
      } else if (auto* b = dynamic_cast<wxButton*>(r)) {
        // An action of its own: a blue row over the full width.
        const bool dark = QCoreApplication::instance() &&
                          QCoreApplication::instance()->property("ocpnios_dark").toBool();
        b->SetMinSize(wxSize(-1, 44));
        b->SetMaxSize(wxSize(-1, -1));
        if (QWidget* q = b->GetHandle())
          q->setStyleSheet(QString("QPushButton { background: transparent; color: #007aff;"
                                   " border: none; text-align: left; padding: 10px 4px; font-weight: normal; }"
                                   "QPushButton:pressed { color: %1; }")
                               .arg(dark ? "#66aaff" : "#5aa0ff"));
        fg->Add(b, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
      } else if (dynamic_cast<wxChoice*>(r)) {
        // A choice under its switch over the full width ("APRS positierappc...").
        r->SetMinSize(wxSize(-1, r->GetMinSize().y));
        r->SetMaxSize(wxSize(-1, -1));
        fg->Add(r, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
      } else {
        // The value under its switch, on the right, as a form value.
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->AddStretchSpacer(1);
        r->SetMaxSize(wxSize(-1, -1));
        row->Add(r, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        fg->Add(row, 0, wxEXPAND | wxBOTTOM, 6);
      }
    }
  }
}

// Grids of radio buttons side by side (MMSI edit window: "Default tracking | Always
// track | Never track | Persistent", three columns): wider than a phone, the labels were
// wrapped and ran over the next radio button (Xander, 24 Nov 2026). When the row does
// not fit, one item per row.
static void RadioGrids(wxWindow* root) {
  const int room = root->GetClientSize().x;
  if (room < 100) return;
  bool changed = false;
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* gs = dynamic_cast<wxGridSizer*>(sz);
    if (!gs || dynamic_cast<wxFlexGridSizer*>(sz) || gs->GetCols() < 2) return;
    int radios = 0, width = 0, cols = gs->GetCols(), i = 0, rowW = 0;
    for (wxSizerItem* it : sz->GetChildren()) {
      wxWindow* w = it->IsWindow() ? it->GetWindow() : nullptr;
      if (!w) return;
      if (dynamic_cast<wxRadioButton*>(w)) radios++;
      else if (!dynamic_cast<wxCheckBox*>(w)) return;
      rowW += w->GetBestSize().x + 2 * it->GetBorder();
      if (++i % cols == 0) {
        width = std::max(width, rowW);
        rowW = 0;
      }
    }
    width = std::max(width, rowW);
    if (radios < 2 || width <= room - 40) return;
    gs->SetCols(1);
    gs->SetRows(0);
    for (wxSizerItem* it : sz->GetChildren()) it->SetFlag(it->GetFlag() | wxEXPAND);
    changed = true;
  });
  if (changed) root->Layout();
}

void ocpnios_SectionGrids(wxWindow* root) {
  RadioGrids(root);
  ocpnios_CheckValueGrids(root);
  std::vector<wxFlexGridSizer*> grids;
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
    if (!fg || g_sectionGrids.count(sz) || fg->GetCols() != 2) return;
    // Minstens één rij "label | sizer met onderdelen".
    std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
    bool pattern = false;
    for (size_t i = 0; i + 1 < v.size(); i += 2)
      if (v[i]->IsWindow() && dynamic_cast<wxStaticText*>(v[i]->GetWindow()) &&
          !v[i]->GetWindow()->GetLabel().empty() && v[i + 1]->IsSizer())
        pattern = true;
    if (pattern) grids.push_back(fg);
  });
  auto painterFor = [](wxWindow* any) -> CardPainter* {
    QWidget* target = any && any->GetHandle() ? any->GetHandle()->parentWidget() : nullptr;
    if (!target) return nullptr;
    auto* painter =
        static_cast<CardPainter*>(target->property("ocpnios_cards").value<QObject*>());
    if (!painter) {
      painter = new CardPainter(target);
      target->installEventFilter(painter);
      target->setProperty("ocpnios_cards", QVariant::fromValue<QObject*>(painter));
    }
    return painter;
  };
  for (wxFlexGridSizer* fg : grids) {
    g_sectionGrids.insert(fg);
    // Formulier: regels "label | keuzelijst/veld" (Eenheden). Zoals iOS: één kaart, label
    // links, waarde rechts; lege opvulregels weg.
    std::vector<wxSizerItem*> rows(fg->GetChildren().begin(), fg->GetChildren().end());
    bool form = false;
    for (size_t i = 0; i + 1 < rows.size(); i += 2)
      if (rows[i]->IsWindow() && dynamic_cast<wxStaticText*>(rows[i]->GetWindow()) &&
          !rows[i]->GetWindow()->GetLabel().empty() && rows[i + 1]->IsWindow() &&
          !dynamic_cast<wxStaticText*>(rows[i + 1]->GetWindow()))
        form = true;
    if (form) {
      wxWindow* any = nullptr;
      for (size_t i = 0; i + 1 < rows.size(); i += 2) {
        wxSizerItem* l = rows[i];
        wxSizerItem* r = rows[i + 1];
        const bool emptyL = l->IsSpacer() || (l->IsWindow() && dynamic_cast<wxStaticText*>(l->GetWindow()) &&
                                              l->GetWindow()->GetLabel().empty());
        const bool emptyR = r->IsSpacer() || (r->IsWindow() && dynamic_cast<wxStaticText*>(r->GetWindow()) &&
                                              r->GetWindow()->GetLabel().empty());
        if (emptyL && emptyR) {
          for (wxSizerItem* e : {l, r}) {
            if (e->IsWindow()) e->GetWindow()->Hide();
            else { e->SetFlag(0); e->SetMinSize(wxSize(0, 0)); }
          }
          continue;
        }
        // wx kent één randmaat per onderdeel: alleen links/rechts; de afstand tussen de
        // regels komt van VGap (met alle vier de kanten werden de regels 100 pt hoog).
        l->SetFlag(wxLEFT | wxALIGN_CENTER_VERTICAL);
        l->SetBorder(kCardSide + kCardInner);
        r->SetFlag(wxRIGHT | wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
        r->SetBorder(kCardSide + kCardInner);
        // Waarde in een formulierregel: het label gaat voor (options.cpp, fitChoices).
        if (r->IsWindow() && r->GetWindow()->GetHandle() && l->IsWindow()) {
          r->GetWindow()->GetHandle()->setProperty("ocpnios_formvalue", true);
          r->GetWindow()->GetHandle()->setProperty(
              "ocpnios_formlabel", l->GetWindow()->GetBestSize().x);
          // Stijl opnieuw: keuzelijst als iOS-menu (grijze waarde, geen kader).
          QWidget* fv = r->GetWindow()->GetHandle();
          MarkFormValue(fv);
          fv->style()->unpolish(fv);
          fv->style()->polish(fv);
        }
        if (!any && l->IsWindow()) any = l->GetWindow();
        // Schakelaar als waarde (Eenheden › Peilingen op de iPhone): als regel, tekst
        // links, schakelaar rechts.
        if (r->IsWindow() && dynamic_cast<wxCheckBox*>(r->GetWindow())) {
          QWidget* q = r->GetWindow()->GetHandle();
          if (q && !q->property("ocpnios_row").toBool()) {
            q->setProperty("ocpnios_row", true);
            q->installEventFilter(new RowSwitch(q));
            q->style()->unpolish(q);
            q->style()->polish(q);
          }
          r->SetFlag((r->GetFlag() | wxEXPAND | wxRIGHT) &
                     ~(wxALIGN_RIGHT | wxALIGN_CENTER_HORIZONTAL));
          r->SetBorder(kCardSide + kCardInner);
        }
        // Schakelaars in de rechterkolom (Eenheden › Peilingen): ook als regel.
        if (r->IsSizer()) {
          std::vector<wxWindow*> dummy;
          MakeSwitchRows(r->GetSizer(), dummy);
          // Ook als de regels al eerder gemaakt waren (Vectorkaartweergave op de iPad:
          // "Kaartinf objecte" afgekapt in een smalle kolom, 11 nov 2026).
          for (wxSizerItem* c : r->GetSizer()->GetChildren())
            if (c->IsWindow() && c->GetWindow()->GetHandle() &&
                c->GetWindow()->GetHandle()->property("ocpnios_row").toBool())
              dummy.push_back(c->GetWindow());
          if (!dummy.empty()) {
            r->SetFlag((r->GetFlag() | wxEXPAND) &
                       ~(wxALIGN_RIGHT | wxALIGN_CENTER_HORIZONTAL));
            r->SetBorder(kCardSide + kCardInner);
          }
        }
      }
      for (int i = fg->GetCols() - 1; i >= 0; i--)
        if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
      fg->AddGrowableCol(1);
      fg->SetVGap(16);
      {
        // Het raster zelf over de volle breedte, met wat ruimte boven en onder.
        ForEachSizer(root, [&](wxSizer* sz) {
          bool contains = false;
          for (wxSizerItem* it : sz->GetChildren())
            if (it->IsSizer() && it->GetSizer() == fg) contains = true;
          if (!contains) return;
          for (wxSizerItem* it : sz->GetChildren()) {
            if (it->IsSizer() && it->GetSizer() == fg) {
              // Niet links/rechts: de rand van 28 kwam dan bij de 30 van de regels (labels
              // op 62 pt in plaats van 30, Eenheden iPhone 11 nov 2026).
              it->SetFlag((it->GetFlag() | wxEXPAND | wxTOP | wxBOTTOM) &
                          ~(wxALIGN_CENTER | wxLEFT | wxRIGHT));
              it->SetBorder(28);
            } else if (it->IsSpacer()) {
              it->SetMinSize(wxSize(0, 0));  // lege band boven het formulier
            }
          }
        });
      }
      if (CardPainter* painter = painterFor(any)) {
        painter->cards.push_back({nullptr, fg, any});
        static_cast<QWidget*>(painter->parent())->update();
      }
      continue;
    }
    for (int i = fg->GetCols() - 1; i >= 0; i--)
      if (fg->IsColGrowable(i)) fg->RemoveGrowableCol(i);
    fg->SetCols(1);
    fg->AddGrowableCol(0);
    // Het raster over de volle breedte: anders bleven schakelaarregels zo smal als de
    // langste tekst en stond de schakelaar halverwege de kaart (iPad, 10 nov 2026).
    ForEachSizer(root, [&](wxSizer* sz) {
      for (wxSizerItem* it : sz->GetChildren())
        if (it->IsSizer() && it->GetSizer() == fg)
          it->SetFlag((it->GetFlag() | wxEXPAND) &
                      ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
    });
    QWidget* target = nullptr;
    CardPainter* painter = nullptr;
    std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
    for (size_t i = 0; i < v.size(); i++) {
      wxSizerItem* it = v[i];
      if (it->IsWindow() && dynamic_cast<wxStaticText*>(it->GetWindow())) {
        wxWindow* lab = it->GetWindow();
        if (lab->GetLabel().empty()) {  // lege opvulregel
          lab->Hide();
          continue;
        }
        lab->SetForegroundColour(wxColour(0x6d, 0x6d, 0x72));
        it->SetFlag(wxLEFT | wxTOP | wxALIGN_LEFT);
        it->SetBorder(kCardSide + 4);
        continue;
      }
      if (it->IsSizer() && it->GetSizer()->GetItemCount() > 0) {
        it->SetFlag(wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM);
        it->SetBorder(kCardSide + kCardInner);
        wxWindow* any = FirstWindowIn(it->GetSizer());
        if (!any || !any->GetHandle()) continue;
        if (!target) {
          target = any->GetHandle()->parentWidget();
          if (!target) continue;
          painter = static_cast<CardPainter*>(
              target->property("ocpnios_cards").value<QObject*>());
          if (!painter) {
            painter = new CardPainter(target);
            target->installEventFilter(painter);
            target->setProperty("ocpnios_cards", QVariant::fromValue<QObject*>(painter));
          }
        }
        CardPainter::Card card{it, nullptr, any, {}};
        MakeSwitchRows(it->GetSizer(), card.rows);
        painter->cards.push_back(card);
      } else if (it->IsSpacer()) {
        it->SetFlag(0);
        it->SetMinSize(wxSize(0, 0));  // opvulling tussen groepen: de kop geeft al ruimte
      }
    }
    if (target) target->update();
  }
  if (!grids.empty()) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }
  ocpnios_GroupBoxCards(root);
  ocpnios_FormGrids(root);
  ocpnios_Segments(root);
  ocpnios_PlainPageCard(root);
  ocpnios_FixControls(root);
}

// Rasters "label | keuzelijst/veld" in een groepsvak (Eigen Schip: Type
// scheepspictogram, afstandscirkels, Stijl, Kleur; Routes: standaardpictogram) als
// iOS-formulierregels: label links, waarde rechts tegen dezelfde rand als de
// schakelaars, keuzelijsten als grijze waarde met pijltje. Elk raster had eigen randen
// en uitlijning, de waarden stonden op vier verschillende plaatsen (Xander, 11 nov 2026).
// Buttons as iOS action rows: blue text on the card, left aligned with the other texts,
// no filled button; an add action with a "+", a removal in red. kind() returns 0
// (action), 1 (add) or 2 (destructive) for each button. Labels lose the desktop
// "...". Buttons with a style of their own (header, connection rows) are left alone.
// Buttons that only share a row with each other ("Select All | Clear All") get one row
// each.
void ocpnios_ActionRows(wxWindow* root, const std::function<int(wxButton*)>& kind) {
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  const QString blue = dark ? "#d06060" : "#007aff";
  const QString red = dark ? "#ff453a" : "#ff3b30";
  const QString grey = dark ? "#636366" : "#c7c7cc";
  bool changed = false;
  std::set<wxBoxSizer*> rows;
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      visit(c);
      auto* b = dynamic_cast<wxButton*>(c);
      QWidget* q = b ? b->GetHandle() : nullptr;
      if (!q || b->GetLabel().empty() || q->property("ocpnios_actionrow").toBool() ||
          q->property("ocpnios_nav").toBool() || q->property("ocpnios_plainbtn").toBool() ||
          q->property("ocpnios_colored").toBool() || !q->styleSheet().isEmpty() ||
          b->GetId() == wxID_OK || b->GetId() == wxID_CANCEL || b->GetId() == wxID_APPLY)
        continue;
      const int k = kind(b);
      wxString label = b->GetLabel();
      label.Trim();
      while (label.EndsWith(".") || label.EndsWith(wxString::FromUTF8("\u2026")))
        label.RemoveLast();
      label.Trim();
      if (k == 1) label = "+  " + label;
      if (label != b->GetLabel()) b->SetLabel(label);
      q->setProperty("ocpnios_actionrow", true);
      q->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: none;"
                               " text-align: left; padding: 10px 0px; font-weight: normal; }"
                               "QPushButton:pressed { color: %2; }"
                               "QPushButton:disabled { color: %3; }")
                           .arg(k == 2 ? red : blue, dark ? "#e08080" : "#66aaff", grey));
      b->SetMinSize(wxSize(-1, 44));
      b->SetMaxSize(wxSize(-1, -1));
      b->InvalidateBestSize();
      if (wxSizer* cs = b->GetContainingSizer()) {
        auto* bs = dynamic_cast<wxBoxSizer*>(cs);
        if (bs && bs->GetOrientation() == wxHORIZONTAL) {
          bool onlyButtons = true;
          for (wxSizerItem* it : bs->GetChildren())
            if (!it->IsSpacer() && !(it->IsWindow() && dynamic_cast<wxButton*>(it->GetWindow())))
              onlyButtons = false;
          if (onlyButtons) rows.insert(bs);
        } else if (wxSizerItem* it = cs->GetItem(b)) {
          // The 44 pt row is the spacing; no extra border above and below.
          it->SetFlag((it->GetFlag() | wxEXPAND) &
                      ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT | wxTOP | wxBOTTOM));
          it->SetProportion(0);
          // A column of only buttons (Chart Files: Add, Remove, Compress): no spacers
          // between the rows either.
          bool onlyButtons = true;
          for (wxSizerItem* x : cs->GetChildren())
            if (!x->IsSpacer() && !(x->IsWindow() && dynamic_cast<wxButton*>(x->GetWindow())))
              onlyButtons = false;
          if (onlyButtons)
            for (wxSizerItem* x : cs->GetChildren())
              if (x->IsSpacer()) {
                x->SetMinSize(wxSize(0, 0));
                x->SetProportion(0);
              }
        }
      }
      changed = true;
    }
  };
  visit(root);
  for (wxBoxSizer* bs : rows) {
    bs->SetOrientation(wxVERTICAL);
    for (wxSizerItem* it : bs->GetChildren()) {
      it->SetProportion(0);
      it->SetFlag((it->GetFlag() | wxEXPAND) &
                  ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT | wxTOP | wxBOTTOM));
    }
    // The row itself over the full width of its card.
    std::function<void(wxSizer*)> widen = [&](wxSizer* sz) {
      for (wxSizerItem* it : sz->GetChildren()) {
        if (!it->IsSizer()) continue;
        if (it->GetSizer() == bs) it->SetFlag(it->GetFlag() | wxEXPAND);
        else widen(it->GetSizer());
      }
    };
    if (root->GetSizer()) widen(root->GetSizer());
  }
  if (changed) root->Layout();
}

// Grids "label | field | unit" (Vector Chart Display: Shallow Depth [6.56] feet): the
// field stood in the middle of the row and the unit at the far right. As an iOS form
// row: label left, field and unit together on the right.
static std::set<wxSizer*> g_unitGrids;
static void UnitGrids(wxWindow* root) {
  std::vector<std::pair<wxSizer*, wxSizerItem*>> grids;
  ForEachSizer(root, [&](wxSizer* sz) {
    for (wxSizerItem* it : sz->GetChildren()) {
      auto* fg = it->IsSizer() ? dynamic_cast<wxFlexGridSizer*>(it->GetSizer()) : nullptr;
      if (!fg || g_unitGrids.count(fg) || fg->GetCols() != 3) continue;
      std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
      if (v.empty() || v.size() % 3) continue;
      bool ok = true;
      for (size_t i = 0; ok && i < v.size(); i += 3)
        ok = v[i]->IsWindow() && dynamic_cast<wxStaticText*>(v[i]->GetWindow()) &&
             v[i + 1]->IsWindow() &&
             (dynamic_cast<wxTextCtrl*>(v[i + 1]->GetWindow()) ||
              dynamic_cast<wxSpinCtrl*>(v[i + 1]->GetWindow())) &&
             v[i + 2]->IsWindow() && dynamic_cast<wxStaticText*>(v[i + 2]->GetWindow());
      if (ok) grids.push_back({sz, it});
    }
  });
  for (auto& [parent, gi] : grids) {
    auto* fg = static_cast<wxFlexGridSizer*>(gi->GetSizer());
    g_unitGrids.insert(fg);
    g_sectionGrids.insert(fg);  // done here, FormGrids leaves it alone
    gi->SetFlag((gi->GetFlag() | wxEXPAND) & ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
    const int vb = ValueRightBorder(parent, gi);
    std::vector<wxWindow*> wins;
    for (wxSizerItem* it : fg->GetChildren()) wins.push_back(it->GetWindow());
    for (wxWindow* w : wins) fg->Detach(w);
    for (int c = fg->GetCols() - 1; c >= 0; c--)
      if (fg->IsColGrowable(c)) fg->RemoveGrowableCol(c);
    fg->SetCols(2);
    fg->AddGrowableCol(0);
    int fieldW = 0;
    for (size_t i = 1; i < wins.size(); i += 3) {
      wins[i]->SetMinSize(wxSize(-1, wins[i]->GetMinSize().y));
      wins[i]->SetMaxSize(wxSize(-1, -1));
      wins[i]->InvalidateBestSize();
      fieldW = std::max(fieldW, wins[i]->GetBestSize().x);
    }
    fieldW = std::min(std::max(fieldW, 70), 84);
    for (size_t i = 0; i + 2 < wins.size(); i += 3) {
      fg->Add(wins[i], 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
      if (auto* ql = qobject_cast<QLabel*>(wins[i]->GetHandle()))
        ql->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      wins[i + 1]->SetMinSize(wxSize(fieldW, wins[i + 1]->GetMinSize().y));
      wins[i + 1]->SetMaxSize(wxSize(fieldW, -1));
      row->Add(wins[i + 1], 0, wxALIGN_CENTER_VERTICAL);
      row->Add(wins[i + 2], 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      fg->Add(row, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, vb);
      row->GetItem((size_t)0)->SetFlag(wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM);
      row->GetItem((size_t)0)->SetBorder(4);
    }
  }
  if (!grids.empty()) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }
}

void ocpnios_FormGrids(wxWindow* root) {
  UnitGrids(root);
  auto isValue = [](wxSizerItem* it) {
    if (!it || !it->IsWindow() || !it->IsShown()) return false;
    wxWindow* w = it->GetWindow();
    if (dynamic_cast<wxStaticText*>(w) || dynamic_cast<wxCheckBox*>(w)) return false;
    if (!w->GetHandle() || w->GetBestSize().y > 60) return false;
    // Kleurkiezer (WR-instellingen "Kleur cursorroute"): ook een waarde rechts.
    return dynamic_cast<wxChoice*>(w) || dynamic_cast<wxTextCtrl*>(w) ||
           dynamic_cast<wxSpinCtrl*>(w) || dynamic_cast<wxButton*>(w) ||
           dynamic_cast<wxComboBox*>(w) || dynamic_cast<wxPickerBase*>(w);
  };
  auto isLabel = [](wxSizerItem* it) {
    return it && it->IsWindow() && dynamic_cast<wxStaticText*>(it->GetWindow()) &&
           !it->GetWindow()->GetLabel().Strip(wxString::both).empty();
  };
  // Raster → (oudersizer, item) om de rand van het raster te kennen.
  std::vector<std::pair<wxSizer*, wxSizerItem*>> grids;
  ForEachSizer(root, [&](wxSizer* sz) {
    for (wxSizerItem* it : sz->GetChildren()) {
      auto* fg = it->IsSizer() ? dynamic_cast<wxFlexGridSizer*>(it->GetSizer()) : nullptr;
      if (!fg || g_sectionGrids.count(fg)) continue;
      const int cols = fg->GetEffectiveColsCount();
      if (cols != 1 && cols != 2) continue;
      std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
      bool any = false;
      for (size_t i = 0; i + 1 < v.size(); i += cols == 2 ? 2 : 1)
        if (isLabel(v[i]) && isValue(v[i + 1])) any = true;
      if (any) grids.push_back({sz, it});
    }
  });
  bool changed = false;
  for (auto& [parent, gi] : grids) {
    auto* fg = static_cast<wxFlexGridSizer*>(gi->GetSizer());
    if (!g_sectionGrids.insert(fg).second) continue;
    const int cols = fg->GetEffectiveColsCount();
    gi->SetFlag((gi->GetFlag() | wxEXPAND) & ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT));
    const int vb = ValueRightBorder(parent, gi);
    // Schakelaarregels in dezelfde groep tekenen hun tekst 6 pt binnen de rand
    // (RowSwitch): labels van invulregels even ver inspringen, anders stonden ze ernaast
    // net verschoven (WR-instellingen, 11 nov 2026).
    std::function<bool(wxSizer*)> hasSwitch = [&](wxSizer* sz) {
      for (wxSizerItem* c : sz->GetChildren()) {
        if (c->IsWindow() && dynamic_cast<wxCheckBox*>(c->GetWindow())) return true;
        if (c->IsSizer() && c->GetSizer() != fg && hasSwitch(c->GetSizer())) return true;
      }
      return false;
    };
    // RowSwitch tekent zijn tekst op x = 0 van de schakelaar (niet meer 6 pt binnen de
    // rand): de labels stonden 6 pt rechts van de schakelaarteksten (Eigen Schip, AIS,
    // Routes; Xander 24 nov 2026 "niet goed uitgelijnd").
    const int labelIndent = 0;
    (void)hasSwitch;
    std::vector<wxWindow*> fields;  // invulvakjes: even breed
    std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
    // Eén regel "label | tekstveld" (Filter in Routebeheer): een zoekregel, het veld vult
    // de rest van de breedte. Rechts op eigen breedte was het een vakje van 40 pt
    // (Markeringen, iPhone 12 nov 2026).
    // Alleen bij een kort label: "Toon markeringspunten alleen bij kaartschaal kleiner
    // dan 1:" en "Straal van aankomstcirkel" zijn invulregels met de waarde rechts (het
    // veld stond midden in de regel, Xander 24 nov 2026).
    const bool searchRow = cols == 2 && v.size() == 2 && isLabel(v[0]) &&
                           dynamic_cast<wxTextCtrl*>(v[1]->GetWindow()) &&
                           !dynamic_cast<wxSpinCtrl*>(v[1]->GetWindow()) &&
                           v[0]->GetWindow()->GetBestSize().x < 120;
    // Eén regel "lang label | veld" (Toon markeringspunten alleen bij kaartschaal kleiner
    // dan 1: [0]): naast het veld brak het label per woord af. Het label over de volle
    // breedte, het veld eronder rechts, zoals de regel met schakelaar erboven.
    if (cols == 2 && v.size() == 2 && !searchRow && isLabel(v[0]) && isValue(v[1]) &&
        !dynamic_cast<wxChoice*>(v[1]->GetWindow()) && root->GetClientSize().x > 0 &&
        v[0]->GetWindow()->GetBestSize().x > root->GetClientSize().x * 55 / 100) {
      wxWindow* lw = v[0]->GetWindow();
      wxWindow* rw = v[1]->GetWindow();
      fg->Detach(lw);
      fg->Detach(rw);
      if (fg->IsColGrowable(1)) fg->RemoveGrowableCol(1);
      fg->SetCols(1);
      if (!fg->IsColGrowable(0)) fg->AddGrowableCol(0);
      fg->Add(lw, 0, wxEXPAND | wxLEFT, labelIndent);
      fg->Add(rw, 0, wxALIGN_RIGHT | wxRIGHT | wxTOP, vb);
      changed = true;
      continue;
    }
    for (size_t i = 0; i + 1 < v.size(); i += cols == 2 ? 2 : 1) {
      if (!isLabel(v[i]) || !isValue(v[i + 1])) continue;
      wxSizerItem* l = v[i];
      wxSizerItem* r = v[i + 1];
      l->SetFlag(wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | (labelIndent ? wxLEFT : 0));
      l->SetBorder(labelIndent);
      if (!searchRow &&
          (dynamic_cast<wxSpinCtrl*>(r->GetWindow()) || dynamic_cast<wxTextCtrl*>(r->GetWindow())))
        fields.push_back(r->GetWindow());
      l->SetProportion(0);
      // Geen vaste regelhoogte op het label: lange labels worden later afgebroken
      // (FitToWidth) en een vaste hoogte kapte de derde regel af (MMSI, 11 nov 2026).
      if (cols == 2) {
        if (auto* ql = qobject_cast<QLabel*>(l->GetWindow()->GetHandle()))
          ql->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      }
      r->SetFlag(searchRow ? (wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT | wxLEFT)
                           : (wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT));
      r->SetBorder(searchRow ? std::max(vb, 8) : vb);
      r->SetProportion(0);
      if (searchRow) {
        r->GetWindow()->SetMinSize(wxSize(120, r->GetWindow()->GetMinSize().y));
        r->GetWindow()->SetMaxSize(wxSize(-1, -1));
      }
      // Kleurkiezer: geen grijs vlak om het kleurstaal en een regelhoogte zoals de
      // andere regels (WR-instellingen, 11 nov 2026).
      if (auto* pk = dynamic_cast<wxPickerBase*>(r->GetWindow())) {
        // wx tekent de achtergrond van de kiezer en zijn knop zelf (systeemgrijs).
        const bool dark = QCoreApplication::instance() &&
                          QCoreApplication::instance()->property("ocpnios_dark").toBool();
        const wxColour card = dark ? wxColour(0x1c, 0x1c, 0x1e) : *wxWHITE;
        pk->SetBackgroundColour(card);
        for (wxWindow* k : pk->GetChildren()) k->SetBackgroundColour(card);
        pk->SetMinSize(wxSize(pk->GetMinSize().x, std::max(pk->GetMinSize().y, 40)));
      }
      if (QWidget* q = r->GetWindow()->GetHandle()) {
        if (qobject_cast<QComboBox*>(q)) {
          q->setProperty("ocpnios_formlabel", l->GetWindow()->GetBestSize().x);
          MarkFormValue(q);
          q->style()->unpolish(q);
          q->style()->polish(q);
        }
      }
      changed = true;
    }
    // Invulvakjes in één raster even breed (Lijndikte 4/2/0 en Windpijlen 2 waren
    // verschillend breed, WR-instellingen 11 nov 2026).
    if (fields.size() > 1) {
      int w = 0;
      for (wxWindow* f : fields) {
        f->SetMinSize(wxSize(-1, f->GetMinSize().y));
        f->SetMaxSize(wxSize(-1, -1));
        f->InvalidateBestSize();
        w = std::max(w, f->GetBestSize().x);
      }
      w = std::min(w, 120);
      if (w > 0)
        for (wxWindow* f : fields) {
          f->SetMinSize(wxSize(w, f->GetMinSize().y));
          f->SetMaxSize(wxSize(w, -1));
        }
    }
    for (int c = fg->GetCols() - 1; c >= 0; c--)
      if (fg->IsColGrowable(c)) fg->RemoveGrowableCol(c);
    fg->AddGrowableCol(cols - 1);
  }
  if (changed) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }

}

// Pagina zonder groepen of secties (Instellingen › Uitgebreid): alles op één witte kaart
// met de gewone marges, zoals de andere pagina's (Xander, 10 nov 2026: nog desktop).
void ocpnios_PlainPageCard(wxWindow* root) {
  auto* sw = dynamic_cast<wxScrolledWindow*>(root);
  wxSizer* top = root->GetSizer();
  if (!sw || !top) return;
  // Een pagina met een tabbladvenster (Kaart-downloader van een plugin) niet: de kaart om
  // de buitenste sizer maakte die breder dan het scherm.
  // Ook dieper (Kaart-downloader: het tabbladvenster zit in het pluginpaneel; de kaart gaf
  // een witte rand om een grijs paneel, iPhone 11 nov 2026).
  // Ook niet bij een eigen schuiflijst in de pagina (Plugins: de pluginlijst tekent zelf
  // iOS-kaarten op grijs; de kaart eromheen gaf een witte rand, iPhone 11 nov 2026).
  std::function<bool(wxWindow*)> hasBook = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      // A list inside a group box (Vector Chart Display: "User Standard Objects") is
      // already a card; it kept the switches and depth fields above it off a card.
      const bool boxedList = dynamic_cast<wxScrolledWindow*>(c) &&
                             dynamic_cast<wxStaticBoxSizer*>(c->GetContainingSizer());
      if (dynamic_cast<wxBookCtrlBase*>(c) ||
          (dynamic_cast<wxScrolledWindow*>(c) && !boxedList) ||
          (!c->IsTopLevel() && hasBook(c)))
        return true;
    }
    return false;
  };
  if (hasBook(root)) return;
  wxWindow* any = FirstWindowIn(top);
  // Het tekenvlak is de ouder van de onderdelen (bij wxQt het binnenvlak van het
  // schuifvenster, niet het venster zelf).
  QWidget* q = any && any->GetHandle() ? any->GetHandle()->parentWidget() : nullptr;
  if (!q || q->property("ocpnios_plaincard").toBool()) return;
  // Al kaarten of groepsvakken: niets doen.
  if (q->property("ocpnios_cards").isValid()) return;
  bool hasBox = false;
  for (QWidget* c : q->findChildren<QWidget*>()) {
    if (c->property("ocpnios_cards").isValid()) return;
    if (qobject_cast<QGroupBox*>(c) && c->isVisibleTo(q)) hasBox = true;
  }
  if (hasBox) {
    // Losse instellingen naast een groepsvak (Vectorkaartweergave: schakelaars en keuzes
    // boven "Gebruikers voorkeur Objecten", 11 nov 2026 nog zonder kaart). Het groepsvak
    // is al een kaart; de rest per blok (tussen twee witregels) een eigen kaart, zoals de
    // secties in de Instellingen-app.
    std::function<bool(wxSizer*)> hasStatic = [&](wxSizer* x) {
      for (wxSizerItem* c : x->GetChildren())
        if (c->IsSizer() && (dynamic_cast<wxStaticBoxSizer*>(c->GetSizer()) ||
                             hasStatic(c->GetSizer())))
          return true;
      return false;
    };
    // De verticale sizer waarin het groepsvak tussen de losse onderdelen staat.
    wxBoxSizer* col = nullptr;
    for (wxSizer* x = top; x;) {
      auto* bs = dynamic_cast<wxBoxSizer*>(x);
      if (bs && bs->GetOrientation() == wxVERTICAL && !dynamic_cast<wxStaticBoxSizer*>(x)) {
        col = bs;
        break;
      }
      wxSizer* next = nullptr;
      for (wxSizerItem* c : x->GetChildren())
        if (c->IsSizer() && hasStatic(c->GetSizer()) &&
            !dynamic_cast<wxStaticBoxSizer*>(c->GetSizer()))
          next = c->GetSizer();
      x = next;
    }
    if (!col) return;
    auto isBreak = [&](wxSizerItem* it) {
      if (it->IsSpacer() || !it->IsShown()) return true;
      // Een paneel met een eigen groepsvak (Geluiden: per alarm een paneel) is zelf al
      // een kaart; een witte kaart eromheen gaf kaarten in kaarten (iPhone 11 nov 2026).
      if (it->IsWindow() && it->GetWindow()->GetHandle())
        for (QGroupBox* gb : it->GetWindow()->GetHandle()->findChildren<QGroupBox*>())
          if (gb->isVisibleTo(it->GetWindow()->GetHandle())) return true;
      if (it->IsSizer())
        return dynamic_cast<wxStaticBoxSizer*>(it->GetSizer()) != nullptr ||
               hasStatic(it->GetSizer());
      return false;
    };
    // Blokken verzamelen: opeenvolgende onderdelen zonder witregel of groepsvak.
    std::vector<std::vector<wxSizerItem*>> runs;
    std::vector<wxSizerItem*> cur;
    for (wxSizerItem* it : col->GetChildren()) {
      if (isBreak(it)) {
        if (!cur.empty()) runs.push_back(cur);
        cur.clear();
        continue;
      }
      // Lege tekst (de lege labelkolom van het oorspronkelijke raster): weg.
      if (it->IsWindow())
        if (auto* st = dynamic_cast<wxStaticText*>(it->GetWindow()))
          if (st->GetLabel().Strip(wxString::both).empty()) {
            st->Hide();
            continue;
          }
      cur.push_back(it);
    }
    if (!cur.empty()) runs.push_back(cur);
    if (runs.empty()) return;
    // Ruimte boven de eerste kaart (de lege kopregel is weg; de kaart plakte aan de titel).
    col->Insert(0, 0, 16);
    q->setProperty("ocpnios_plaincard", true);
    auto* painter = new CardPainter(q);
    q->installEventFilter(painter);
    q->setProperty("ocpnios_cards", QVariant::fromValue<QObject*>(painter));
    const int side = kCardSide + kCardInner - 6;
    for (auto& run : runs) {
      wxSizer* cardSizer = nullptr;
      std::vector<wxWindow*> rows;
      auto collectRows = [&](wxSizerItem* it) {
        if (it->IsWindow() && dynamic_cast<wxCheckBox*>(it->GetWindow()))
          rows.push_back(it->GetWindow());
        else if (it->IsSizer() && !dynamic_cast<wxFlexGridSizer*>(it->GetSizer()))
          for (wxSizerItem* c : it->GetSizer()->GetChildren())
            if (c->IsWindow() && c->IsShown() && dynamic_cast<wxCheckBox*>(c->GetWindow()))
              rows.push_back(c->GetWindow());
      };
      for (wxSizerItem* it : run) collectRows(it);
      if (run.size() == 1 && run[0]->IsSizer()) {
        // Eén raster of blok: zelf de kaart (raster houdt zijn lijntjes per regel).
        run[0]->SetFlag(run[0]->GetFlag() | wxLEFT | wxRIGHT | wxEXPAND);
        run[0]->SetBorder(side);
        cardSizer = run[0]->GetSizer();
      } else {
        // Meer onderdelen: samen in een nieuwe verticale sizer op dezelfde plaats.
        int pos = 0;
        for (wxSizerItem* c : col->GetChildren()) {
          if (c == run[0]) break;
          pos++;
        }
        auto* box = new wxBoxSizer(wxVERTICAL);
        for (wxSizerItem* it : run) {
          const int prop = it->GetProportion(), flag = it->GetFlag(), border = it->GetBorder();
          if (it->IsWindow()) {
            wxWindow* w = it->GetWindow();
            col->Detach(w);
            box->Add(w, prop, flag | wxEXPAND, border);
          } else {
            wxSizer* x = it->GetSizer();
            col->Detach(x);
            box->Add(x, prop, flag | wxEXPAND, border);
          }
        }
        col->Insert(pos, box, 0, wxLEFT | wxRIGHT | wxEXPAND, side);
        cardSizer = box;
      }
      painter->cards.push_back({nullptr, cardSizer, FirstWindowIn(cardSizer), rows});
    }
    root->Layout();
    sw->FitInside();
    q->update();
    return;
  }
  q->setProperty("ocpnios_plaincard", true);
  // Marge alleen links en rechts: de hele pagina in een omhullende sizer (een grotere
  // rand per onderdeel gold ook boven en onder; de regels liepen ver uit elkaar).
  auto* wrap = new wxBoxSizer(wxVERTICAL);
  root->SetSizer(wrap, false);
  wrap->AddSpacer(16);  // ruimte tussen titel en kaart (Uitgebreid: kaart plakte aan titel)
  wrap->Add(top, 1, wxEXPAND | wxLEFT | wxRIGHT, kCardSide + kCardInner - 6);
  auto* painter = new CardPainter(q);
  q->installEventFilter(painter);
  q->setProperty("ocpnios_cards", QVariant::fromValue<QObject*>(painter));
  painter->cards.push_back({nullptr, top, any, {}});
  root->Layout();
  sw->FitInside();
  q->update();
}

// Keuzerondjes naast elkaar ("Locale Tijd ○ UTC") als iOS-segmentknop: grijze strook,
// gekozen deel wit (Xander, 10 nov 2026: iOS-stijl overal).
void ocpnios_Segments(wxWindow* root) {
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  bool changed = false;
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxHORIZONTAL) return;
    std::vector<wxSizerItem*> radios;
    for (wxSizerItem* it : sz->GetChildren())
      if (it->IsWindow() && dynamic_cast<wxRadioButton*>(it->GetWindow()) && it->IsShown())
        radios.push_back(it);
    if (radios.size() < 2 || radios.size() > 5) return;
    for (size_t i = 0; i < radios.size(); i++) {
      wxSizerItem* it = radios[i];
      QWidget* q = it->GetWindow()->GetHandle();
      if (!q) continue;
      const bool first = i == 0, last = i + 1 == radios.size();
      const QString strip = dark ? "#2c2c2e" : "#e3e3e8";
      const QString sel = dark ? "#636366" : "#ffffff";
      const QString text = dark ? "#ffffff" : "#000000";
      const QString ss =
          QString("QRadioButton { background: %1; color: %3; border: 2px solid %1;"
                  " padding: 5px 16px; spacing: 0px;"
                  " border-top-left-radius: %4px; border-bottom-left-radius: %4px;"
                  " border-top-right-radius: %5px; border-bottom-right-radius: %5px; }"
                  "QRadioButton:checked { background: %2; border-radius: 8px; }"
                  "QRadioButton::indicator { width: 0px; height: 0px; image: none;"
                  " border: none; background: transparent; margin: 0px; }")
              .arg(strip, sel, text, first ? "9" : "0", last ? "9" : "0");
      q->setProperty("ocpnios_segment", true);
      if (q->styleSheet() != ss) {
        q->setStyleSheet(ss);
        changed = true;
      }
      // Segmenten tegen elkaar aan (CenterRowItems zette er 16 pt tussen).
      it->SetFlag(it->GetFlag() & ~(wxLEFT | wxRIGHT));
      it->GetWindow()->InvalidateBestSize();
    }
  });
  if (changed) root->Layout();
}

// Groepsvakken (wxStaticBox, op iOS een QGroupBox die als witte kaart getekend wordt):
// links en rechts op dezelfde afstand van de rand als de andere kaarten (16 pt); stonden
// op 5 pt (Schermindeling, iPad 10 nov 2026). Schakelaars erin als iOS-regels met een
// lijntje ertussen.
namespace {
class BoxLines : public QObject {
 public:
  BoxLines(QWidget* box, std::vector<wxWindow*> rows) : QObject(box), m_rows(std::move(rows)) {}
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() != QEvent::Paint || m_busy) return false;
    auto* box = static_cast<QWidget*>(obj);
    m_busy = true;
    QCoreApplication::sendEvent(box, ev);  // eerst de kaart zelf
    m_busy = false;
    QPainter p(box);
    const bool dark = QCoreApplication::instance() &&
                      QCoreApplication::instance()->property("ocpnios_dark").toBool();
    p.setPen(QPen(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.6));
    QWidget* prev = nullptr;
    for (wxWindow* rw : m_rows) {
      QWidget* q = rw && rw->IsShown() ? rw->GetHandle() : nullptr;
      if (!q) continue;
      if (prev) {
        const QPoint a = box->mapFrom(box->window(), prev->mapTo(box->window(),
                                                                 QPoint(0, prev->height())));
        const QPoint b = box->mapFrom(box->window(), q->mapTo(box->window(), QPoint(0, 0)));
        // Staat er iets tussen de twee regels (Routes: veld en label tussen twee
        // schakelaars), dan de lijn vlak boven de volgende regel; midden ertussen liep
        // hij door dat label (11 nov 2026).
        const double ly = b.y() - a.y() > 24 ? b.y() - 3 : (a.y() + b.y()) / 2.0 + 1;
        p.drawLine(QPointF(b.x(), ly), QPointF(box->width() - 6, ly));
      }
      prev = q;
    }
    return true;
  }

 private:
  std::vector<wxWindow*> m_rows;
  bool m_busy = false;
};
}  // namespace

// Rand die links/rechts al echt gold. Een plugin geeft soms een rand zonder zijde op
// (o-charts: Add(knop, 0, wxALIGN_CENTER_HORIZONTAL, 50)); die telt niet. Met die 50 erbij
// werd de inhoud 558 pt breed op een scherm van 440 (o-charts Voorkeuren, 11 nov 2026).
int SideBorder(wxSizerItem* it) {
  return (it->GetFlag() & (wxLEFT | wxRIGHT)) ? it->GetBorder() : 0;
}

void ocpnios_GroupBoxCards(wxWindow* root) {
  root->Layout();
  bool changed = false;
  ForEachSizer(root, [&](wxSizer* sz) {
    for (wxSizerItem* it : sz->GetChildren()) {
      // Keuzerondjesvak (wxRadioBox, op iOS ook een QGroupBox-kaart): zelfde marge als
      // de andere kaarten; stond op 5 pt van de rand (GRIB › Voorkeuren, 11 nov 2026).
      if (auto* rb = it->IsWindow() ? dynamic_cast<wxRadioBox*>(it->GetWindow()) : nullptr) {
        QWidget* rq = rb->GetHandle();
        if (rq && !rq->property("ocpnios_boxcard").toBool()) {
          rq->setProperty("ocpnios_boxcard", true);
          // Elk keuzerondje minstens zo breed als zijn tekst: wx rekent de kolombreedte
          // met de Engelse tekst of de desktopletter, en "Algemeen NMEA 0183" werd
          // afgekapt (Verbindingen › Uploadformaat, iPhone 12 nov 2026).
          bool grew = false;
          for (QRadioButton* r : rq->findChildren<QRadioButton*>()) {
            const int need = r->sizeHint().width();
            if (r->width() < need) { r->setMinimumWidth(need); grew = true; }
            // Ook de hoogte: anders vielen de staarten van g, y en p weg ("Sianal",
            // "Mavara" in het zoekvenster van mayara_pi, 24 nov 2026).
            const int needH = r->sizeHint().height();
            if (r->height() < needH) { r->setMinimumHeight(needH); grew = true; }
          }
          if (grew) { rb->InvalidateBestSize(); changed = true; }
          const int x = rb->GetPosition().x;
          if (x >= 0 && x < kCardSide && (it->GetFlag() & wxEXPAND)) {
            it->SetBorder(SideBorder(it) + (kCardSide - x));
            it->SetFlag(it->GetFlag() | wxLEFT | wxRIGHT);
            changed = true;
          }
        }
        continue;
      }
      auto* sb = it->IsSizer() ? dynamic_cast<wxStaticBoxSizer*>(it->GetSizer()) : nullptr;
      if (!sb || !sb->GetStaticBox()) continue;
      wxStaticBox* box = sb->GetStaticBox();
      QWidget* q = box->GetHandle();
      if (!q || q->property("ocpnios_boxcard").toBool()) continue;
      q->setProperty("ocpnios_boxcard", true);
      // Alleen vakken die tegen de rand van hun pagina staan (niet een vak in een vak).
      if (!dynamic_cast<wxStaticBox*>(box->GetParent())) {
        const int x = box->GetPosition().x;
        if (x >= 0 && x < kCardSide) {
          it->SetBorder(SideBorder(it) + (kCardSide - x));
          it->SetFlag(it->GetFlag() | wxLEFT | wxRIGHT | wxEXPAND);
          // Een eerdere ronde (clamp) zette de minimumbreedte op de volle breedte vanaf
          // de rand; met de marge erbij werd het vak te breed (o-charts
          // Systeemidentificatie: 426 + 2x16 op 440, 11 nov 2026). Het vak vult toch.
          if (box->GetMinSize().x > 0) SetMinWidth(box, -1);
          changed = true;
        }
      }
      std::vector<wxWindow*> rows;
      if (sb->GetOrientation() == wxVERTICAL) {
        MakeSwitchRows(sb, rows);
        if (rows.size() > 1) q->installEventFilter(new BoxLines(q, rows));
        if (!rows.empty()) changed = true;
      }
    }
  });
  if (changed) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }
}

// Schakelaars in de nachtstand: gedempte afbeeldingen (ui/switch_*_dark.svg) in plaats
// van fel wit en groen. Ook gebruikt door Instellingen (options.cpp).
QString ocpnios_DarkSwitchStyle() {
  const QString ui = QCoreApplication::applicationDirPath() + "/SharedSupport/uidata/ios/";
  return QString("QCheckBox::indicator { image: url(%1switch_off_dark.svg); }"
                 "QCheckBox::indicator:checked { image: url(%1switch_on_dark.svg); }")
      .arg(ui);
}

// Lijsten met kolommen die samen breder zijn dan de lijst passend maken.
void ocpnios_FitColumns(QWidget* q) {
  // Lijst met kolommen die samen breder zijn dan de lijst (Routebeheer op de iPhone:
  // "Van <-> Naar" half buiten de kaart en een horizontale schuifbalk): de breedste
  // kolom smaller, niet smaller dan zijn kop.
  for (QTreeView* tv : q->findChildren<QTreeView*>()) {
    QHeaderView* h = tv->header();
    if (!h || h->isHidden() || h->count() < 2) continue;
    int total = 0, widest = -1;
    for (int i = 0; i < h->count(); i++) {
      if (h->isSectionHidden(i)) continue;
      total += h->sectionSize(i);
      if (widest < 0 || h->sectionSize(i) > h->sectionSize(widest)) widest = i;
    }
    const int room = tv->viewport()->width();
    if (room < 100 || total <= room || widest < 0) continue;
    const QString head = h->model()->headerData(widest, Qt::Horizontal).toString();
    const int minW = QFontMetrics(h->font()).horizontalAdvance(head) + 24;
    const int newW = std::max(minW, h->sectionSize(widest) - (total - room));
    h->resizeSection(widest, newW);
    if (total - h->sectionSize(widest) + newW <= room)
      tv->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
}

// Knoppen in bladen en Instellingen: losse blauwe tekst oogde niet als knop (Xander,
// 10 nov 2026). Getint (lichtblauw vlak, blauwe tekst) of gevuld (blauw vlak, witte
// tekst). Keuze Xander: gevuld. Getint blijft beschikbaar met OCPNIOS_BUTTONS=tinted.
QString ocpnios_ButtonStyle(bool dark) {
  const char* env = std::getenv("OCPNIOS_BUTTONS");
  const bool filled = !(env && std::string(env) == "tinted");
  // Kopbalkknoppen (Annuleren, Toepassen, ‹ Terug) blijven tekstknoppen, niet vet;
  // "Gereed" zet zijn eigen vette stijl.
  const QString nav =
      "QPushButton[ocpnios_nav=\"true\"] { background: transparent; font-weight: normal; }"
      "QPushButton[ocpnios_nav=\"true\"]:default { font-weight: 600; }"
      // Knop zonder tekst (kleurkiezer "Markeerkleur", info-knop): geen blauw vlak, het
      // pictogram of de kleur is de knop.
      "QPushButton[ocpnios_plainbtn=\"true\"] { background: transparent; padding: 4px; }";
  if (filled)
    return QString("QPushButton { background-color: %1; color: #ffffff; border: none;"
                   "  border-radius: 10px; padding: 10px 16px; font-weight: 600; }"
                   "QPushButton:pressed { background-color: %2; }"
                   "QPushButton:disabled { background-color: %3; color: %4; }")
        .arg(dark ? "#8a3a3a" : "#007aff", dark ? "#6b2a2a" : "#0060d0",
             dark ? "#2c2c2e" : "#e3e3e8", dark ? "#636366" : "#a0a0a5") + nav;
  return QString("QPushButton { background-color: %1; color: %2; border: none;"
                 "  border-radius: 10px; padding: 10px 16px; font-weight: 600; }"
                 "QPushButton:pressed { background-color: %3; }"
                 "QPushButton:disabled { background-color: %4; color: %5; }")
      .arg(dark ? "#3a2222" : "#e5f0ff", dark ? "#d06060" : "#007aff",
           dark ? "#4a2a2a" : "#cce0ff", dark ? "#2c2c2e" : "#f0f0f3",
           dark ? "#636366" : "#a0a0a5") + nav;
}

// Stylesheet voor een blad of melding in iOS-stijl (zie ocpnios_SheetLook).
QString ocpnios_SheetStyle(bool dark) {
  QString ss = QString(
         "/*ocpnios-sheetlook*/"
         "QDialog, QMainWindow { background-color: %8; }"
         // Schuifvlak van een hoog blad (window_fit.cpp): via het stijlblad, een palet
         // werd weer overschreven en de GRIB-balk bleef in de schemerstand lichtgrijs
         // onder een zwarte kopbalk (iPhone, 13 nov 2026).
         "QWidget#ocpnios_sheet_content { background-color: %8; }"
         "QGroupBox { background-color: %1; border: none; border-radius: 10px;"
         "  margin-top: 30px; padding: 10px 6px 6px 6px; }"
         "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
         "  left: 6px; top: 4px; color: %2; }"
         // Knoppen in het blad als iOS-actie: blauwe tekst op een kaart, geen rand.
         // Knoppen met een eigen kleur (Raymarine Auto/Standby) hebben een eigen
         // stylesheet en blijven zo. De kopbalkknoppen (ocpnios_nav) ongemoeid.
         "QPushButton { background-color: %1; color: #007aff; border: none;"
         "  border-radius: 10px; padding: 10px 16px; }"
         "QPushButton:pressed { background-color: %3; }"
         "QPushButton:disabled { color: %2; }"
         "QPushButton:default { font-weight: 600; border: none; }"
         "QPushButton[ocpnios_nav=\"true\"] { border: none; background: transparent;"
         "  color: #007aff; font-size: 18px; padding: 12px 14px; }"
         "QPushButton[ocpnios_nav=\"true\"]:pressed { color: #66aaff; }"
         // Knop met een eigen kleur (Raymarine Auto rood, Standby groen): tekst in de
         // kleur die de plugin koos, niet blauw.
         "QPushButton[ocpnios_colored=\"true\"] { color: palette(button-text); }"
         "QPushButton[ocpnios_onwhite=\"true\"] { background-color: %4; }"
         // Tabbladen als iOS-segmentknop: grijze strook, gekozen deel wit.
         "QTabWidget::pane { border: none; }"
         "QTabWidget::tab-bar { alignment: center; }"
         "QTabBar::tab { background: %4; color: %5; border: 2px solid %4;"
         "  padding: 6px 16px; margin: 0px; }"
         "QTabBar::tab:first { border-top-left-radius: 9px;"
         "  border-bottom-left-radius: 9px; }"
         "QTabBar::tab:last { border-top-right-radius: 9px;"
         "  border-bottom-right-radius: 9px; }"
         "QTabBar::tab:selected { background: %6; border-radius: 8px;"
         "  font-weight: 600; }"
         // Lijsten en tabellen als kaart, kopregel in grijs zonder lijnen.
         "QTreeView, QListView, QTableView { background: %1; border: none;"
         "  border-radius: 10px; }"
         "QHeaderView { background: transparent; border: none; }"
         // Tekstvakken (licentietekst, logboeken) als kaart, zonder grijze rand.
         "QTextEdit, QPlainTextEdit, QTextBrowser { background: %1; border: none;"
         "  border-radius: 10px; padding: 6px; }"
         "QScrollArea { border: none; }"
         // Invulvakken en keuzelijsten: tekst leesbaar op de ondergrond (in de
         // nachtstand bleef de tekst zwart op donker).
         // Ook de achtergrond: in de schemerstand stond lichte tekst op een wit vak (GRIB-
         // tijd onleesbaar, iPhone 13 nov 2026).
         "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { color: %5; background-color: %1; }"
         // Schuifregelaar in de actiekleur (in de nachtstand rood, zie hieronder).
         "QSlider::sub-page:horizontal { background: #007aff; border-radius: 2px; }"
         // Tekst naast schakelaars en keuzerondjes: in de schemerstand bleef die zwart op
         // het donkere paneel (GRIB-cursorgegevens "Wind", iPhone 13 nov 2026).
         "QCheckBox, QRadioButton { color: %5; }"
         "QCheckBox:disabled, QRadioButton:disabled { color: %2; }"
         // Keuzelijst als waarde in een formulierregel: grijze waarde zonder kader.
         "QComboBox[ocpnios_formvalue=\"true\"] { border: none; background: transparent;"
         "  color: %2; }"
         "QHeaderView::section { background: %1; color: %2; border: none;"
         "  border-bottom: 1px solid %7; padding: 6px 8px; }")
         .arg(dark ? "#1c1c1e" : "#ffffff", dark ? "#8d8d93" : "#6d6d72",
              dark ? "#2c2c2e" : "#d1d1d6", dark ? "#2c2c2e" : "#e3e3e8",
              dark ? "#ffffff" : "#000000", dark ? "#636366" : "#ffffff",
              dark ? "#38383a" : "#c6c6c8")
         .arg(dark ? "#000000" : "#f2f2f7");
  // Nachtstand: actiekleur gedempt rood zoals de kopbalkknoppen (window_fit.cpp); fel
  // blauw verstoort het nachtzicht.
  if (dark) {
    ss.replace("#007aff", "#d06060");
    ss.replace("#66aaff", "#e08080");
    ss += ocpnios_DarkSwitchStyle();
  }
  ss += ocpnios_ButtonStyle(dark);
  return ss;
}

// iOS-stijl voor elk blad (dialogen van OpenCPN en plugins), zoals Instellingen:
// groepsvakken als witte kaarten met grijze kop, rasters "kop | onderdelen" als secties,
// schakelaars als regels (Xander, 10 nov 2026: "Ja, zo uitrollen"). Instellingen doet
// dit zelf per pagina (options.cpp, IosApplyLook/IosFitPage).
void ocpnios_SheetLook(wxWindow* root) {
  QWidget* q = root ? root->GetHandle() : nullptr;
  if (!q) return;
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  if (q->property("ocpnios_look_dark").toInt() != (dark ? 2 : 1)) {
    q->setProperty("ocpnios_look_dark", dark ? 2 : 1);
    // Eén achtergrondkleur voor het hele blad (iOS "grouped background"); de strook
    // onderin Routebeheer en de kopbalk waren lichtgrijs (#ececec) naast #f2f2f7.
    {
      QPalette pal = q->palette();
      pal.setColor(QPalette::Window, dark ? QColor(0, 0, 0) : QColor(0xf2, 0xf2, 0xf7));
      q->setPalette(pal);
      for (QWidget* c : q->findChildren<QWidget*>()) {
        // Een wx-paneel tekent zijn achtergrond zelf in zijn wx-kleur, ook zonder
        // autoFill (GRIB-cursorgegevens bleven in de schemerstand lichtgrijs, iPhone
        // 13 nov 2026). Grijs licht of zwart: naar de bladkleur.
        if (auto* wp = dynamic_cast<wxPanel*>(wxWindow::QtRetrieveWindowPointer(c))) {
          if (!wp->IsTopLevel() && wp->UseBgCol()) {
            const wxColour wb = wp->GetBackgroundColour();
            const QColor qb(wb.Red(), wb.Green(), wb.Blue());
            if (qb.saturation() < 20 && (qb.lightness() > 200 || qb.lightness() < 8)) {
              const QColor t = pal.color(QPalette::Window);
              wp->SetBackgroundColour(wxColour(t.red(), t.green(), t.blue()));
              // Teksten met een vaste donkere kleur (DimeWindow) werden zwart op zwart.
              // In de nacht licht (herkenbaar aan 254,254,254), overdag weer zwart.
              const wxColour mark(254, 254, 254);
              auto fix = [&](wxWindow* k) {
                const wxColour f = k->GetForegroundColour();
                const QColor qf(f.Red(), f.Green(), f.Blue());
                if (dark && qf.lightness() < 60)
                  k->SetForegroundColour(mark);
                else if (!dark && f == mark)
                  k->SetForegroundColour(*wxBLACK);
              };
              std::function<void(wxWindow*)> fg = [&](wxWindow* w) {
                if (w->UseForegroundColour()) fix(w);
                for (wxWindow* k : w->GetChildren())
                  if (!k->IsTopLevel()) fg(k);
              };
              fg(wp);
              wp->Refresh();
            }
          }
        }
        if (!c->autoFillBackground() || qobject_cast<QAbstractScrollArea*>(c)) continue;
        const QColor bg = c->palette().color(QPalette::Window);
        // Alleen grijze vlakken (systeemkleur); gekleurde panelen houden hun kleur.
        if (bg.saturation() < 20 && (bg.lightness() > 200 || bg.lightness() < 8)) {
          QPalette p2 = c->palette();
          p2.setColor(QPalette::Window, pal.color(QPalette::Window));
          c->setPalette(p2);
        }
      }
    }
    QString ss = q->styleSheet();
    const int cut = ss.indexOf("/*ocpnios-sheetlook*/");
    if (cut >= 0) ss.truncate(cut);
    ss += ocpnios_SheetStyle(dark);
    q->setStyleSheet(ss);
    // Kopbalk van het blad (window_fit.cpp) in dezelfde kleur; die nam bij het maken de
    // vensterkleur over (#ececec).
    if (QWidget* h = q->findChild<QWidget*>("ocpnios_sheet_header")) {
      // Titel in de nachtstand licht (bleef zwart op zwart); knoppen in de kleur die
      // window_fit.cpp voor dag of nacht gebruikt.
      h->setStyleSheet(QString("QWidget#ocpnios_sheet_header { border-bottom: 1px solid %1;"
                               " background-color: %2; }"
                               "QWidget#ocpnios_sheet_header QLabel { color: %3; }")
                           .arg(dark ? "#38383a" : "#c8c8cc", dark ? "#000000" : "#f2f2f7",
                                dark ? "#c7c7cc" : "#000000"));
      for (QPushButton* hb : h->findChildren<QPushButton*>()) {
        QString bs = hb->styleSheet();
        bs.replace(dark ? "#007aff" : "#d06060", dark ? "#d06060" : "#007aff");
        if (bs != hb->styleSheet()) hb->setStyleSheet(bs);
      }
    }
  }
  ocpnios_FitColumns(q);
  // Keuzelijst met een vaste kleine breedte (Raymarine: 40 pt voor "1".."10"): met de
  // iOS-chevron rechts paste de waarde niet meer ("‘"). Minstens langste keuze + chevron.
  {
    std::set<wxWindow*> grown;
    std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        if (auto* ch = dynamic_cast<wxChoice*>(c)) {
          int need = 0;
          for (unsigned int k = 0; k < ch->GetCount(); k++)
            need = std::max(need, TextWidth(c, ch->GetString(k)));
          need += 28 + 16;  // uitklapvak met chevron (touch_ui: 28) en opvulling
          // Vaste lage hoogte (Raymarine: 20 pt): met de iOS-opvulling bleef alleen de
          // bovenkant van de waarde zichtbaar ("1" als "‘").
          if (c->GetSize().y > 0 && c->GetSize().y < 32) {
            c->SetMinSize(wxSize(c->GetMinSize().x, 34));
            if (c->GetMaxSize().y > 0 && c->GetMaxSize().y < 34)
              c->SetMaxSize(wxSize(c->GetMaxSize().x, -1));
            if (QWidget* cq = c->GetHandle()) {
              cq->setMaximumHeight(QWIDGETSIZE_MAX);
              cq->setMinimumHeight(34);
            }
            c->SetSize(wxSize(-1, 34));
            if (c->GetParent()) grown.insert(c->GetParent());
          }
          // Alleen kleine lijsten; brede zijn al op schermbreedte gebracht (FitToWidth).
          if (c->GetSize().x < need && c->GetSize().x < 100) {
            c->SetMinSize(wxSize(need, c->GetMinSize().y));
            if (c->GetMaxSize().x > 0 && c->GetMaxSize().x < need)
              c->SetMaxSize(wxSize(-1, c->GetMaxSize().y));
            if (QWidget* cq = c->GetHandle()) {
              cq->setMaximumWidth(QWIDGETSIZE_MAX);
              cq->setMinimumWidth(need);
            }
            c->SetSize(wxSize(need, -1));
            if (c->GetParent()) grown.insert(c->GetParent());
          }
        }
        visit(c);
      }
    };
    visit(root);
    for (wxWindow* p : grown) p->Layout();
    if (!grown.empty()) root->Layout();
  }
  // Rasterregel die rechts buiten beeld valt (Raymarine-voorkeuren: "Reset" half weg,
  // iPad 10 nov 2026): de breedste tekst in die regel smaller afbreken.
  for (int pass = 0; pass < 3; pass++) {
    bool fitted = false;
    const int limit = q->width() - 8;
    ForEachSizer(root, [&](wxSizer* sz) {
      auto* fg = dynamic_cast<wxFlexGridSizer*>(sz);
      if (!fg) return;
      const int cols = std::max(1, fg->GetEffectiveColsCount());
      std::vector<wxSizerItem*> v(fg->GetChildren().begin(), fg->GetChildren().end());
      for (size_t row = 0; row < v.size(); row += cols) {
        int right = 0;
        wxWindow* widest = nullptr;
        size_t widestCol = 0;
        for (size_t i = row; i < std::min(v.size(), row + cols); i++) {
          if (!v[i]->IsWindow() || !v[i]->IsShown()) continue;
          wxWindow* w = v[i]->GetWindow();
          QWidget* wq = w->GetHandle();
          if (!wq || !wq->isVisible()) continue;
          right = std::max(right, wq->mapTo(q, QPoint(wq->width(), 0)).x());
          if (dynamic_cast<wxStaticText*>(w) &&
              (!widest || w->GetSize().x > widest->GetSize().x)) {
            widest = w;
            widestCol = i - row;
          }
        }
        if (right > limit && widest) {
          // De hele kolom smaller: een tekst in dezelfde kolom van een andere regel hield
          // de kolom anders even breed ("Reset" bleef half weg).
          const int avail = widest->GetSize().x - (right - limit) - 4;
          for (size_t r2 = widestCol; r2 < v.size(); r2 += cols) {
            if (!v[r2]->IsWindow()) continue;
            wxWindow* t = v[r2]->GetWindow();
            if (!dynamic_cast<wxStaticText*>(t) || t->GetSize().x <= avail) continue;
            if (FitText(t, avail)) {
              fitted = true;
              if (t->GetParent()) t->GetParent()->Layout();
            }
          }
        }
      }
    });
    if (!fitted) break;
    root->Layout();
  }
  // Knoppen op een wit vlak (een plugin die zijn paneel zelf wit maakt, Navico-
  // stuurautomaat): wit op wit was geen knop meer te zien. Dan iOS-grijs als achtergrond.
  for (QPushButton* pb : q->findChildren<QPushButton*>()) {
    QWidget* par = pb->parentWidget();
    // De kleur die de plugin met wx zette (SetBackgroundColour); het Qt-palet is hier
    // al overschreven door de bladkleur. Wit of bijna wit telt; #f2f2f7 niet.
    wxWindow* wpar = par ? wxWindow::QtRetrieveWindowPointer(par) : nullptr;
    const wxColour wc = wpar && wpar->UseBgCol() ? wpar->GetBackgroundColour() : wxColour();
    const bool onWhite =
        wc.IsOk() && wc.Red() > 250 && wc.Green() > 250 && wc.Blue() > 250;
    if (onWhite != pb->property("ocpnios_onwhite").toBool()) {
      pb->setProperty("ocpnios_onwhite", onWhite);
      pb->style()->unpolish(pb);
      pb->style()->polish(pb);
    }
  }
  // Knoppen met een eigen achtergrondkleur (wxQt-patch zet die als eigen stylesheet).
  for (QPushButton* pb : q->findChildren<QPushButton*>()) {
    const bool colored = pb->styleSheet().startsWith("QPushButton { background-color:");
    if (colored && !pb->property("ocpnios_colored").toBool()) {
      pb->setProperty("ocpnios_colored", true);
      pb->style()->unpolish(pb);
      pb->style()->polish(pb);
    }
  }
  // Knoppen onder elkaar (radar: "Selecteer radartypes", "Verberg Radar"): als iOS-
  // actieregels zo breed als de kaarten erboven, met ruimte ertussen; ze plakten tegen
  // elkaar en tegen de kaart erboven (iPad, 10 nov 2026).
  bool moved = false;
  std::set<wxWindow*> touched;  // vensters waarin iets verschoof: zelf opnieuw indelen
  auto relayout = [&]() {
    for (wxWindow* t : touched) t->Layout();
    root->Layout();
  };
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxVERTICAL || dynamic_cast<wxStaticBoxSizer*>(sz))
      return;
    for (wxSizerItem* it : sz->GetChildren()) {
      auto* b = it->IsWindow() ? dynamic_cast<wxButton*>(it->GetWindow()) : nullptr;
      if (!b || b->GetLabel().empty()) continue;
      // Alleen ruimte erboven: links en rechts gelijk met de kaarten.
      const int want = (it->GetFlag() | wxEXPAND | wxTOP) &
                       ~(wxALIGN_CENTER_HORIZONTAL | wxALIGN_RIGHT | wxLEFT | wxRIGHT |
                         wxBOTTOM);
      if (it->GetFlag() == want && it->GetBorder() >= 10) continue;
      it->SetFlag(want);
      it->SetBorder(std::max(it->GetBorder(), 10));
      moved = true;
      touched.insert(b->GetParent());
    }
  });
  if (moved) relayout();
  // Knop die zo tegen de schermrand komt (Routebeheer op de iPhone: "Importeer GPX-
  // bestand" van rand tot rand): toch ruimte links en rechts.
  if (moved) {
    bool again = false;
    ForEachSizer(root, [&](wxSizer* sz) {
      auto* bs = dynamic_cast<wxBoxSizer*>(sz);
      if (!bs || bs->GetOrientation() != wxVERTICAL) return;
      for (wxSizerItem* it : sz->GetChildren()) {
        auto* b = it->IsWindow() ? dynamic_cast<wxButton*>(it->GetWindow()) : nullptr;
        if (!b || !b->GetHandle() || (it->GetFlag() & wxLEFT)) continue;
        const QPoint p = b->GetHandle()->mapTo(b->GetHandle()->window(), QPoint(0, 0));
        if (p.x() >= 8) continue;
        it->SetBorder(SideBorder(it) > 0 ? SideBorder(it) : kCardSide);
        it->SetFlag(it->GetFlag() | wxLEFT | wxRIGHT);
        again = true;
        touched.insert(b->GetParent());
      }
    });
    if (again) relayout();
  }
  ocpnios_SectionGrids(root);  // loopt zelf alle onderliggende vensters af
  // Overige schakelaars ("Toon alle routes" in Routebeheer): ook als regel, schakelaar
  // rechts. Ná de kaarten, die de regels in hun groep zelf verzamelen (lijntjes).
  std::vector<wxWindow*> loose;
  ForEachSizer(root, [&](wxSizer* sz) {
    if (!dynamic_cast<wxStaticBoxSizer*>(sz)) MakeSwitchRows(sz, loose);
  });
  // Losse regels niet tegen de schermrand: de schakelaar raakte rechts de rand.
  for (wxWindow* w : loose) {
    wxSizer* cs = w->GetContainingSizer();
    wxSizerItem* it = cs ? cs->GetItem(w) : nullptr;
    if (!it) continue;
    it->SetFlag(it->GetFlag() | wxLEFT | wxRIGHT);
    it->SetBorder(std::max(it->GetBorder(), 10));
  }
  // Opnieuw indelen in het eigen venster van elke regel: root->Layout() deelt een
  // tabbladpagina niet opnieuw in (Routebeheer: rand bleef 0, gemeten 10 nov 2026).
  std::set<wxWindow*> parents;
  for (wxWindow* w : loose)
    if (w->GetParent()) parents.insert(w->GetParent());
  for (wxWindow* p : parents) p->Layout();
  if (!loose.empty()) root->Layout();
  ocpnios_FitRowHeights(root);
}

// Schakelaarregel met lange tekst: zo hoog als de tekst naast de schakelaar nodig heeft
// (Raymarine-voorkeuren: drie regels in de hoogte van twee, afgekapt; iPad 10 nov 2026).
void ocpnios_FitRowHeights(wxWindow* root) {
  std::set<wxWindow*> parents;
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      QWidget* q = c->GetHandle();
      if (q && q->property("ocpnios_row").toBool() && q->width() > 80) {
        QString text = static_cast<QCheckBox*>(q)->text();
        text.replace('\n', ' ');
        const int tw = q->width() - 46 - 12;
        const QRect br = QFontMetrics(q->font())
                             .boundingRect(QRect(0, 0, tw, 10000),
                                           Qt::TextWordWrap | Qt::AlignLeft, text);
        const int need = std::max(34, br.height() + 18);
        if (std::abs(c->GetMinSize().y - need) > 2 && (c->GetSize().y < need ||
                                                        c->GetMinSize().y > need)) {
          c->SetMinSize(wxSize(c->GetMinSize().x, need));
          if (c->GetParent()) parents.insert(c->GetParent());
        }
      }
      visit(c);
    }
  };
  visit(root);
  for (wxWindow* p : parents) p->Layout();
  if (!parents.empty()) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }
}

// Melding met eigen knoppen (veilige modus, vragen; window_fit.cpp, bladsoort 0): alleen
// de iOS-opmaak, zonder de indeling te veranderen (de melding houdt haar maat).
void ocpnios_AlertLook(QWidget* dialog) {
  if (!dialog || dialog->property("ocpnios_alertlook").toBool()) return;
  dialog->setProperty("ocpnios_alertlook", true);
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  dialog->setStyleSheet(dialog->styleSheet() + ocpnios_SheetStyle(dark));
  // De knoppen krijgen meer opvulling dan bij het indelen van de melding; zonder nieuwe
  // indeling bleven ze te smal ("gre", "nnulee" bij Akkoord/Annuleren, 5.14.2,
  // 10 nov 2026). Knoppen minstens zo breed als hun tekst, dan opnieuw indelen.
  wxWindow* ww = wxWindow::QtRetrieveWindowPointer(dialog);
  if (!ww) return;
  bool grew = false;
  std::function<void(wxWindow*)> visit = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      if (auto* b = dynamic_cast<wxButton*>(c)) {
        if (QWidget* bq = b->GetHandle()) {
          bq->ensurePolished();
          const QSize sh = bq->sizeHint();
          if (b->GetSize().x < sh.width() || b->GetSize().y < sh.height()) {
            b->SetMinSize(wxSize(std::max(b->GetMinSize().x, sh.width()),
                                 std::max(b->GetMinSize().y, sh.height())));
            grew = true;
          }
        }
      }
      visit(c);
    }
  };
  visit(ww);
  if (!grew) return;
  ww->Layout();
  // Past de melding niet meer, dan breder; window_fit zet haar weer in het midden.
  if (ww->GetSizer()) {
    const wxSize need = ww->GetSizer()->GetMinSize();
    const wxSize cur = ww->GetClientSize();
    if (need.x > cur.x || need.y > cur.y)
      ww->SetClientSize(wxSize(std::max(need.x, cur.x), std::max(need.y, cur.y)));
  }
}

void ocpnios_RetintIconButtons();  // hieronder

// Na het wisselen van kleurenschema (color_scheme.mm): open bladen opnieuw opmaken.
void ocpnios_RefreshSheetLooks() {
  for (QWidget* top : QApplication::topLevelWidgets()) {
    if (!top->property("ocpnios_look_dark").isValid() || !top->isVisible()) continue;
    if (wxWindow* ww = wxWindow::QtRetrieveWindowPointer(top)) ocpnios_SheetLook(ww);
    for (QWidget* c : top->findChildren<QWidget*>()) c->update();
  }
  ocpnios_RetintIconButtons();
}

// Na de iOS-opmaak (grotere opvulling van knoppen, vaste breedtes uit de desktopversie):
//  - knoppen minstens zo groot als hun tekst in de iOS-stijl ("ies lettertype",
//    "Selecteer … geluid" boven en onder afgekapt; iPhone 10 nov 2026);
//  - schuifregelaars en velden die rechts buiten de pagina vallen smaller
//    (Schaalfactor-regelaars in Gebruikersinterface).
namespace {
bool OnlyButtons(wxSizer* sz) {
  int n = 0;
  for (wxSizerItem* it : sz->GetChildren()) {
    if (it->IsSpacer()) continue;
    if (it->IsSizer()) {
      if (!OnlyButtons(it->GetSizer())) return false;
      n++;
      continue;
    }
    if (!it->IsWindow() || !dynamic_cast<wxButton*>(it->GetWindow())) return false;
    n++;
  }
  return n > 0;
}
bool HasShown(wxSizer* sz) {
  for (wxSizerItem* it : sz->GetChildren()) {
    if (it->IsWindow() && it->GetWindow()->IsShown()) return true;
    if (it->IsSizer() && HasShown(it->GetSizer())) return true;
  }
  return false;
}
}  // namespace

// Desktopindeling "lijst | kolom knoppen" (Templates, Kaartgroepen): op iOS de lijst over
// de volle breedte en de knoppen eronder (Xander, 10 nov 2026: "ziet er nog niet iOS
// uit"). En een groepsvak zonder zichtbare inhoud (Template Status) niet tonen.
bool ocpnios_StackListButtons(wxWindow* root) {
  bool changed = false;
  ForEachSizer(root, [&](wxSizer* sz) {
    if (auto* sb = dynamic_cast<wxStaticBoxSizer*>(sz)) {
      wxStaticBox* box = sb->GetStaticBox();
      const bool empty = !HasShown(sb);
      if (box && empty == box->IsShown()) {
        box->Show(!empty);
        changed = true;
      }
    }
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    if (!bs || bs->GetOrientation() != wxHORIZONTAL) return;
    // Rij knoppen breder dan de pagina (Kaart-downloader: "Bewerken…" half weg; MMSI:
    // "Nieuw…"): onder elkaar, elk over de volle breedte.
    if (OnlyButtons(sz) && sz->GetItemCount() > 1 && root->GetHandle() &&
        sz->GetMinSize().x > root->GetHandle()->width() - 24) {
      bs->SetOrientation(wxVERTICAL);
      for (wxSizerItem* it : sz->GetChildren()) it->SetFlag(it->GetFlag() | wxEXPAND);
      changed = true;
      return;
    }
    wxSizerItem* list = nullptr;
    wxSizerItem* buttons = nullptr;
    for (wxSizerItem* it : sz->GetChildren()) {
      if (it->IsSpacer()) continue;
      std::function<bool(wxSizerItem*)> hasDir = [&](wxSizerItem* x) {
        if (x->IsWindow()) return dynamic_cast<wxGenericDirCtrl*>(x->GetWindow()) != nullptr;
        if (x->IsSizer())
          for (wxSizerItem* y : x->GetSizer()->GetChildren())
            if (hasDir(y)) return true;
        return false;
      };
      const bool dirCtrl = hasDir(it);
      if (!list && (dirCtrl || ItemWantsSpace(it))) list = it;
      else if (!buttons && it->IsSizer() && OnlyButtons(it->GetSizer())) buttons = it;
    }
    if (!list || !buttons) return;
    bs->SetOrientation(wxVERTICAL);
    list->SetFlag(list->GetFlag() | wxEXPAND);
    list->SetProportion(0);
    // Zonder uitrekfactor kromp de lijst tot een streep (Kaartbestanden › Mappen).
    if (list->GetMinSize().y < 160) list->SetMinSize(wxSize(list->GetMinSize().x, 200));
    buttons->SetFlag(buttons->GetFlag() | wxEXPAND);
    buttons->SetProportion(0);
    changed = true;
  });
  if (changed) root->Layout();
  return changed;
}

QImage ocpnios_SymbolImage(const char* name, int pointSize);  // ios_look.mm

// Iconen van pluginknoppen (GRIB-balk) als iOS-symbool in de accentkleur: blauw bij dag,
// rood in schemer en nacht, net als de knoppen in de kopbalk. De desktopplaatjes (rode
// pijlen, "123", @) oogden niet als iOS (Xander, 13 nov 2026: "de grib plugin ziet er
// nog totaal niet ios uit").
namespace {
QColor IconAccent() {
  const bool dark = QCoreApplication::instance() &&
                    QCoreApplication::instance()->property("ocpnios_dark").toBool();
  return dark ? QColor(0xd0, 0x60, 0x60) : QColor(0x00, 0x7a, 0xff);
}
QImage TintImage(const QImage& src, const QColor& c) {
  QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter p(&img);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(QRect(0, 0, img.width(), img.height()), c);
  p.end();
  img.setDevicePixelRatio(src.devicePixelRatio());
  return img;
}
const char* const kTintIconProp = "ocpnios_tinticon";
}  // namespace

wxBitmap ocpnios_PluginSymbol(const char* symbol, int pointSize) {
  QImage f = ocpnios_SymbolImage(symbol, pointSize);
  if (f.isNull()) return wxNullBitmap;
  return wxBitmap(QPixmap::fromImage(TintImage(f, IconAccent())));
}

// Plaatjesknoppen in `root` als iOS-werkbalkknop: geen kader, een vlak van minstens
// 40 pt om op te tikken, bij indrukken een lichte achtergrond. Het icoon krijgt na een
// wissel van kleurenschema de nieuwe accentkleur (ocpnios_RefreshSheetLooks).
void ocpnios_FlatIconButtons(wxWindow* root) {
  if (!root) return;
  std::function<void(wxWindow*)> walk = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      // Ook de aan/uit-plaatjesknop (GRIB "gebied kiezen"): als blauwe knop met een
      // blauw icoon was hij een leeg blauw vlak (iPhone, 13 nov 2026). Aan = grijs vlak.
      wxControl* bb = dynamic_cast<wxBitmapButton*>(c);
      if (!bb) bb = dynamic_cast<wxBitmapToggleButton*>(c);
      if (bb) {
        if (QWidget* q = bb->GetHandle()) {
          q->setProperty(kTintIconProp, true);
          q->setStyleSheet(
              "QPushButton { border: none; background: transparent; padding: 4px;"
              " min-width: 36px; min-height: 36px; }"
              "QPushButton:pressed, QPushButton:checked {"
              " background: rgba(120,120,128,0.24); border-radius: 8px; }");
        }
        bb->SetMinSize(wxSize(40, 40));
        continue;
      }
      walk(c);
    }
  };
  walk(root);
}

void ocpnios_RetintIconButtons() {
  const QColor accent = IconAccent();
  for (QWidget* top : QApplication::topLevelWidgets()) {
    for (QAbstractButton* b : top->findChildren<QAbstractButton*>()) {
      if (!b->property(kTintIconProp).toBool() || b->icon().isNull()) continue;
      const QSize sz = b->iconSize();
      const qreal dpr = top->devicePixelRatioF();
      QPixmap pm = b->icon().pixmap(sz, dpr);
      if (pm.isNull()) continue;
      b->setIcon(QIcon(QPixmap::fromImage(TintImage(pm.toImage(), accent))));
    }
  }
}

namespace {

// Mapboom (wxGenericDirCtrl: Kaartgroepen) als iOS-lijst zoals de Bestanden-app:
// regels van 44 pt, blauw mapsymbool, pijltje om open te klappen, de bovenste mappen met
// hun pad vanaf Documenten in plaats van het hele pad van de app-container, geen
// horizontale schuifbalk. Alleen de weergave: de tekst in het model blijft het volle pad
// (options.cpp zoekt groepen op die tekst). Xander (11 nov 2026): "het bladeren door
// bestanden ziet er nog erg desktop uit".
constexpr int kDirRow = 44;
constexpr int kDirIndent = 20;

bool DarkMode() {
  return QCoreApplication::instance() &&
         QCoreApplication::instance()->property("ocpnios_dark").toBool();
}

int DirDepth(const QTreeView* tv, QModelIndex ix) {
  int d = 0;
  while (ix.parent().isValid() && ix.parent() != tv->rootIndex()) {
    d++;
    ix = ix.parent();
  }
  return d;
}

int DirChevronX(const QTreeView* tv, const QModelIndex& ix) {
  return 14 + DirDepth(tv, ix) * kDirIndent;
}

class DirTreeDelegate : public QStyledItemDelegate {
 public:
  DirTreeDelegate(QTreeView* tv, bool dirs) : QStyledItemDelegate(tv), m_tv(tv), m_dirs(dirs) {
    Load("folder.fill", m_folder);
    Load("map", m_leaf);
  }

  QSize sizeHint(const QStyleOptionViewItem& o, const QModelIndex& ix) const override {
    // Bron in Prioriteiten op twee regels (zie SourceLines): wat hoger.
    const bool two = IsSource(ix);
    return QSize(QStyledItemDelegate::sizeHint(o, ix).width(), two ? kDirRow + 14 : kDirRow);
  }

  void paint(QPainter* p, const QStyleOptionViewItem& opt,
             const QModelIndex& ix) const override {
    if (ix.column() > 0) return;  // alleen de eerste kolom (MakeDirTree verbergt de rest)
    const bool dark = DarkMode();
    const QRect r = opt.rect;
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    if (opt.state & QStyle::State_Selected)
      p->fillRect(r, dark ? QColor(0x3a, 0x3a, 0x3c) : QColor(0xe5, 0xe5, 0xea));
    const int cx = r.left() + DirChevronX(m_tv, ix);
    const int cy = r.center().y();
    if (m_tv->model() && m_tv->model()->hasChildren(ix)) {
      QPen pen(dark ? QColor(0x8d, 0x8d, 0x93) : QColor(0xb0, 0xb0, 0xb5), 2);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      p->setPen(pen);
      QPainterPath path;
      if (m_tv->isExpanded(ix)) {
        path.moveTo(cx - 5, cy - 2.5);
        path.lineTo(cx, cy + 2.5);
        path.lineTo(cx + 5, cy - 2.5);
      } else {
        path.moveTo(cx - 2.5, cy - 5);
        path.lineTo(cx + 2.5, cy);
        path.lineTo(cx - 2.5, cy + 5);
      }
      p->drawPath(path);
    }
    int x = cx + 14;
    const bool node = m_dirs || (m_tv->model() && m_tv->model()->hasChildren(ix));
    const QImage& folder = (node ? m_folder : m_leaf)[dark ? 1 : 0];
    if (!folder.isNull()) {
      const QSizeF s = folder.deviceIndependentSize();
      p->drawImage(QPointF(x, cy - s.height() / 2), folder);
      x += int(s.width()) + 10;
    }
    QFont font = opt.font;
    const QVariant fv = ix.data(Qt::FontRole);
    if (fv.isValid()) font = fv.value<QFont>();
    p->setFont(font);
    p->setPen(opt.palette.color(QPalette::Text));
    const QRect tr(x, r.top(), r.right() - 12 - x, r.height());
    QString line1, line2;
    if (IsSource(ix) && SourceLines(Label(ix), line1, line2)) {
      // Prioriteiten (connections_ios.cpp): de verbinding zoals in de lijst met
      // verbindingen ("TCP:localhost:10111"), eronder grijs de zin en het protocol. Op
      // één regel viel de poort weg ("nmea0183…:0;GNRMC").
      const QFontMetrics fm(font);
      const int h1 = fm.height();
      QFont small = opt.font;
      small.setBold(false);
      small.setPointSizeF(small.pointSizeF() * 0.85);
      const QFontMetrics sm(small);
      const int top = r.top() + (r.height() - h1 - sm.height() - 2) / 2;
      // Vet (actieve bron) tekende iets breder dan gemeten: wat marge.
      p->drawText(QRect(x, top, tr.width(), h1), Qt::AlignLeft | Qt::AlignVCenter,
                  fm.elidedText(line1, Qt::ElideMiddle, tr.width() - 10));
      p->setFont(small);
      p->setPen(dark ? QColor(0x8d, 0x8d, 0x93) : QColor(0x8e, 0x8e, 0x93));
      p->drawText(QRect(x, top + h1 + 2, tr.width(), sm.height()),
                  Qt::AlignLeft | Qt::AlignVCenter,
                  sm.elidedText(line2, Qt::ElideRight, tr.width()));
    } else {
      const QString text = QFontMetrics(font).elidedText(Label(ix), Qt::ElideMiddle, tr.width());
      p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, text);
    }
    p->setPen(QPen(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.6));
    p->drawLine(QPointF(x, r.bottom() + 0.5), QPointF(r.right(), r.bottom() + 0.5));
    p->restore();
  }

 private:
  // Een bron onder een categorie in Prioriteiten (eigenschap gezet in connections_ios.cpp).
  bool IsSource(const QModelIndex& ix) const {
    return m_tv && m_tv->property("ocpnios_prio_sources").toBool() && DirDepth(m_tv, ix) >= 2 &&
           ix.data(Qt::DisplayRole).toString().contains(';');
  }
  // "nmea0183 TCP:localhost:10111:0;GNRMC" -> "TCP:localhost:10111" en
  // "GNRMC · nmea0183". Het laatste ":0" (volgnummer binnen de verbinding) valt weg.
  static bool SourceLines(const QString& label, QString& line1, QString& line2) {
    const int sp = label.indexOf(' ');
    const int sc = label.lastIndexOf(';');
    if (sp <= 0 || sc <= sp) return false;
    const QString proto = label.left(sp);
    QString iface = label.mid(sp + 1, sc - sp - 1);
    const QString what = label.mid(sc + 1);
    const int lc = iface.lastIndexOf(':');
    if (lc > 0 && iface.count(':') >= 3) {
      bool num = false;
      iface.mid(lc + 1).toInt(&num);
      if (num) iface = iface.left(lc);
    }
    line1 = iface;
    line2 = what.isEmpty() ? proto : what + QString::fromUtf8(" \u00b7 ") + proto;
    return true;
  }
  QString Label(const QModelIndex& ix) const {
    QString t = ix.data(Qt::DisplayRole).toString();
    if (DirDepth(m_tv, ix) > 0 || !t.startsWith('/')) return t;
    // Bovenste map: het pad vanaf Documenten (de map van de app in de Bestanden-app).
    const int d = t.lastIndexOf("/Documents");
    if (d >= 0) {
      const QString rest = t.mid(d + 10);
      return rest.isEmpty() || rest == "/" ? QStringLiteral("Documenten") : rest.mid(1);
    }
    return t.section('/', -1, -1, QString::SectionSkipEmpty);
  }
  static void Load(const char* name, QImage out[2]) {
    QImage f = ocpnios_SymbolImage(name, 17);
    if (f.isNull()) return;
    for (int dark = 0; dark < 2; dark++) {
      QImage img = f.convertToFormat(QImage::Format_ARGB32_Premultiplied);
      QPainter p(&img);
      p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(QRect(0, 0, img.width(), img.height()),
                 dark ? QColor(0xd0, 0x60, 0x60) : QColor(0x00, 0x7a, 0xff));
      p.end();
      img.setDevicePixelRatio(f.devicePixelRatio());
      out[dark] = img;
    }
  }
  QTreeView* m_tv;
  bool m_dirs;
  QImage m_folder[2];
  QImage m_leaf[2];
};

// Tik op het pijltje: map open- of dichtklappen (de inspringruimte met Qt's eigen
// driehoekjes is weg).
class DirTreeTap : public QObject {
 public:
  explicit DirTreeTap(QTreeView* tv) : QObject(tv), m_tv(tv) {}
  bool eventFilter(QObject*, QEvent* ev) override {
    if (ev->type() != QEvent::MouseButtonRelease) return false;
    const QPoint pos = static_cast<QMouseEvent*>(ev)->position().toPoint();
    const QModelIndex ix = m_tv->indexAt(pos);
    if (!ix.isValid() || !m_tv->model()->hasChildren(ix)) return false;
    const int cx = m_tv->visualRect(ix).left() + DirChevronX(m_tv, ix);
    if (pos.x() < cx - 16 || pos.x() > cx + 16) return false;
    m_tv->setExpanded(ix, !m_tv->isExpanded(ix));
    return false;
  }

 private:
  QTreeView* m_tv;
};

void MakeDirTree(wxTreeCtrl* tc, QTreeView* tv, bool dirs) {
  if (tv->property("ocpnios_dirtree").toBool()) return;
  tv->setProperty("ocpnios_dirtree", true);
  // wxQt tekent de afbeelding van een regel zelf, na de weergave (wxQTreeWidget::drawRow),
  // linksboven in de regel: het oude gele mapje lag over het pijltje. Een lijst met even
  // veel doorzichtige afbeeldingen: wx tekent dan niets zichtbaars. Niet leeg laten: wx
  // vraagt de afbeelding op volgnummer op zonder te controleren.
  if (wxImageList* old = tc->GetImageList()) {
    const int n = old->GetImageCount();
    auto* clear = new wxImageList(1, 1, true, std::max(1, n));
    wxImage img(1, 1);
    img.InitAlpha();
    img.SetAlpha(0, 0, 0);
    for (int i = 0; i < std::max(1, n); i++) clear->Add(wxBitmap(img));
    tc->AssignImageList(clear);  // de boom ruimt hem op
  }
  // Geen naam bewerken bij een tik op een gekozen regel: dat opende het toetsenbord
  // (wxQt zet SelectedClicked aan); in deze lijsten heeft hernoemen geen betekenis.
  tv->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tv->setIndentation(0);
  tv->setRootIsDecorated(false);
  tv->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tv->setUniformRowHeights(true);
  // Eén kolom over de volle breedte. Overige kolommen (de boom van Nieuwe kaartbron
  // meldt er meer) verbergen: de weergave stond anders in elke kolom herhaald.
  if (QHeaderView* h = tv->header()) {
    for (int c = 1; tv->model() && c < tv->model()->columnCount(); c++)
      tv->setColumnHidden(c, true);
    h->setStretchLastSection(true);
    h->setSectionResizeMode(QHeaderView::Stretch);
  }
  tv->setItemDelegate(new DirTreeDelegate(tv, dirs));
  tv->viewport()->installEventFilter(new DirTreeTap(tv));
  tv->setStyleSheet(tv->styleSheet() +
                    "QTreeView::branch { image: none; border-image: none; }");
  tv->viewport()->update();
}

}  // namespace

void ocpnios_FixControls(wxWindow* root) {
  QWidget* rq = root ? root->GetHandle() : nullptr;
  if (!rq) return;
  // Losse tekstregels die in de Qt-letter breder zijn dan de rest van hun ouder: op
  // woordgrenzen afbreken. wx meet ~15% te smal, en de tekstregels van CreateTextSizer
  // (meldingen, keuzevragen) zijn elk een eigen label; de WeatherRouting-importvraag liep
  // rechts weg (iPhone, 11 nov 2026).
  {
    bool any = false;
    // Eerst de regels van één alinea samenvoegen tot één label: CreateTextSizer zet elke
    // regel van de desktopafbreking in een eigen label; per regel afbreken gaf rafelige
    // regels ("the / standard / boats"). Een witregel (spacer) blijft een alinea-grens.
    ForEachSizer(root, [&](wxSizer* sz) {
      auto* bs = dynamic_cast<wxBoxSizer*>(sz);
      if (!bs || bs->GetOrientation() != wxVERTICAL) return;
      // Alleen een tekstblok zoals wxDialog::CreateTextSizer het maakt: niets dan
      // tekstregels (en witregels), minstens drie. Losse labels elders blijven apart.
      int texts = 0;
      for (wxSizerItem* it : sz->GetChildren()) {
        if (it->IsSpacer()) continue;
        if (!it->IsWindow() || !dynamic_cast<wxStaticText*>(it->GetWindow())) return;
        texts++;
      }
      if (texts < 3) return;
      wxStaticText* head = nullptr;
      int run = 0;
      for (wxSizerItem* it : sz->GetChildren()) {
        auto* st = it->IsWindow() ? dynamic_cast<wxStaticText*>(it->GetWindow()) : nullptr;
        if (!st || !it->IsShown() || st->GetLabel().empty()) {
          head = nullptr;
          run = 0;
          continue;
        }
        if (!head) {
          head = st;
          run = 1;
          continue;
        }
        head->SetLabel(head->GetLabel() + " " + st->GetLabel());
        head->InvalidateBestSize();
        st->Hide();
        run++;
        any = true;
      }
    });
    std::function<void(wxWindow*)> wrap = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        auto* st = dynamic_cast<wxStaticText*>(c);
        QWidget* q = c->GetHandle();
        QWidget* par = q ? q->parentWidget() : nullptr;
        if (st && par && c->IsShown() && !st->GetLabel().empty()) {
          // Ruimte: tot de rechterrand van de ouder, en binnen het venster met dezelfde
          // marge rechts als links (de ouder kan breder zijn dan wat zichtbaar is).
          const int xr = q->mapTo(rq, QPoint(0, 0)).x();
          const int room = std::min(par->width() - q->x() - 12, rq->width() - xr - std::max(16, xr));
          int longest = 0;
          for (const wxString& line : wxSplit(st->GetLabel(), '\n', '\0'))
            longest = std::max(longest, TextWidth(c, line));
          if (room > 80 && longest > room) {
            wxString orig = st->GetLabel();
            orig.Replace("\n", " ");
            st->SetLabel(WrapWords(c, orig, room));
            c->InvalidateBestSize();
            any = true;
          }
        }
        wrap(c);
      }
    };
    wrap(root);
    if (any) {
      root->Layout();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
    }
  }
  // Lijsten met kolommen (wxListCtrl) vragen als beste breedte de som van alle kolommen
  // (Kaart-downloader: 582 pt op een pagina van 432); daardoor werd het tabblad breder
  // dan het scherm. De lijst mag smal; de kolommen schuiven binnen de lijst.
  {
    bool any = false;
    std::function<void(wxWindow*)> narrow = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        if (auto* lc = dynamic_cast<wxListCtrl*>(c)) {
          if (lc->GetBestSize().x > rq->width() - 40 && lc->GetMinSize().x != 100) {
            lc->SetMinSize(wxSize(100, lc->GetMinSize().y));
            lc->SetInitialSize(wxSize(100, lc->GetSize().y));
            lc->InvalidateBestSize();
            any = true;
          }
        }
        narrow(c);
      }
    };
    narrow(root);
    if (any) {
      std::function<void(wxWindow*)> inval = [&](wxWindow* w) {
        for (wxWindow* c : w->GetChildren())
          if (!c->IsTopLevel()) inval(c);
        w->InvalidateBestSize();
        if (w->GetSizer()) w->Layout();
      };
      inval(root);
    }
    // Tabbladvensters en hun pagina's niet breder dan de pagina: een notebook houdt de
    // breedte van de breedste pagina vast.
    std::function<void(wxWindow*)> clampBook = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        if (auto* nb = dynamic_cast<wxBookCtrlBase*>(c)) {
          QWidget* nq = nb->GetHandle();
          const int x = nq ? nq->mapTo(rq, QPoint(0, 0)).x() : 0;
          const int avail = rq->width() - x - 8;
          if (nq && nq->width() > avail && avail > 150) {
            nb->SetMinSize(wxSize(avail, nb->GetMinSize().y));
            nb->SetMaxSize(wxSize(avail, -1));
            for (size_t i = 0; i < nb->GetPageCount(); i++) {
              wxWindow* pg = nb->GetPage(i);
              pg->SetMaxSize(wxSize(avail, -1));
              pg->SetMinSize(wxSize(std::min(pg->GetMinSize().x, avail), pg->GetMinSize().y));
              if (pg->GetSizer()) pg->Layout();
            }
            any = true;
          }
        }
        clampBook(c);
      }
    };
    clampBook(root);
    if (any) {
      root->Layout();
      if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
    }
  }
  // Nooit horizontaal schuiven op een pagina (op een telefoon onverwacht).
  if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) {
    int rx = 0, ry = 0;
    sw->GetScrollPixelsPerUnit(&rx, &ry);
    if (rx != 0) sw->SetScrollRate(0, ry > 0 ? ry : 5);
  }
  ocpnios_StackListButtons(root);
  ocpnios_FitColumns(rq);
  // Zichtbare tabbalken binnen een pagina (Kaart-downloader: "Selecteer catalogus…" /
  // "Download kaarten…") als iOS-segmentknop. Alleen op de balk zelf: een stijl voor
  // alle tabbalken maakte de pagina's van Instellingen breder (verborgen balken tellen
  // mee in de breedte).
  // Ook een balk met één tab (Kaartgroepen: alleen "Alle kaarten" zolang er geen groep
  // is): anders bleef dat een los desktoptabblad (Xander, 11 nov 2026).
  for (QTabWidget* tw : rq->findChildren<QTabWidget*>()) {
    QTabBar* bar = tw->tabBar();
    if (!bar || !bar->isVisibleTo(tw) || bar->count() < 1 ||
        bar->property("ocpnios_segment").toBool())
      continue;
    bar->setProperty("ocpnios_segment", true);
    const bool dark = QCoreApplication::instance() &&
                      QCoreApplication::instance()->property("ocpnios_dark").toBool();
    bar->setStyleSheet(
        QString("QTabBar::tab { background: %1; color: %2; border: 2px solid %1;"
                " padding: 6px 12px; margin: 0px; }"
                "QTabBar::tab:first { border-top-left-radius: 9px;"
                " border-bottom-left-radius: 9px; }"
                "QTabBar::tab:last { border-top-right-radius: 9px;"
                " border-bottom-right-radius: 9px; }"
                "QTabBar::tab:selected { background: %3; border-radius: 8px;"
                " font-weight: 600; }")
            .arg(dark ? "#2c2c2e" : "#e3e3e8", dark ? "#ffffff" : "#000000",
                 dark ? "#636366" : "#ffffff"));
    tw->setStyleSheet(tw->styleSheet() +
                      "QTabWidget::pane { border: none; } QTabWidget::tab-bar { alignment: center; }");
  }
  // Boom zonder zichtbare kop (Nieuwe kaartbron: "GS…", "OS…"): de kolom groeit mee
  // met de inhoud (ook als er later rijen bijkomen of een tak opengaat), en vult
  // minstens de lijst. Wordt hij breder, dan schuift de lijst horizontaal.
  // Mapbomen (Kaartgroepen) als iOS-lijst, zie DirTreeDelegate.
  {
    std::function<void(wxWindow*)> dirs = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        if (auto* dc = dynamic_cast<wxGenericDirCtrl*>(c))
          if (dc->GetTreeCtrl())
            if (auto* tv = qobject_cast<QTreeView*>(dc->GetTreeCtrl()->GetHandle()))
              MakeDirTree(dc->GetTreeCtrl(), tv, true);
        // Andere boomlijsten zonder kopregel (Nieuwe kaartbron van de Kaart-downloader:
        // catalogi per bron) in dezelfde stijl.
        if (auto* tc = dynamic_cast<wxTreeCtrl*>(c))
          if (auto* tv = qobject_cast<QTreeView*>(tc->GetHandle()))
            if (tv->header() && tv->header()->isHidden()) MakeDirTree(tc, tv, false);
        dirs(c);
      }
    };
    dirs(root);
  }
  for (QTreeView* tv : rq->findChildren<QTreeView*>()) {
    QHeaderView* h = tv->header();
    if (!h || !h->isHidden() || tv->property("ocpnios_treecol").toBool() ||
        tv->property("ocpnios_dirtree").toBool())
      continue;
    tv->setProperty("ocpnios_treecol", true);
    h->setStretchLastSection(false);
    h->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    h->setMinimumSectionSize(std::max(60, tv->viewport()->width()));
  }
  // Keuzelijst als waarde in een formulierregel niet breder dan de ruimte naast het
  // label ("graden, decimale minuten" liep over "Geogr. Breedte/Lengte").
  {
    bool any = false;
    std::function<void(wxWindow*)> fit = [&](wxWindow* w) {
      for (wxWindow* c : w->GetChildren()) {
        if (c->IsTopLevel()) continue;
        QWidget* q = c->GetHandle();
        if (q && q->property("ocpnios_formvalue").toBool() && dynamic_cast<wxChoice*>(c) &&
            !q->property("ocpnios_formtext").toBool()) {
          const int avail = rq->width() - q->property("ocpnios_formlabel").toInt() - 2 * 30 - 24;
          if (avail > 80 && c->GetMinSize().x > avail) {
            c->SetMinSize(wxSize(avail, c->GetMinSize().y));
            c->SetMaxSize(wxSize(avail, -1));
            q->setMinimumWidth(0);
            q->setMaximumWidth(avail);
            any = true;
          }
        }
        fit(c);
      }
    };
    fit(root);
    if (any) root->Layout();
  }
  // Rasterregels "[schakelaar] tekst | veld" (AIS objecten) blijven zoals OpenCPN ze
  // zet: schakelaar links, veld rechts. Een poging de schakelaar rechts te zetten maakte
  // de tekstkolom breed en duwde de velden van het scherm (11 nov 2026).
  // Schakelaar direct in een groepsvak met wxEXPAND (Verbindingen › Uitgebreid:
  // "Magnetische peiling…"): ook als regel.
  ForEachSizer(root, [&](wxSizer* sz) {
    if (!dynamic_cast<wxStaticBoxSizer*>(sz)) return;
    for (wxSizerItem* it : sz->GetChildren()) {
      auto* cb = it->IsWindow() ? dynamic_cast<wxCheckBox*>(it->GetWindow()) : nullptr;
      QWidget* q = cb ? cb->GetHandle() : nullptr;
      if (!q || !(it->GetFlag() & wxEXPAND) || q->property("ocpnios_row").toBool()) continue;
      q->setProperty("ocpnios_row", true);
      q->installEventFilter(new RowSwitch(q));
      q->style()->unpolish(q);
      q->style()->polish(q);
    }
  });
  // Schakelaar met uitrekfactor in een horizontale rij (Verbindingen › Uitgebreid: info-
  // knop + "Always send RMB…"), of gevolgd door een lege/uitrekkende ruimte (Eigen Schip:
  // "Toon richting actief waypoint", "Automatische dagelijkse tracks", "Markeer tracks"):
  // ook als regel. In een raster met meer kolommen: alleen als de schakelaar de hele
  // regel krijgt (kolom groeit).
  ForEachSizer(root, [&](wxSizer* sz) {
    auto* bs = dynamic_cast<wxBoxSizer*>(sz);
    auto* fgs = dynamic_cast<wxFlexGridSizer*>(sz);
    const bool hrow = bs && bs->GetOrientation() == wxHORIZONTAL;
    if (!hrow && !fgs) return;
    std::vector<wxSizerItem*> v(sz->GetChildren().begin(), sz->GetChildren().end());
    for (size_t i = 0; i < v.size(); i++) {
      wxSizerItem* it = v[i];
      auto* cb = it->IsWindow() ? dynamic_cast<wxCheckBox*>(it->GetWindow()) : nullptr;
      QWidget* q = cb ? cb->GetHandle() : nullptr;
      if (!q || q->property("ocpnios_row").toBool()) continue;
      if (!hrow || it->GetProportion() <= 0) continue;
      q->setProperty("ocpnios_row", true);
      q->installEventFilter(new RowSwitch(q));
      q->style()->unpolish(q);
      q->style()->polish(q);
    }
  });
  // Losse schakelaars (Uitgebreid: "Rechttrekken scheve rasterkaarten") ook als regel.
  {
    std::vector<wxWindow*> loose;
    ForEachSizer(root, [&](wxSizer* sz) {
      if (!dynamic_cast<wxStaticBoxSizer*>(sz)) MakeSwitchRows(sz, loose);
    });
    // Niet tegen de schermrand, en gelijk met de knoppen en kaarten erboven (o-charts
    // Voorkeuren: "Schakel volledige kaartdatabase..." stond op x=0, 11 nov 2026).
    for (wxWindow* w : loose) {
      wxSizer* cs = w->GetContainingSizer();
      wxSizerItem* it = cs ? cs->GetItem(w) : nullptr;
      if (!it || (it->GetFlag() & wxLEFT)) continue;
      it->SetBorder(SideBorder(it) > 0 ? SideBorder(it) : kCardSide);
      it->SetFlag(it->GetFlag() | wxLEFT | wxRIGHT);
    }
    std::set<wxWindow*> ps;
    for (wxWindow* w : loose)
      if (w->GetParent()) ps.insert(w->GetParent());
    for (wxWindow* p : ps) p->Layout();
    if (!loose.empty()) root->Layout();
  }
  std::set<wxWindow*> parents;
  std::function<void(wxWindow*)> grow = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      // Invulveld te smal voor zijn inhoud (2147483646 in Routes/markeringspunten).
      if (auto* t = dynamic_cast<wxTextCtrl*>(c)) {
        QWidget* tq = t->GetHandle();
        if (tq && t->IsShown() && !t->IsMultiLine()) {
          const int need = TextWidth(t, t->GetValue()) + 28;
          if (t->GetSize().x < need && need < 220) {
            // Een eerder gezette maximale breedte (options.cpp, IosFitPage: ~28%) gaat
            // voor; die loslaten, het veld blijft binnen de kaart (clamp hieronder).
            t->SetMaxSize(wxSize(-1, t->GetMaxSize().y));
            tq->setMaximumWidth(QWIDGETSIZE_MAX);
            t->SetMinSize(wxSize(need, t->GetMinSize().y));
            tq->setMinimumWidth(need);
            if (t->GetParent()) parents.insert(t->GetParent());
          }
        }
      }
      if (auto* b = dynamic_cast<wxButton*>(c)) {
        if (QWidget* pq = b->GetHandle())
          if (b->GetLabel().empty() && !pq->property("ocpnios_plainbtn").toBool()) {
            pq->setProperty("ocpnios_plainbtn", true);
            pq->style()->unpolish(pq);
            pq->style()->polish(pq);
          }
        QWidget* bq = b->GetHandle();
        if (bq && b->IsShown() && !b->GetLabel().empty() &&
            !bq->property("ocpnios_nav").toBool()) {
          bq->ensurePolished();
          const QSize sh = bq->sizeHint();
          const wxSize cur = b->GetSize();
          const int maxW = std::max(120, rq->width() - 40);
          if (cur.x < std::min(sh.width(), maxW) || cur.y < sh.height()) {
            b->SetMinSize(wxSize(std::min(std::max(b->GetMinSize().x, sh.width()), maxW),
                                 std::max(b->GetMinSize().y, sh.height())));
            if (b->GetParent()) parents.insert(b->GetParent());
          }
        }
      }
      grow(c);
    }
  };
  grow(root);
  for (wxWindow* p : parents) p->Layout();
  if (!parents.empty()) root->Layout();
  // Na het vergroten van knoppen kan een rij knoppen te breed zijn geworden.
  ocpnios_StackListButtons(root);

  parents.clear();
  const int limit = rq->width() - 16;
  std::function<void(wxWindow*)> clamp = [&](wxWindow* w) {
    for (wxWindow* c : w->GetChildren()) {
      if (c->IsTopLevel()) continue;
      QWidget* q = c->GetHandle();
      // Formulierwaarde (FormValueText) niet vastzetten: die vult haar kolom en krimpt
      // mee. Vastgezet op de breedte van een nog te brede indeling bleef Routes breder
      // dan het scherm (pictogramkeuze 416 pt, 11 nov 2026).
      const bool formText = q && q->property("ocpnios_formtext").toBool();
      const bool narrowable = !formText &&
                              (dynamic_cast<wxSlider*>(c) || dynamic_cast<wxTextCtrl*>(c) ||
                               dynamic_cast<wxChoice*>(c) || dynamic_cast<wxComboBox*>(c));
      if (narrowable && q && q->isVisible()) {
        const int right = q->mapTo(rq, QPoint(q->width(), 0)).x();
        if (right > limit && limit > 100) {
          const int w2 = std::max(80, q->width() - (right - limit));
          c->SetMinSize(wxSize(w2, c->GetMinSize().y));
          c->SetMaxSize(wxSize(w2, -1));
          q->setMinimumWidth(0);
          q->setMaximumWidth(w2);
          if (c->GetParent()) parents.insert(c->GetParent());
        }
      }
      clamp(c);
    }
  };
  clamp(root);
  for (wxWindow* p : parents) p->Layout();
  if (!parents.empty()) {
    root->Layout();
    if (auto* sw = dynamic_cast<wxScrolledWindow*>(root)) sw->FitInside();
  }
  // Schakelaarregels met lange tekst ook hier op hoogte (Instellingen gebruikt
  // ocpnios_SheetLook niet; "Vergrendel markeringspunten…" was afgekapt).
  ocpnios_FitRowHeights(root);
}
