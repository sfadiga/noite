#include "board/TabStrip.h"

#include "Theme.h"
#include "board/CardDelegate.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kHPadding = 18;
constexpr int kVPadding = 5;
constexpr int kRadius = 6;

QFont countFont(const QFont& label)
{
    QFont font = label;
    font.setPointSizeF(label.pointSizeF() * 0.85);
    font.setWeight(QFont::Normal);
    return font;
}

} // namespace

TabButton::TabButton(const QString& label, QWidget* parent)
    : QAbstractButton(parent)
    , label_(label.toUpper())
    , count_(QStringLiteral("0"))
{
    setCheckable(true);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus); // the grid keeps the keyboard
    setCursor(Qt::PointingHandCursor);
}

void TabButton::setCount(int count)
{
    const QString text = QString::number(count);
    if (text != count_) {
        count_ = text;
        update();
    }
}

QSize TabButton::sizeHint() const
{
    const QFontMetrics label(font());
    const QFontMetrics count(countFont(font()));
    return {label.horizontalAdvance(label_) + 2 * kHPadding, label.height() + count.height() + 2 * kVPadding};
}

void TabButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    QColor text = pal.color(QPalette::Text);
    if (isChecked()) {
        painter.fillPath(shape, pal.color(QPalette::Highlight));
        text = pal.color(QPalette::HighlightedText);
    } else {
        QColor fill = CardDelegate::cardFill(std::nullopt, pal);
        if (underMouse())
            fill = CardDelegate::cardBorder(pal);
        painter.fillPath(shape, fill);
        painter.setPen(CardDelegate::cardBorder(pal));
        painter.drawPath(shape);
    }

    const QFontMetrics labelMetrics(font());
    const QFont small = countFont(font());
    const QRect inner = rect().adjusted(0, kVPadding, 0, -kVPadding);
    painter.setPen(text);
    painter.setFont(font());
    painter.drawText(QRect(inner.left(), inner.top(), inner.width(), labelMetrics.height()), Qt::AlignCenter, label_);
    painter.setFont(small);
    painter.drawText(QRect(inner.left(), inner.top() + labelMetrics.height(), inner.width(),
                           inner.height() - labelMetrics.height()),
                     Qt::AlignHCenter | Qt::AlignTop, count_);
}

TabStrip::TabStrip(QWidget* parent)
    : QWidget(parent)
    , group_(new QButtonGroup(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    int index = 0;
    for (const QString& name : {tr("Today"), tr("All"), tr("Completed"), tr("Trash")}) {
        auto* button = new TabButton(name, this);
        buttons_.append(button);
        group_->addButton(button, index++);
        layout->addWidget(button);
    }
    group_->setExclusive(true);
    buttons_.first()->setChecked(true);
    connect(group_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked)
            emit currentChanged(id);
    });
    sizeButtons();
}

int TabStrip::currentIndex() const
{
    return group_->checkedId();
}

void TabStrip::setCurrentIndex(int index)
{
    if (index >= 0 && index < count())
        buttons_.at(index)->setChecked(true);
}

void TabStrip::setCounts(const noite::TabCounts& counts)
{
    for (int i = 0; i < count(); ++i)
        buttons_.at(i)->setCount(counts.of(noite::Tab(i)));
}

int TabStrip::buttonHeight() const
{
    return buttons_.first()->height();
}

void TabStrip::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        sizeButtons();
}

// Equal buttons, sized for the widest label.
void TabStrip::sizeButtons()
{
    const QFont header = theme::headerFont(font());
    QSize size;
    for (TabButton* button : buttons_) {
        button->setFont(header);
        size = size.expandedTo(button->sizeHint());
    }
    for (TabButton* button : buttons_)
        button->setFixedSize(size);
}
