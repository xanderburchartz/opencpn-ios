// Navigatielijsten in iOS-stijl ("inset grouped", zoals de Instellingen-app van iOS):
// regels op een witte kaart met afgeronde hoeken, op de achtergrond van het venster;
// elke regel rechts een pijltje (›), een dunne scheidingslijn die bij de tekst begint,
// en optioneel links een iOS-symbool in een gekleurd vierkantje.
//
// Xander (10 nov 2026): regels zagen er niet aantikbaar uit; later: Instellingen had twee
// stijlen door elkaar (iOS-kopregel boven een desktoplijst met rand en OpenCPN-
// pictogrammen). Alleen voor lijsten die naar een volgend scherm gaan.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAbstractItemView>
#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QScroller>
#include <QScrollerProperties>
#include <QStyledItemDelegate>

#include <string>
#include <vector>

QImage ocpnios_SymbolImage(const char* name, int pointSize);  // ios_look.mm

namespace {

constexpr int kSide = 16;      // kaart: ruimte links en rechts
constexpr int kRadius = 10;    // afgeronde hoeken
constexpr int kChevronW = 28;  // ruimte voor ›
constexpr int kIcon = 29;      // gekleurd vierkantje (iOS: 29 pt)

bool IsDark() {
  return QCoreApplication::instance() &&
         QCoreApplication::instance()->property("ocpnios_dark").toBool();
}

struct Symbol {
  QString name;
  QColor color;
};

class NavDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  std::vector<Symbol> symbols;  // per regel

  QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& ix) const override {
    QSize s = QStyledItemDelegate::sizeHint(opt, ix);
    s.setHeight(std::max(s.height(), 50));
    return s;
  }

  void paint(QPainter* p, const QStyleOptionViewItem& opt,
             const QModelIndex& ix) const override {
    const int rows = ix.model() ? ix.model()->rowCount(ix.parent()) : 1;
    const bool first = ix.row() == 0, last = ix.row() == rows - 1;
    const QRect card = opt.rect.adjusted(kSide, 0, -kSide, 0);
    const bool pressed = (opt.state & QStyle::State_Selected) && QApplication::mouseButtons();

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    // Kaart: alleen de eerste en laatste regel met ronde hoeken.
    QPainterPath path;
    const QRectF r(card.left(), card.top(), card.width(), card.height());
    const double k = kRadius;
    path.moveTo(r.left(), r.top() + (first ? k : 0));
    if (first) path.arcTo(r.left(), r.top(), 2 * k, 2 * k, 180, -90);
    else path.lineTo(r.left(), r.top());
    path.lineTo(r.right() - (first ? k : 0), r.top());
    if (first) path.arcTo(r.right() - 2 * k, r.top(), 2 * k, 2 * k, 90, -90);
    path.lineTo(r.right(), r.bottom() - (last ? k : 0));
    if (last) path.arcTo(r.right() - 2 * k, r.bottom() - 2 * k, 2 * k, 2 * k, 0, -90);
    else path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left() + (last ? k : 0), r.bottom());
    if (last) path.arcTo(r.left(), r.bottom() - 2 * k, 2 * k, 2 * k, 270, -90);
    path.closeSubpath();
    const bool dark = IsDark();
    QColor bg = dark ? QColor(0x1c, 0x1c, 0x1e) : QColor(Qt::white);
    if (pressed) bg = dark ? QColor(0x3a, 0x3a, 0x3c) : QColor(0xd1, 0xd1, 0xd6);
    p->fillPath(path, bg);

    // Symbool in een gekleurd vierkantje.
    int textLeft = card.left() + 16;
    const Symbol* sym = ix.row() < (int)symbols.size() && !symbols[ix.row()].name.isEmpty()
                            ? &symbols[ix.row()] : nullptr;
    if (sym) {
      const QRect box(card.left() + 16, card.center().y() - kIcon / 2, kIcon, kIcon);
      QPainterPath bp;
      bp.addRoundedRect(QRectF(box), 7, 7);
      // Nachtstand: gedempte kleuren (felle vlakken verstoren het nachtzicht).
      if (dark) p->setOpacity(0.45);
      p->fillPath(bp, sym->color);
      const QImage img = Image(sym->name);
      if (!img.isNull()) {
        const QSizeF is = img.deviceIndependentSize();
        p->drawImage(QPointF(box.left() + (kIcon - is.width()) / 2.0,
                             box.top() + (kIcon - is.height()) / 2.0), img);
      }
      p->setOpacity(1.0);
      textLeft = box.right() + 14;
    }

    // Tekst (één regel; categorienamen hebben een harde regelovergang).
    QString text = ix.data(Qt::DisplayRole).toString();
    text.replace('\n', ' ');
    p->setPen(dark ? QColor(Qt::white) : QColor(Qt::black));
    p->setFont(opt.font);
    const QRect tr(textLeft, card.top(), card.right() - kChevronW - textLeft, card.height());
    p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetrics(opt.font).elidedText(text, Qt::ElideRight, tr.width()));

    // Pijltje ›
    QPen pen(QColor(0xc4, 0xc4, 0xc7), 2.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p->setPen(pen);
    const int cx = card.right() - kChevronW / 2;
    const int cy = card.center().y();
    p->drawLine(QPointF(cx - 3, cy - 7), QPointF(cx + 4, cy));
    p->drawLine(QPointF(cx + 4, cy), QPointF(cx - 3, cy + 7));
    // Scheidingslijn vanaf de tekst, niet onder de laatste regel.
    if (!last) {
      p->setPen(QPen(dark ? QColor(0x38, 0x38, 0x3a) : QColor(0xc6, 0xc6, 0xc8), 0.6));
      p->drawLine(QPointF(textLeft, card.bottom() + 0.5),
                  QPointF(card.right(), card.bottom() + 0.5));
    }
    p->restore();
  }

 private:
  QImage Image(const QString& name) const {
    auto it = m_cache.find(name);
    if (it != m_cache.end()) return it.value();
    QImage img = ocpnios_SymbolImage(name.toUtf8().constData(), 17);
    m_cache.insert(name, img);
    return img;
  }
  mutable QHash<QString, QImage> m_cache;
};

}  // namespace

void ocpnios_MakeNavList(QAbstractItemView* view) {
  if (!view || view->property("ocpnios_navlist").toBool()) return;
  view->setProperty("ocpnios_navlist", true);
  view->setItemDelegate(new NavDelegate(view));
  // Achtergrond van het venster rond de kaart, geen rand; ruimte boven de kaart.
  if (auto* fr = qobject_cast<QFrame*>(view)) fr->setFrameShape(QFrame::NoFrame);
  view->setAutoFillBackground(false);
  view->viewport()->setAutoFillBackground(false);
  view->setStyleSheet("QAbstractItemView { background: transparent; border: none;"
                      " padding-top: 12px; }");
  // Vegen met de vinger zoals in touch_ui.cpp, maar zonder drukvertraging. Met de
  // vertraging (0,15 s: QScroller houdt de druk vast en speelt hem later af) werd na
  // twee keer "terug" de eerste tik op deze lijst soms niet afgeleverd: geen
  // `clicked`, tijdsafhankelijk (iPhone-simulator, 11 nov 2026; met meetlog wél goed).
  // Een navigatielijst kiest bij het loslaten, een vertraging heeft hier geen nut.
  QWidget* vp = view->viewport();
  vp->setProperty("ocpnios_kinetic", true);  // touch_ui.cpp: niet nogmaals instellen
  QScroller::grabGesture(vp, QScroller::LeftMouseButtonGesture);
  QScroller* sc = QScroller::scroller(vp);
  QScrollerProperties sp = sc->scrollerProperties();
  sp.setScrollMetric(QScrollerProperties::DragStartDistance, 0.002);
  sp.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0.0);
  sp.setScrollMetric(QScrollerProperties::DecelerationFactor, 0.6);
  sp.setScrollMetric(QScrollerProperties::MaximumVelocity, 0.15);
  const QVariant off = QVariant::fromValue<QScrollerProperties::OvershootPolicy>(
      QScrollerProperties::OvershootAlwaysOff);
  sp.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, off);
  sp.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy, off);
  sc->setScrollerProperties(sp);
}

// Symbolen per regel (SF Symbols-naam en kleur 0xRRGGBB); naam leeg = geen symbool.
void ocpnios_SetNavSymbols(QAbstractItemView* view,
                           const std::vector<std::pair<std::string, unsigned>>& syms) {
  if (!view) return;
  auto* d = dynamic_cast<NavDelegate*>(view->itemDelegate());
  if (!d) return;
  d->symbols.clear();
  for (const auto& s : syms)
    d->symbols.push_back({QString::fromStdString(s.first), QColor::fromRgb(s.second)});
  view->viewport()->update();
}
