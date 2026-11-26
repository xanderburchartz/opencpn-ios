// iOS status display (HIG review 26 Nov 2026, round 2): own ship values as an iOS-style
// bar at the bottom ("Bar") or as a rounded, translucent box on the chart ("Floating"),
// in the day/dusk/night colours. Replaces the content of the desktop status bar; the
// fields come from what OpenCPN itself knows (no NMEA parsing here). The user picks the
// style and the fields in Options > User Interface > Status Bar (ocpnios_BuildStatusPage).
// "Show status bar" in Options still switches the whole display on or off.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cmath>
#include <vector>

#include <wx/wx.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QMouseEvent>

#include "model/config_vars.h"  // g_bShowStatusBar, g_bShowTrue, g_bShowMag
#include "model/navutil_base.h"  // toSDMM, toUsrSpeed, toUsrDistance, toMagnetic
#include "model/own_ship.h"      // gLat, gLon, gSog, gCog, gHdt
#include "model/routeman.h"      // g_pRouteMan
#include "navutil.h"             // pConfig
#include "chcanv.h"
#include "ocpn_frame.h"           // gFrame
#include "viewport.h"            // global_color_scheme

#ifdef __OCPN__IOS__

void ocpnios_ChartBarTick();  // app/ios/chart_bar_ios.cpp
int ocpnios_OverlayInsetLeft(wxWindow* canvas, int top, int bottom);  // window_fit.cpp
QRect ocpnios_ToolbarRect();  // toolbar_ios.cpp

namespace {

enum Field { kSog, kCog, kHdg, kPos, kWpt, kFieldCount };
const char* const kFieldKeys[kFieldCount] = {"sog", "cog", "hdg", "pos", "wpt"};
const char* const kConfigPath = "/Settings/iOS";

// The app has no translation catalogue of its own; Dutch and English are enough for
// these few strings.
wxString Tr(const char* en, const char* nl) {
  const wxLocale* l = wxGetLocale();
  if (l && l->GetCanonicalName().StartsWith("nl")) return wxString::FromUTF8(nl);
  return wxString::FromUTF8(en);
}

struct Config {
  int style = 1;          // 0 = bar, 1 = floating box (default since 26 nov 2026)
  unsigned fields = 0x3;  // bit per Field; default SOG and COG
  // Where the user dragged the box, as a fraction of the free room (0 = left/top,
  // 1 = right/bottom), so it keeps its corner after rotating. -1: the default corner,
  // bottom left.
  double fx = -1, fy = -1;
};

Config ReadConfig() {
  Config c;
  if (!pConfig) return c;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath(kConfigPath);
  // Default a floating box in the bottom left corner (Xander 26 nov 2026: "het nieuwe
  // SOG/COG scherm default graag ook als zwevend veld").
  long style = 1;
  pConfig->Read("StatusStyle", &style, 1L);
  c.style = style == 0 ? 0 : 1;
  // Once: the bar was the default until 26 nov 2026 and was written as soon as the
  // page was used, so a stored "bar" is mostly not a choice. From now on the style is
  // only written with StatusStyleSet.
  long set = 0;
  pConfig->Read("StatusStyleSet", &set, 0L);
  if (!set) c.style = 1;
  pConfig->Read("StatusBoxX", &c.fx, -1.0);
  pConfig->Read("StatusBoxY", &c.fy, -1.0);
  wxString f;
  if (pConfig->Read("StatusFields", &f)) {
    c.fields = 0;
    for (int i = 0; i < kFieldCount; i++)
      if (wxString("," + f + ",").Contains(wxString(",") + kFieldKeys[i] + ","))
        c.fields |= 1u << i;
  }
  pConfig->SetPath(old);
  return c;
}

void WriteConfig(const Config& c) {
  if (!pConfig) return;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath(kConfigPath);
  pConfig->Write("StatusStyle", (long)c.style);
  pConfig->Write("StatusStyleSet", 1L);
  pConfig->Write("StatusBoxX", c.fx);
  pConfig->Write("StatusBoxY", c.fy);
  wxString f;
  for (int i = 0; i < kFieldCount; i++)
    if (c.fields & (1u << i)) f += (f.empty() ? "" : ",") + wxString(kFieldKeys[i]);
  pConfig->Write("StatusFields", f);
  pConfig->SetPath(old);
  pConfig->Flush();
}

QString Q(const wxString& s) { return QString::fromUtf8(s.utf8_str().data()); }

QString Course(double deg) {
  if (std::isnan(deg)) return QString::fromUtf8("---°");
  QString s;
  if (g_bShowTrue || !g_bShowMag)
    s = QString::asprintf("%03d°", (int)std::lround(deg) % 360);
  if (g_bShowMag) {
    if (!s.isEmpty()) s += "  ";
    s += QString::asprintf("%03d°M", (int)std::lround(toMagnetic(deg)) % 360);
  }
  return s;
}

// Caption (small, grey) above the value (large).
QString Caption(int f) {
  switch (f) {
    case kSog: return "SOG";
    case kCog: return "COG";
    case kHdg: return "HDG";
    case kPos: return Q(Tr("Position", "Positie"));
    default: return Q(Tr("Waypoint", "Waypoint"));
  }
}

QString Value(int f) {
  switch (f) {
    case kSog:
      if (std::isnan(gSog)) return "---";
      return QString::asprintf("%.1f ", toUsrSpeed(gSog)) + Q(getUsrSpeedUnit());
    case kCog:
      // As the desktop status bar: COG only above 0.05 kn.
      return (!std::isnan(gSog) && gSog > 0.05) ? Course(gCog) : QString::fromUtf8("---°");
    case kHdg:
      return Course(gHdt);
    case kPos:
      if (std::isnan(gLat) || std::isnan(gLon)) return "---";
      return Q(toSDMM(1, gLat, false)) + "\n" + Q(toSDMM(2, gLon, false));
    default: {
      if (!g_pRouteMan || !g_pRouteMan->GetpActivePoint()) return "---";
      const double rng = g_pRouteMan->GetCurrentRngToActivePoint();
      const double brg = g_pRouteMan->GetCurrentBrgToActivePoint();
      return QString::asprintf("%.1f ", toUsrDistance(rng)) + Q(getUsrDistanceUnit()) +
             "  " + Course(brg);
    }
  }
}

struct Colors {
  QColor bg, value, caption;
};

Colors SchemeColors(bool floating) {
  switch (global_color_scheme) {
    case GLOBAL_COLOR_SCHEME_NIGHT:
      return {floating ? QColor(0, 0, 0, 180) : QColor(0, 0, 0), QColor(170, 70, 70),
              QColor(110, 45, 45)};
    case GLOBAL_COLOR_SCHEME_DUSK:
      return {floating ? QColor(20, 20, 22, 180) : QColor(20, 20, 22), QColor(200, 200, 204),
              QColor(141, 141, 147)};
    default:
      return {floating ? QColor(255, 255, 255, 190) : QColor(242, 242, 247), QColor(0, 0, 0),
              QColor(110, 110, 115)};
  }
}

class StatusPanel : public QWidget {
 public:
  explicit StatusPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);  // taps go to the chart
    m_grid = new QGridLayout(this);
  }

  void Rebuild(const Config& c, bool floating, int columns) {
    m_floating = floating;
    m_fields.clear();
    for (int i = 0; i < kFieldCount; i++)
      if (c.fields & (1u << i)) m_fields.push_back(i);
    // Start from scratch: the bar style puts each pair in a box widget, the floating
    // style puts the labels straight into the grid.
    m_values.clear();
    m_captions.clear();
    qDeleteAll(findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    delete m_grid;
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(floating ? 14 : 16, floating ? 8 : 4, floating ? 14 : 16,
                               floating ? 10 : 4);
    m_grid->setHorizontalSpacing(floating ? 20 : 24);
    m_grid->setVerticalSpacing(floating ? 6 : 0);
    const Colors col = SchemeColors(floating);
    const QString capStyle = QString("color: %1; font-size: %2px;")
                                 .arg(col.caption.name())
                                 .arg(floating ? 12 : 11);
    const QString valStyle = QString("color: %1; font-size: %2px; font-weight: 600;")
                                 .arg(col.value.name())
                                 .arg(floating ? 28 : 17);
    int n = 0;
    for (int f : m_fields) {
      auto* cap = new QLabel(Caption(f), this);
      auto* val = new QLabel(this);
      cap->setStyleSheet(capStyle);
      val->setStyleSheet(f == kPos ? valStyle + QString("font-size: %1px;").arg(floating ? 17 : 13)
                                   : valStyle);
      const int row = (n / columns) * 2, colIx = n % columns;
      if (floating) {
        m_grid->addWidget(cap, row, colIx);
        m_grid->addWidget(val, row + 1, colIx);
      } else {
        // Bar: caption and value next to each other, one line.
        auto* box = new QWidget(this);
        auto* h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        cap->setParent(box);
        val->setParent(box);
        h->addWidget(cap, 0, Qt::AlignVCenter);
        h->addWidget(val, 0, Qt::AlignVCenter);
        h->addStretch(1);  // pair stays together on the left of its column
        // The position is long: on a narrow bar it gets a row of its own.
        const bool wide = f == kPos && columns < kFieldCount;
        if (wide && n % columns) n += columns - n % columns;
        if (wide)
          m_grid->addWidget(box, n / columns, 0, 1, columns);
        else
          m_grid->addWidget(box, n / columns, n % columns);
        if (wide) n += columns - 1;
      }
      m_captions.push_back(cap);
      m_values.push_back(val);
      n++;
    }
    if (!floating) m_grid->setColumnStretch(std::min(n, columns), 1);
    m_bg = col.bg;
    // Widgets created after the panel was shown stay hidden until shown explicitly
    // (a rebuild after a colour scheme change gave an empty box).
    for (QWidget* c : findChildren<QWidget*>()) c->show();
    Update();
  }

  void Update() {
    for (size_t i = 0; i < m_fields.size(); i++) {
      QString v = Value(m_fields[i]);
      if (!m_floating && m_fields[i] == kPos) v.replace("\n", "  ");
      if (m_values[i]->text() != v) m_values[i]->setText(v);
    }
  }

  bool Empty() const { return m_fields.empty(); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_bg);
    if (m_floating)
      p.drawRoundedRect(rect(), 14, 14);
    else
      p.fillRect(rect(), m_bg);
  }

 private:
  QGridLayout* m_grid;
  std::vector<int> m_fields;
  std::vector<QLabel*> m_values, m_captions;
  QColor m_bg;
  bool m_floating = false;
};

QPointer<StatusPanel> s_panel;
void Place();

// New desktop field labels (wx recreates them when the field count changes) would
// show for up to a second until the next Place(); hide them as soon as they appear.
class FieldHider : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject*, QEvent* ev) override {
    // ChildAdded: new label; LayoutRequest: wx showed its labels again (after rotating).
    // Place() changes nothing when all is in order, so this does not loop.
    if (ev->type() == QEvent::ChildAdded || ev->type() == QEvent::LayoutRequest) {
      static bool pending = false;
      if (!pending) {
        pending = true;
        QTimer::singleShot(0, qApp, []() {
          pending = false;
          Place();
        });
      }
    }
    return false;
  }
};
QTimer* s_timer = nullptr;
Config s_config;
bool s_loaded = false;  // s_config read from the config file
int s_builtScheme = -1;
int s_builtColumns = -1;
int s_builtStyle = -1;

QMainWindow* MainWindow() {
  wxWindow* top = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
  return top ? qobject_cast<QMainWindow*>(top->GetHandle()) : nullptr;
}

QStatusBar* StatusBarOf(QMainWindow* mw) {
  return mw ? mw->findChild<QStatusBar*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

// Place (and if needed rebuild) the panel for the current settings. Called once a
// second and after a settings change; cheap when nothing changed.
// Room for the top left corner of the floating box: 8 pt from the chart's edges, at the
// left clear of the island (as the toolbar), at the bottom on the line of the zoom bar.
QRect FreeRoom(QMainWindow* mw, const QRect& area, const QSize& sz) {
  int left = area.left() + 8;
  if (gFrame)
    if (ChartCanvas* cc = gFrame->GetPrimaryCanvas())
      if (QWidget* cw = cc->GetHandle()) {
        const QPoint p = cw->mapFrom(mw, QPoint(area.left(), area.bottom() - sz.height()));
        left = area.left() + std::max(8, ocpnios_OverlayInsetLeft(cc, p.y(), p.y() + sz.height()));
      }
  const int right = area.right() - 8 - sz.width();
  const int top = area.top() + 8;
  const int bottom = area.bottom() - 8 - sz.height();
  // Clear of the toolbar when it reaches down to the box's line (landscape: the toolbar
  // runs the full height and the box lay over it, simulator 26 nov 2026). In portrait
  // the toolbar ends above the box and the box keeps the corner.
  const QRect tb = ocpnios_ToolbarRect();
  if (tb.isValid()) {
    const QRect tbLocal(mw->mapFromGlobal(tb.topLeft()), tb.size());
    if (tbLocal.bottom() + 8 > bottom) left = std::max(left, tbLocal.right() + 8);
  }
  return QRect(QPoint(left, top), QPoint(std::max(left, right), std::max(top, bottom)));
}

// Dragging the floating box with a finger (Xander 26 nov 2026: "zwevend worden zodat de
// gebruiker zelf kan bepalen waar hij komt"). A touch that moves less than 8 pt is no
// drag. On release the place is stored as a fraction of the free room.
struct DragState {
  bool pressed = false, active = false;
  QPoint start, origin;
} s_drag;

class BoxDrag : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* obj, QEvent* ev) override {
    auto* box = qobject_cast<QWidget*>(obj);
    if (!box) return false;
    const auto t = ev->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseMove &&
        t != QEvent::MouseButtonRelease)
      return false;
    auto* me = static_cast<QMouseEvent*>(ev);
    const QPoint g = me->globalPosition().toPoint();
    if (t == QEvent::MouseButtonPress) {
      s_drag.pressed = true;
      s_drag.active = false;
      s_drag.start = g;
      s_drag.origin = box->pos();
      return true;
    }
    if (!s_drag.pressed) return false;
    QMainWindow* mw = MainWindow();
    if (!mw) return false;
    QWidget* central = mw->centralWidget();
    const QRect area = central ? central->geometry() : mw->rect();
    const QRect room = FreeRoom(mw, area, box->size());
    if (t == QEvent::MouseMove) {
      const QPoint d = g - s_drag.start;
      if (!s_drag.active && d.manhattanLength() < 8) return true;
      s_drag.active = true;
      const QPoint p = s_drag.origin + d;
      box->move(std::clamp(p.x(), room.left(), room.right()),
                std::clamp(p.y(), room.top(), room.bottom()));
      return true;
    }
    // Release
    const bool was = s_drag.active;
    s_drag = DragState();
    if (was) {
      s_config.fx = room.width() > 0 ? double(box->x() - room.left()) / room.width() : 0.0;
      s_config.fy = room.height() > 0 ? double(box->y() - room.top()) / room.height() : 1.0;
      WriteConfig(s_config);
    }
    return true;
  }
};

void Place() {
  QMainWindow* mw = MainWindow();
  if (!mw) return;
  QStatusBar* sb = StatusBarOf(mw);
  const bool floating = s_config.style == 1;
  const bool wanted = g_bShowStatusBar && s_config.fields != 0;
  if (!s_panel) s_panel = new StatusPanel(mw);

  QWidget* central = mw->centralWidget();
  const QRect area = central ? central->geometry() : mw->rect();
  // iPhone portrait: at most 2 (box) or 3 (bar) fields per row, the rest below.
  const int columns = floating ? (area.width() < 600 ? 2 : 3) : (area.width() < 600 ? 3 : kFieldCount);
  if (s_builtScheme != (int)global_color_scheme || s_builtColumns != columns ||
      s_builtStyle != s_config.style) {
    s_panel->Rebuild(s_config, floating, columns);
    s_builtScheme = (int)global_color_scheme;
    s_builtColumns = columns;
    s_builtStyle = s_config.style;
  } else {
    s_panel->Update();
  }

  if (!wanted || !sb) {
    s_panel->hide();
    return;
  }
  if (!floating) {
    if (!s_panel->testAttribute(Qt::WA_TransparentForMouseEvents))
      s_panel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Bar: our row inside the status bar, the desktop fields hidden. wx creates new
    // field labels when the field count changes, so hide them on every pass.
    if (s_panel->parentWidget() != sb) {
      s_panel->setParent(sb);
      sb->addWidget(s_panel, 1);
    }
    for (QWidget* c : sb->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
      if (c != s_panel && c->isVisible()) c->hide();
    if (!sb->property("ocpnios_statusfilter").toBool()) {
      sb->setProperty("ocpnios_statusfilter", true);
      sb->installEventFilter(new FieldHider(sb));
    }
    const Colors col = SchemeColors(false);
    const QString sheet = QString("QStatusBar { background-color: %1; border: none; }"
                                  " QStatusBar::item { border: none; }")
                              .arg(col.bg.name());
    if (sb->styleSheet() != sheet) sb->setStyleSheet(sheet);
    const int h = std::max(30, s_panel->sizeHint().height());
    if (sb->minimumHeight() != h) sb->setMinimumHeight(h);
    if (!sb->isVisible()) sb->show();
    s_panel->show();
    return;
  }
  // Floating box: the desktop status bar goes; the box sits on the chart, bottom left
  // above the scale bar and the chart bar.
  if (s_panel->parentWidget() != mw) {
    sb->removeWidget(s_panel);
    s_panel->setParent(mw);
  }
  if (sb->isVisible()) sb->hide();
  s_panel->layout()->activate();
  s_panel->adjustSize();
  const QSize sz = s_panel->sizeHint();
  if (!s_drag.active) {
    const QRect room = FreeRoom(mw, area, sz);
    const double fx = s_config.fx < 0 ? 0.0 : std::clamp(s_config.fx, 0.0, 1.0);
    const double fy = s_config.fy < 0 ? 1.0 : std::clamp(s_config.fy, 0.0, 1.0);
    const int x = room.left() + (int)std::lround(fx * room.width());
    const int y = room.top() + (int)std::lround(fy * room.height());
    s_panel->setGeometry(x, y, sz.width(), sz.height());
  }
  // The box takes the finger itself (to be dragged); as a bar it lets taps through.
  if (s_panel->testAttribute(Qt::WA_TransparentForMouseEvents))
    s_panel->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  if (!s_panel->property("ocpnios_dragfilter").toBool()) {
    s_panel->setProperty("ocpnios_dragfilter", true);
    s_panel->installEventFilter(new BoxDrag(s_panel));
  }
  // Labels let the finger through to the box, so it can be dragged anywhere on it.
  for (QWidget* c : s_panel->findChildren<QWidget*>())
    if (!c->testAttribute(Qt::WA_TransparentForMouseEvents))
      c->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  s_panel->show();
  s_panel->raise();
}

}  // namespace

// Start the status display (from ocpnios_OnStartupDone, app/ios/frame_ios.cpp).
void ocpnios_StatusPanelInit() {
  if (s_timer) return;
  if (!s_loaded) s_config = ReadConfig();
  s_loaded = true;
  s_timer = new QTimer(qApp);
  s_timer->setInterval(1000);
  QObject::connect(s_timer, &QTimer::timeout, qApp, []() {
    Place();
    ocpnios_ChartBarTick();  // app/ios/chart_bar_ios.cpp
  });
  s_timer->start();
  // Not directly: this can run while OpenCPN is building the status bar (resize in
  // progress, see MyFrame::ODoSetSize).
  QTimer::singleShot(0, qApp, []() { Place(); });
}

// Place the status display again right away (after rotating, map_controls_ios.cpp).
void ocpnios_StatusPanelRelayout() {
  if (s_timer) Place();
}

// Options > User Interface > Status Bar: style and fields, applied at once (iOS style,
// no Apply needed). Built into an empty page from options::IosApplyLook.
void ocpnios_BuildStatusPage(wxScrolledWindow* page) {
  // OpenCPN builds the Options dialog during startup, before ocpnios_StatusPanelInit.
  if (!s_loaded) s_config = ReadConfig();
  s_loaded = true;
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  page->SetSizer(sizer);
  auto* form = new wxFlexGridSizer(2, 8, 8);
  form->AddGrowableCol(1);
  form->Add(new wxStaticText(page, wxID_ANY, Tr("Style", "Vorm")), 0, wxALIGN_CENTER_VERTICAL);
  // Two radio buttons side by side: fit_width turns them into an iOS segmented control.
  auto* seg = new wxBoxSizer(wxHORIZONTAL);
  auto* barBtn = new wxRadioButton(page, wxID_ANY, Tr("Bar", "Balk"), wxDefaultPosition,
                                   wxDefaultSize, wxRB_GROUP);
  auto* boxBtn = new wxRadioButton(page, wxID_ANY, Tr("Floating box", "Zwevend vak"));
  (s_config.style == 1 ? boxBtn : barBtn)->SetValue(true);
  seg->Add(barBtn, 0, wxALIGN_CENTER_VERTICAL);
  seg->Add(boxBtn, 0, wxALIGN_CENTER_VERTICAL);
  form->Add(seg, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
  sizer->Add(form, 0, wxEXPAND | wxALL, 12);

  auto* box = new wxStaticBoxSizer(wxVERTICAL, page, Tr("Fields", "Velden"));
  const wxString names[kFieldCount] = {
      Tr("Speed over ground (SOG)", "Snelheid over grond (SOG)"),
      Tr("Course over ground (COG)", "Koers over grond (COG)"),
      Tr("Heading (HDG)", "Koers (HDG)"),
      Tr("Position", "Positie"),
      Tr("Next waypoint: distance and bearing", "Volgend waypoint: afstand en peiling")};
  std::vector<wxCheckBox*> checks;
  for (int i = 0; i < kFieldCount; i++) {
    auto* cb = new wxCheckBox(box->GetStaticBox(), wxID_ANY, names[i]);
    cb->SetValue(s_config.fields & (1u << i));
    box->Add(cb, 0, wxEXPAND | wxALL, 6);
    checks.push_back(cb);
  }
  sizer->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  auto apply = [boxBtn, checks](wxCommandEvent& e) {
    e.Skip();
    Config c;
    c.style = boxBtn->GetValue() ? 1 : 0;
    c.fields = 0;
    for (int i = 0; i < kFieldCount; i++)
      if (checks[i]->GetValue()) c.fields |= 1u << i;
    s_config = c;
    WriteConfig(c);
    s_builtStyle = -1;  // rebuild
    Place();
  };
  barBtn->Bind(wxEVT_RADIOBUTTON, apply);
  boxBtn->Bind(wxEVT_RADIOBUTTON, apply);
  for (wxCheckBox* cb : checks) cb->Bind(wxEVT_CHECKBOX, apply);
}

wxString ocpnios_StatusPageTitle() { return Tr("Status Bar", "Statusbalk"); }

#endif  // __OCPN__IOS__
