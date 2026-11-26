// iOS toolbar (HIG review 26 Nov 2026, round 2; route A, no core change).
//
// OpenCPN's own toolbar is drawn into the chart; on iOS it looked like a desktop app
// (mixed icon styles, a collapse button). This file hides that toolbar with the switch
// plugins also use (g_disable_main_toolbar: no drawing, no taps) and shows an iOS
// toolbar in its place: SF Symbols, 44 pt buttons, "More" when not everything fits,
// swipe left to hide with a handle at the edge to bring it back, optional auto-hide.
//
// The buttons mirror OpenCPN's (hidden) toolbar: same ids, same order, same on/off
// state. A tap sends the same wx tool event the OpenCPN toolbar sends, so every action
// and every plugin works through the normal OpenCPN route. Which built-in buttons
// exist is still OpenCPN's GlobalToolbarConfig (same meaning as on the desktop);
// plugin buttons the user switched off are kept in /Settings/iOS/ToolbarHiddenTools.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include <wx/wx.h>
#include <wx/scrolwin.h>

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QToolButton>
#include <QAbstractButton>
#include <QWheelEvent>

#include "model/config_vars.h"  // g_toolbarConfig, g_bPermanentMOBIcon
#include "model/gui_vars.h"     // g_disable_main_toolbar
#include "model/idents.h"       // ID_SETTINGS, ID_MOB, ...
#include "chcanv.h"
#include "navutil.h"  // pConfig
#include "ocpn_frame.h"
#include "pluginmanager.h"
#include "model/plugin_loader.h"
#include "toolbar.h"  // g_MainToolbar
#include "viewport.h"  // global_color_scheme

#ifdef __OCPN__IOS__

QImage ocpnios_SymbolImage(const char* name, int pointSize);  // ios_look.mm
void ocpnios_ShowActionSheet(QMenu* menu, QWidget* anchor, const QString& cancel);  // action_sheet.mm
int ocpnios_OverlayInsetLeft(wxWindow* canvas, int top, int bottom);  // window_fit.cpp
void ocpnios_ForgetPage(wxWindow* root);  // fit_width.cpp

namespace {

// OpenCPN's toolbar stays hidden from the start: set before any chart is drawn
// (dynamic initialisation runs before main), so it is never visible, also not at
// startup, after rotating or after a colour scheme change.
struct HideCoreToolbar {
  HideCoreToolbar() { g_disable_main_toolbar = true; }
} s_hideCoreToolbar;

const char* const kPath = "/Settings/iOS";

wxString Tr(const char* en, const char* nl) {
  const wxLocale* l = wxGetLocale();
  if (l && l->GetCanonicalName().StartsWith("nl")) return wxString::FromUTF8(nl);
  return wxString::FromUTF8(en);
}
QString Q(const wxString& s) { return QString::fromUtf8(s.utf8_str().data()); }

// ---- settings ------------------------------------------------------------------

std::set<wxString> s_hidden;  // keys of plugin buttons switched off
// Auto-hide is OpenCPN's own setting (Options > User Interface: "Hide toolbar after
// ... seconds"), so there is one setting, also on the desktop. Options > Toolbar shows it
// as Always / Automatically.
#define s_autoHide g_bAutoHideToolbar
wxCheckBox* s_autoHideCB = nullptr;  // "Hide toolbar after" in Options > User Interface
int AutoHideMs() { return std::max(1, g_nAutoHideToolbar) * 1000; }
bool s_loaded = false;

void LoadSettings() {
  if (s_loaded || !pConfig) return;
  s_loaded = true;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath(kPath);
  wxString hidden;
  pConfig->Read("ToolbarHiddenTools", &hidden);
  for (const wxString& k : wxSplit(hidden, '|'))
    if (!k.empty()) s_hidden.insert(k);
  pConfig->SetPath(old);
}

void SaveSettings() {
  if (!pConfig) return;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath(kPath);
  wxString hidden;
  for (const wxString& k : s_hidden) hidden += (hidden.empty() ? "" : "|") + k;
  pConfig->Write("ToolbarHiddenTools", hidden);
  pConfig->SetPath("/Settings");
  pConfig->Write("AutoHideToolbar", g_bAutoHideToolbar);
  pConfig->SetPath(old);
  pConfig->Flush();
}

// ---- tools ---------------------------------------------------------------------

struct Tool {
  int id = 0;
  wxString key;       // stable name for the settings (plugin: common name + number)
  QString label;      // tool tip of OpenCPN, used in "More" and in Options
  const char* symbol = nullptr;  // only for the iOS controls ("More")
  QImage image;       // OpenCPN's own icon of the tool (core and plugins)
  qint64 imageKey = 0;  // changes when OpenCPN redraws the icon (colour scheme)
  bool toggle = false;
  bool on = false;
  bool plugin = false;
};

ocpnToolBarSimple* CoreToolbar() {
  return g_MainToolbar ? g_MainToolbar->GetToolbar() : nullptr;
}

// The buttons of OpenCPN's toolbar, in its order, without the collapse button (☰).
std::vector<Tool> CoreTools(bool includeHidden) {
  std::vector<Tool> out;
  ocpnToolBarSimple* tb = CoreToolbar();
  if (!tb) return out;
  std::vector<PlugInToolbarToolContainer*> ptools;
  if (g_pi_manager) {
    ArrayOfPlugInToolbarTools& a = g_pi_manager->GetPluginToolbarToolArray();
    for (unsigned i = 0; i < a.GetCount(); i++) ptools.push_back(a[i]);
  }
  for (auto node = tb->m_tools.GetFirst(); node; node = node->GetNext()) {
    wxToolBarToolBase* t = node->GetData();
    if (!t || t->GetId() == ID_MASTERTOGGLE) continue;
    Tool tool;
    tool.id = t->GetId();
    tool.label = Q(tb->GetToolShortHelp(tool.id));
    tool.toggle = t->CanBeToggled();
    tool.on = tool.toggle && tb->GetToolState(tool.id);
    // OpenCPN's own icon, recognisable for its users (Xander, 26 nov 2026: "de
    // icoontjes wil ik toch wel van OpenCPN houden"). OpenCPN redraws these for the
    // dusk and night schemes.
    const wxBitmap bmp = t->GetNormalBitmap();
    if (bmp.IsOk() && bmp.GetHandle()) {
      tool.image = bmp.GetHandle()->toImage();
      tool.imageKey = bmp.GetHandle()->cacheKey();
    }
    tool.key = wxString::Format("id%d", tool.id);
    for (PlugInToolbarToolContainer* p : ptools) {
      if (p->id != tool.id || !p->m_pplugin) continue;
      tool.plugin = true;
      int index = 0;
      for (PlugInToolbarToolContainer* q : ptools) {
        if (q == p) break;
        if (q->m_pplugin == p->m_pplugin) index++;
      }
      const wxString name = p->m_pplugin->GetCommonName();
      // Key from the plugin's file name: the common name is translated
      // (Dashboard is "Instrumentenpaneel" in Dutch).
      wxString file = name;
      if (const ArrayOfPlugIns* arr = PluginLoader::GetInstance()->GetPlugInArray())
        for (unsigned k = 0; k < arr->GetCount(); k++)
          if ((*arr)[k]->m_pplugin == p->m_pplugin && !(*arr)[k]->m_plugin_filename.empty())
            file = (*arr)[k]->m_plugin_filename;
      tool.key = wxString::Format("%s#%d", file, index);
      if (tool.label.isEmpty()) tool.label = Q(name);
      if (tool.image.isNull() && p->bitmap_day && p->bitmap_day->IsOk() &&
          p->bitmap_day->GetHandle())
        tool.image = p->bitmap_day->GetHandle()->toImage();
    }
    if (!includeHidden && tool.plugin && s_hidden.count(tool.key)) continue;
    out.push_back(tool);
  }
  return out;
}

// Same steps as a tap on OpenCPN's toolbar (ocpnToolBarSimple::OnMouseEvent).
void Activate(int id) {
  ocpnToolBarSimple* tb = CoreToolbar();
  if (!tb || !gFrame) return;
  opencpn_plugin_113* plugin = nullptr;
  if (g_pi_manager) {
    ArrayOfPlugInToolbarTools& a = g_pi_manager->GetPluginToolbarToolArray();
    for (unsigned i = 0; i < a.GetCount(); i++)
      if (a[i]->id == id) plugin = dynamic_cast<opencpn_plugin_113*>(a[i]->m_pplugin);
  }
  if (plugin) plugin->OnToolbarToolDownCallback(id);
  wxCommandEvent ev(wxEVT_COMMAND_TOOL_CLICKED, id);
  wxToolBarToolBase* t = tb->FindById(id);
  if (t && t->CanBeToggled()) {
    const bool on = !tb->GetToolState(id);
    tb->ToggleTool(id, on);
    ev.SetInt(on);
    ev.SetExtraLong(on);
  }
  gFrame->GetEventHandler()->AddPendingEvent(ev);
  if (plugin) plugin->OnToolbarToolUpCallback(id);
}

// ---- look ----------------------------------------------------------------------

struct Look {
  QColor bar, icon, on, mob;
};
Look CurrentLook() {
  switch (global_color_scheme) {
    case GLOBAL_COLOR_SCHEME_NIGHT:
      return {QColor(0, 0, 0, 230), QColor(150, 60, 60), QColor(60, 20, 20), QColor(150, 60, 60)};
    case GLOBAL_COLOR_SCHEME_DUSK:
      return {QColor(28, 28, 30, 230), QColor(200, 200, 204), QColor(80, 80, 86),
              QColor(255, 69, 58)};
    default:
      return {QColor(40, 40, 42, 225), QColor(255, 255, 255), QColor(110, 110, 116),
              QColor(255, 69, 58)};
  }
}

constexpr int kIcon = 32;  // OpenCPN icon inside the 44 pt button

// Plugin icons come with the dark square of OpenCPN's toolbar style behind them; on
// this bar that showed as a box around each plugin icon. When all four corners have
// the same dark, opaque colour, that colour becomes transparent. A plugin's own light
// background (part of its design) stays.
QImage WithoutStyleSquare(const QImage& in) {
  QImage img = in.convertToFormat(QImage::Format_ARGB32);
  const int w = img.width(), h = img.height();
  if (w < 8 || h < 8) return img;
  // The square may have rounded corners: look at the whole edge, one pixel inside.
  std::vector<QRgb> edge;
  for (int x = 1; x < w - 1; ++x) {
    edge.push_back(img.pixel(x, 1));
    edge.push_back(img.pixel(x, h - 2));
  }
  for (int y = 1; y < h - 1; ++y) {
    edge.push_back(img.pixel(1, y));
    edge.push_back(img.pixel(w - 2, y));
  }
  auto near = [](QRgb p, QRgb c) {
    return qAlpha(p) > 200 && std::abs(qRed(p) - qRed(c)) <= 6 &&
           std::abs(qGreen(p) - qGreen(c)) <= 6 && std::abs(qBlue(p) - qBlue(c)) <= 6;
  };
  QRgb best = 0;
  size_t bestCount = 0;
  for (size_t i = 0; i < edge.size(); i += 7) {
    if (qAlpha(edge[i]) < 250 || qGray(edge[i]) > 90) continue;
    size_t n = 0;
    for (QRgb p : edge) n += near(p, edge[i]);
    if (n > bestCount) bestCount = n, best = edge[i];
  }
  if (bestCount * 20 < edge.size() * 7) return img;  // no square around this icon
  for (int y = 0; y < h; ++y) {
    auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
    for (int x = 0; x < w; ++x)
      if (near(line[x], best)) line[x] = qRgba(0, 0, 0, 0);
  }
  return img;
}

QIcon MakeIcon(const Tool& t, const QColor& c) {
  if (!t.image.isNull()) {
    // OpenCPN's icon as it is, sharp at the screen's pixel density.
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const int px = std::lround(kIcon * dpr);
    QImage img = WithoutStyleSquare(t.image).scaled(px, px, Qt::KeepAspectRatio,
                                                    Qt::SmoothTransformation);
    img.setDevicePixelRatio(dpr);
    return QIcon(QPixmap::fromImage(img));
  }
  QImage img = t.symbol ? ocpnios_SymbolImage(t.symbol, 21) : QImage();
  if (img.isNull()) img = ocpnios_SymbolImage("puzzlepiece.extension", 21);
  img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  const qreal dpr = img.devicePixelRatio();
  QPainter p(&img);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(img.rect(), c);
  p.end();
  img.setDevicePixelRatio(dpr);
  return QIcon(QPixmap::fromImage(img));
}

constexpr int kButton = 44;  // HIG minimum tap target
constexpr int kPad = 4;

class Handle;

class IosToolbar : public QWidget {
 public:
  explicit IosToolbar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NoMousePropagation);
    m_hideTimer.setSingleShot(true);
    QObject::connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
      SlideOut();
      m_autoOut = true;
    });
  }

  // Rebuild the buttons when OpenCPN's toolbar, the settings or the colours changed;
  // otherwise only update the on/off state.
  void Sync(const QRect& area);
  void SlideOut();
  void SlideIn();
  bool IsOut() const { return m_out; }
  void Touched() {
    if (s_autoHide) {
      m_hideTimer.start(AutoHideMs());
    } else {
      m_hideTimer.stop();
      // Auto-hide switched off while the toolbar was hidden by it: back. A toolbar the
      // user swiped away stays away.
      if (m_out && m_autoOut) SlideIn();
    }
  }
  QPointer<Handle> handle;

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_look.bar);
    p.drawRoundedRect(rect(), 12, 12);
  }
  bool eventFilter(QObject* obj, QEvent* ev) override;
  void wheelEvent(QWheelEvent* ev) override {
    // Two fingers on a trackpad sideways (iPad with trackpad, iPad app on a Mac).
    m_wheelX += ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x() / 4;
    if (std::abs(m_wheelX) > 40) {
      m_wheelX = 0;
      SlideOut();
    }
    ev->accept();
  }

 private:
  void Place(const QRect& area);
  std::vector<Tool> m_tools;
  std::vector<QToolButton*> m_buttons;
  QString m_signature;
  QRect m_area;
  Look m_look;
  bool m_out = false;
  QPoint m_pressAt;
  bool m_swiping = false;
  int m_wheelX = 0;
  int m_inset = 8;
  bool m_autoOut = false;  // hidden by auto-hide, not by a swipe
  QTimer m_hideTimer;
  QPointer<QPropertyAnimation> m_anim;
};

// Small tab at the left edge while the toolbar is away: tap or swipe right (or two
// fingers sideways on a trackpad) brings it back.
class Handle : public QToolButton {
 public:
  Handle(QWidget* parent, IosToolbar* bar) : QToolButton(parent), m_bar(bar) {
    setAttribute(Qt::WA_NoMousePropagation);
    resize(44, 64);  // 44 pt wide to tap; the visible tab is the left 22 pt
    // A button, so VoiceOver (and the accessibility API) can bring the toolbar back.
    setAccessibleName(Q(Tr("Show toolbar", "Werkbalk tonen")));
    setToolTip(accessibleName());
    // After the tap has been handled: hiding the tab while Qt still delivers its touch
    // left Qt holding the touch, and no tap reached the app any more (simulator,
    // 26 Nov 2026).
    QObject::connect(this, &QToolButton::clicked, this, [this]() {
      QPointer<IosToolbar> bar(m_bar);
      QTimer::singleShot(0, this, [bar]() { if (bar) bar->SlideIn(); });
    });
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Look l = CurrentLook();
    p.setPen(Qt::NoPen);
    p.setBrush(l.bar);
    p.drawRoundedRect(QRect(-12, 0, 22 + 12, height()), 10, 10);
    p.setPen(QPen(l.icon, 3, Qt::SolidLine, Qt::RoundCap));
    const int x = 10;
    p.drawLine(x, height() / 2 - 12, x, height() / 2 + 12);
  }
  void wheelEvent(QWheelEvent* ev) override {
    m_wheelX += ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x() / 4;
    if (std::abs(m_wheelX) > 40) {
      m_wheelX = 0;
      if (m_bar) m_bar->SlideIn();
    }
    ev->accept();
  }
  // Swipe right on the tab: same as a tap.
  void mouseMoveEvent(QMouseEvent* ev) override {
    if (ev->position().x() > 30 && m_bar) {
      QPointer<IosToolbar> bar(m_bar);
      QTimer::singleShot(0, this, [bar]() { if (bar) bar->SlideIn(); });
    }
    QToolButton::mouseMoveEvent(ev);
  }

 private:
  QPointer<IosToolbar> m_bar;
  int m_wheelX = 0;
};

void IosToolbar::Sync(const QRect& area) {
  LoadSettings();
  std::vector<Tool> tools = CoreTools(false);
  QString sig = QString::number((int)global_color_scheme) + "|" +
                QString::number(area.height()) + "|";
  for (const Tool& t : tools)
    sig += QString::number(t.id) + ":" + QString::number(t.imageKey) + ",";
  if (sig != m_signature) {
    m_signature = sig;
    m_tools = tools;
    m_look = CurrentLook();
    for (QToolButton* b : m_buttons) b->deleteLater();
    m_buttons.clear();
    // How many fit: the chart height minus margins and room for the scale bar.
    const int avail = std::max(kButton, area.height() - 16 - 70);
    const int fit = std::max(1, (avail - 2 * kPad) / kButton);
    const bool more = (int)m_tools.size() > fit;
    const int shown = more ? fit - 1 : (int)m_tools.size();
    const QString style =
        QString("QToolButton { border: none; border-radius: 9px; background: transparent; }"
                " QToolButton:pressed { background: %1; }"
                " QToolButton:checked { background: %1; }")
            .arg(m_look.on.name(QColor::HexArgb));
    auto addButton = [&](const QIcon& icon, const QString& tip) {
      auto* b = new QToolButton(this);
      b->setIcon(icon);
      b->setIconSize(QSize(kIcon, kIcon));
      b->setFixedSize(kButton, kButton);
      b->setToolTip(tip);
      b->setAccessibleName(tip);
      b->setStyleSheet(style);
      b->installEventFilter(this);
      b->show();
      m_buttons.push_back(b);
      return b;
    };
    // MOB always last and always visible, also when the rest goes into "More".
    std::vector<Tool> order;
    const Tool* mob = nullptr;
    for (const Tool& t : m_tools)
      if (t.id == ID_MOB) mob = &t;
    for (const Tool& t : m_tools)
      if (t.id != ID_MOB) order.push_back(t);
    std::vector<Tool> visible(order.begin(),
                              order.begin() + std::min<size_t>(order.size(),
                                                               mob ? std::max(0, shown - 1) : shown));
    std::vector<Tool> rest(order.begin() + visible.size(), order.end());
    for (const Tool& t : visible) {
      QToolButton* b = addButton(MakeIcon(t, m_look.icon), t.label);
      b->setCheckable(t.toggle);
      b->setChecked(t.on);
      const int id = t.id;
      QObject::connect(b, &QToolButton::clicked, this, [this, id]() {
        Touched();
        Activate(id);
      });
    }
    if (!rest.empty()) {
      Tool m;
      m.symbol = "ellipsis.circle";
      QToolButton* b = addButton(MakeIcon(m, m_look.icon), Q(Tr("More", "Meer")));
      QObject::connect(b, &QToolButton::clicked, this, [this, rest, b]() {
        Touched();
        auto* menu = new QMenu(b);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (const Tool& t : rest) {
          QAction* a = menu->addAction(t.label);
          a->setCheckable(t.toggle);
          a->setChecked(t.toggle && CoreToolbar() && CoreToolbar()->GetToolState(t.id));
          const int id = t.id;
          QObject::connect(a, &QAction::triggered, menu, [id]() { Activate(id); });
        }
        ocpnios_ShowActionSheet(menu, b, Q(wxGetTranslation("Cancel")));
      });
    }
    if (mob) {
      QToolButton* b = addButton(MakeIcon(*mob, m_look.mob), mob->label);
      QObject::connect(b, &QToolButton::clicked, this, [this]() {
        Touched();
        Activate(ID_MOB);
      });
    }
    // Remember which button belongs to which tool for the state updates below.
    for (size_t i = 0; i < visible.size(); i++) m_buttons[i]->setProperty("ocpnios_toolid", visible[i].id);
    update();
  } else {
    // Only the on/off state (Track, plugin windows).
    ocpnToolBarSimple* tb = CoreToolbar();
    for (QToolButton* b : m_buttons) {
      const QVariant id = b->property("ocpnios_toolid");
      if (tb && id.isValid() && b->isCheckable()) {
        const bool on = tb->GetToolState(id.toInt());
        if (b->isChecked() != on) b->setChecked(on);
      }
    }
  }
  Place(area);
}

void IosToolbar::Place(const QRect& area) {
  const int n = (int)m_buttons.size();
  const QSize sz(kButton + 2 * kPad, n * kButton + 2 * kPad);
  for (int i = 0; i < n; i++) m_buttons[i]->move(kPad, kPad + i * kButton);
  const int top = area.top() + 8;
  // Clear of the camera island and the rounded corners (landscape), as OpenCPN's own
  // toolbar was (window_fit.cpp).
  int inset = 0;
  if (gFrame && gFrame->GetPrimaryCanvas() && parentWidget()) {
    wxWindow* canvas = gFrame->GetPrimaryCanvas();
    if (QWidget* cw = canvas->GetHandle()) {
      const QPoint p = cw->mapFrom(parentWidget(), QPoint(area.left(), top));
      inset = ocpnios_OverlayInsetLeft(canvas, p.y(), p.y() + sz.height());
    }
  }
  m_inset = std::max(8, inset);
  const int left = area.left() + m_inset;
  if (size() != sz) resize(sz);
  if (!m_anim || m_anim->state() != QAbstractAnimation::Running) {
    move(m_out ? area.left() - sz.width() - 4 : left, top);
  }
  m_area = area;
  if (handle) {
    // Never hidden, only moved off screen while the toolbar is shown: a hidden tab that
    // had just been tapped kept the next tap near it (on the Options button) and the tap
    // was lost (simulator, 26 Nov 2026).
    handle->move(m_out ? area.left() + std::max(0, m_inset - 8) : -1000, top + 8);
    if (!handle->isVisible()) handle->show();
    if (m_out) handle->raise();
  }
  setVisible(true);
  raise();
}

void IosToolbar::SlideOut() {
  if (m_out) return;
  m_out = true;
  m_autoOut = false;
  m_hideTimer.stop();
  auto* a = new QPropertyAnimation(this, "pos", this);
  a->setDuration(220);
  a->setEndValue(QPoint(m_area.left() - width() - 4, pos().y()));
  a->start(QAbstractAnimation::DeleteWhenStopped);
  m_anim = a;
  if (handle) {
    handle->move(m_area.left() + std::max(0, m_inset - 8), handle->y());
    handle->raise();
  }
}

void IosToolbar::SlideIn() {
  if (!m_out) return;
  m_out = false;
  m_autoOut = false;
  if (handle) handle->move(-1000, handle->y());
  auto* a = new QPropertyAnimation(this, "pos", this);
  a->setDuration(220);
  a->setEndValue(QPoint(m_area.left() + m_inset, pos().y()));
  a->start(QAbstractAnimation::DeleteWhenStopped);
  m_anim = a;
  Touched();
}

// Swipe left on the toolbar: away. The press goes on to the button; a swipe cancels it
// (release far outside, as PanelDrag in window_fit.cpp does).
bool IosToolbar::eventFilter(QObject* obj, QEvent* ev) {
  const auto t = ev->type();
  if (t == QEvent::Wheel) {
    wheelEvent(static_cast<QWheelEvent*>(ev));
    return true;
  }
  if (t != QEvent::MouseButtonPress && t != QEvent::MouseMove && t != QEvent::MouseButtonRelease)
    return false;
  auto* me = static_cast<QMouseEvent*>(ev);
  const QPoint gp = me->globalPosition().toPoint();
  if (t == QEvent::MouseButtonPress) {
    m_pressAt = gp;
    m_swiping = false;
    Touched();
    return false;
  }
  if (t == QEvent::MouseMove && !m_swiping) {
    const QPoint d = gp - m_pressAt;
    if (d.x() < -30 && std::abs(d.x()) > std::abs(d.y())) {
      m_swiping = true;
      QPointF far(-100000, -100000);
      QMouseEvent rel(QEvent::MouseButtonRelease, far, far, Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
      QCoreApplication::sendEvent(obj, &rel);
      if (auto* b = qobject_cast<QAbstractButton*>(obj)) b->setDown(false);
      SlideOut();
      return true;
    }
  }
  return m_swiping;
}

QPointer<IosToolbar> s_bar;
QTimer* s_timer = nullptr;

QMainWindow* MainWindow() {
  wxWindow* top = wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
  return top ? qobject_cast<QMainWindow*>(top->GetHandle()) : nullptr;
}

// Auto-hide: a tap on the chart brings the toolbar back.
class ChartTap : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* obj, QEvent* ev) override {
    if (ev->type() == QEvent::MouseButtonPress && s_bar && s_autoHide && obj->isWidgetType()) {
      QWidget* w = static_cast<QWidget*>(obj);
      if (w->window() == MainWindow() && !s_bar->isAncestorOf(w) && w != s_bar &&
          !(s_bar->handle && (w == s_bar->handle))) {
        if (s_bar->IsOut()) s_bar->SlideIn();
        else s_bar->Touched();
      }
    }
    return false;
  }
};

// GlobalToolbarConfig to disk at once: OpenCPN only writes it on exit or when the app
// goes to the background, and iOS may end the app before that.
void SaveToolbarConfig() {
  if (!pConfig) return;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath("/Settings");
  pConfig->Write("GlobalToolbarConfig", g_toolbarConfig);
  pConfig->SetPath(old);
  pConfig->Flush();
}

// First start with this toolbar: Print and Help off by default; they can be switched
// on in Options > Toolbar. Same characters of GlobalToolbarConfig as on the desktop.
// Once OpenCPN's toolbar exists (it is built after the end of startup).
void ApplyDefaultsOnce() {
  // Auto-hide delay: OpenCPN's default of 2 s is for a mouse; on a touch screen the
  // toolbar was gone before the next tap (simulator, 26 Nov 2026). 5 s once, after that
  // the user's value from Options > User Interface.
  static bool delayChecked = false;
  if (!delayChecked && pConfig) {
    delayChecked = true;
    const wxString old = pConfig->GetPath();
    pConfig->SetPath(kPath);
    long d = 0;
    pConfig->Read("ToolbarAutoHideDelaySet", &d, 0L);
    if (!d) {
      pConfig->Write("ToolbarAutoHideDelaySet", 1L);
      if (g_nAutoHideToolbar <= 2) g_nAutoHideToolbar = 5;
      pConfig->SetPath("/Settings");
      pConfig->Write("AutoHideToolbarSecs", (long)g_nAutoHideToolbar);
      pConfig->Flush();
    }
    pConfig->SetPath(old);
  }
  static bool checked = false;
  if (checked || !pConfig || !g_MainToolbar || !g_MainToolbar->m_FloatingToolbarConfigMenu)
    return;
  checked = true;
  const wxString old = pConfig->GetPath();
  pConfig->SetPath(kPath);
  long done = 0;
  pConfig->Read("ToolbarDefaults", &done, 0L);
  if (done) {
    pConfig->SetPath(old);
    return;
  }
  pConfig->Write("ToolbarDefaults", 1L);
  pConfig->SetPath(old);
  wxMenu* m = g_MainToolbar->m_FloatingToolbarConfigMenu;
  for (int id : {ID_PRINT, ID_ABOUT}) {
    size_t pos = 0;
    if (m->FindChildItem(id + 100, &pos)) {
      while (g_toolbarConfig.Len() <= pos) g_toolbarConfig += 'X';
      g_toolbarConfig.SetChar(pos, '.');
    }
  }
  SaveToolbarConfig();
  g_MainToolbar->SetToolConfigString(g_toolbarConfig);
  if (gFrame) gFrame->RequestNewMasterToolbar(true);
}

void Tick() {
  QMainWindow* mw = MainWindow();
  if (!mw || !g_MainToolbar) return;
  g_disable_main_toolbar = true;  // a plugin could have switched it back on
  ApplyDefaultsOnce();
  if (!g_MainToolbar) return;
  // OpenCPN's hidden toolbar must not fade itself (that collapses it to the ☰ and
  // changes its tool list); auto-hide is done by the iOS toolbar.
  g_MainToolbar->SetAutoHide(false);
  // Without OpenGL OpenCPN draws its toolbar through DrawDC, which does not look at
  // g_disable_main_toolbar: keep it below the screen (also with OpenGL, so a switch
  // between the two shows nothing).
  if (g_MainToolbar->GetToolbarPosition().y < 50000) {
    g_MainToolbar->SetULDockPosition(wxPoint(-1, 100000));
    g_MainToolbar->SetDefaultPosition();
  }
  if (!s_bar) {
    s_bar = new IosToolbar(mw);
    s_bar->handle = new Handle(mw, s_bar);
    s_bar->handle->move(-1000, 0);
    qApp->installEventFilter(new ChartTap(qApp));
    s_bar->Touched();
  }
  QWidget* central = mw->centralWidget();
  s_bar->Sync(central ? central->geometry() : mw->rect());
}

}  // namespace

// Start (from ocpnios_OnStartupDone, frame_ios.cpp).
void ocpnios_ToolbarInit() {
  if (s_timer) return;
  LoadSettings();
  // MOB is always on the toolbar (Xander, 26 Nov 2026).
  g_bPermanentMOBIcon = true;
  s_timer = new QTimer(qApp);
  s_timer->setInterval(500);
  QObject::connect(s_timer, &QTimer::timeout, qApp, []() { Tick(); });
  s_timer->start();
  Tick();
}

// Where the toolbar is on the screen (global points), empty when hidden; for placing a
// floating panel next to it (window_fit.cpp).
QRect ocpnios_ToolbarRect() {
  if (!s_bar || !s_bar->isVisible() || s_bar->IsOut()) return QRect();
  return QRect(s_bar->mapToGlobal(QPoint(0, 0)), s_bar->size());
}

// Place the toolbar again right away (after rotating, map_controls_ios.cpp).
void ocpnios_ToolbarRelayout() {
  if (s_timer) Tick();
}

// ---- Options > Toolbar -----------------------------------------------------------

wxString ocpnios_ToolbarPageTitle() { return Tr("Toolbar", "Werkbalk"); }

namespace {
wxScrolledWindow* s_page = nullptr;
void BuildToolbarPageContents(wxScrolledWindow* page);
}  // namespace

void ocpnios_BuildToolbarPage(wxScrolledWindow* page) {
  s_page = page;
  page->Bind(wxEVT_DESTROY, [page](wxWindowDestroyEvent& e) {
    e.Skip();
    if (e.GetEventObject() == page) s_page = nullptr;
  });
  BuildToolbarPageContents(page);
}

// Options is built during startup, before OpenCPN's toolbar and the plugin buttons
// exist; fill the page again each time it is opened (options_ios.cpp).
void ocpnios_RefreshToolbarPage(wxWindow* shownPage) {
  if (!s_page || !shownPage) return;
  bool inside = false;
  for (wxWindow* w = s_page; w; w = w->GetParent())
    if (w == shownPage) inside = true;
  if (!inside) return;
  ocpnios_ForgetPage(s_page);  // fit_width.cpp: card painters point into the page
  s_page->DestroyChildren();
  s_page->SetSizer(nullptr);
  BuildToolbarPageContents(s_page);
  s_page->Layout();
  s_page->FitInside();
}

namespace {
void BuildToolbarPageContents(wxScrolledWindow* page) {
  LoadSettings();
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  page->SetSizer(sizer);

  // Show always / hide automatically: radio buttons side by side become a segmented
  // control (fit_width.cpp).
  auto* form = new wxFlexGridSizer(2, 8, 8);
  form->AddGrowableCol(1);
  form->Add(new wxStaticText(page, wxID_ANY, Tr("Show toolbar", "Werkbalk tonen")), 0,
            wxALIGN_CENTER_VERTICAL);
  auto* seg = new wxBoxSizer(wxHORIZONTAL);
  auto* always = new wxRadioButton(page, wxID_ANY, Tr("Always", "Altijd"),
                                   wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
  auto* autoHide = new wxRadioButton(page, wxID_ANY, Tr("Hide automatically",
                                                        "Automatisch"));
  (s_autoHide ? autoHide : always)->SetValue(true);
  seg->Add(always, 0, wxALIGN_CENTER_VERTICAL);
  seg->Add(autoHide, 0, wxALIGN_CENTER_VERTICAL);
  form->Add(seg, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
  sizer->Add(form, 0, wxEXPAND | wxALL, 12);
  auto onMode = [autoHide](wxCommandEvent& e) {
    e.Skip();
    s_autoHide = autoHide->GetValue();
    // Options > User Interface shows the same setting; Done reads it from there.
    if (s_autoHideCB) s_autoHideCB->SetValue(s_autoHide);
    SaveSettings();
    if (s_bar) s_bar->Touched();
  };
  always->Bind(wxEVT_RADIOBUTTON, onMode);
  autoHide->Bind(wxEVT_RADIOBUTTON, onMode);

  // Built-in buttons: GlobalToolbarConfig. All buttons of OpenCPN's config menu, also
  // the ones switched off now.
  auto* box = new wxStaticBoxSizer(wxVERTICAL, page, Tr("Buttons", "Knoppen"));
  if (g_MainToolbar && g_MainToolbar->m_FloatingToolbarConfigMenu) {
    wxMenu* m = g_MainToolbar->m_FloatingToolbarConfigMenu;
    for (size_t pos = 0; pos < m->GetMenuItemCount(); pos++) {
      wxMenuItem* item = m->FindItemByPosition(pos);
      if (!item) continue;
      const int id = item->GetId() - 100;
      auto* cb = new wxCheckBox(box->GetStaticBox(), wxID_ANY, item->GetItemLabelText());
      cb->SetValue(item->IsChecked());
      box->Add(cb, 0, wxEXPAND | wxALL, 6);
      if (id == ID_MOB) {  // never off
        cb->SetValue(true);
        cb->Disable();
        continue;
      }
      cb->Bind(wxEVT_CHECKBOX, [pos](wxCommandEvent& e) {
        e.Skip();
        while (g_toolbarConfig.Len() <= pos) g_toolbarConfig += 'X';
        g_toolbarConfig.SetChar(pos, e.IsChecked() ? 'X' : '.');
        SaveToolbarConfig();
        if (g_MainToolbar) g_MainToolbar->SetToolConfigString(g_toolbarConfig);
        if (gFrame) gFrame->RequestNewMasterToolbar(true);
      });
    }
  }
  // MOB when it is permanent (not in the config menu).
  {
    auto* cb = new wxCheckBox(box->GetStaticBox(), wxID_ANY, Tr("MOB", "MOB"));
    bool listed = false;
    if (g_MainToolbar && g_MainToolbar->m_FloatingToolbarConfigMenu)
      listed = g_MainToolbar->m_FloatingToolbarConfigMenu->FindItem(ID_MOB + 100) != nullptr;
    if (listed) {
      cb->Destroy();
    } else {
      cb->SetValue(true);
      cb->Disable();
      box->Add(cb, 0, wxEXPAND | wxALL, 6);
    }
  }
  sizer->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  // Plugin buttons: own iOS key.
  std::vector<Tool> all = CoreTools(true);
  bool any = false;
  for (const Tool& t : all) any = any || t.plugin;
  if (any) {
    auto* pbox = new wxStaticBoxSizer(wxVERTICAL, page, Tr("Plugins", "Plugins"));
    for (const Tool& t : all) {
      if (!t.plugin) continue;
      auto* cb = new wxCheckBox(pbox->GetStaticBox(), wxID_ANY,
                                wxString::FromUTF8(t.label.toUtf8().constData()));
      cb->SetValue(!s_hidden.count(t.key));
      pbox->Add(cb, 0, wxEXPAND | wxALL, 6);
      const wxString key = t.key;
      cb->Bind(wxEVT_CHECKBOX, [key](wxCommandEvent& e) {
        e.Skip();
        if (e.IsChecked()) s_hidden.erase(key);
        else s_hidden.insert(key);
        SaveSettings();
        if (s_bar) Tick();
      });
    }
    sizer->Add(pbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }
}

}  // namespace

// Options > User Interface: "Hide toolbar after ... seconds" is the same setting as
// Options > Toolbar > Automatically (options_ios.cpp registers it). Live both ways.
void ocpnios_SetAutoHideCheckbox(wxCheckBox* cb) {
  s_autoHideCB = cb;
  if (!cb || cb->GetClientData()) return;
  cb->SetClientData(cb);
  cb->Bind(wxEVT_CHECKBOX, [](wxCommandEvent& e) {
    e.Skip();
    g_bAutoHideToolbar = e.IsChecked();
    SaveSettings();
    if (s_bar) s_bar->Touched();
  });
  cb->Bind(wxEVT_DESTROY, [cb](wxWindowDestroyEvent& e) {
    e.Skip();
    if (e.GetEventObject() == cb && s_autoHideCB == cb) s_autoHideCB = nullptr;
  });
}

#endif  // __OCPN__IOS__
