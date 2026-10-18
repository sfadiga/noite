#include "board/ChipBar.h"

#include "board/CardDelegate.h"
#include "board/FlowLayout.h"

#include <QPainter>
#include <QToolButton>

namespace {

QString dateKey(noite::DateChip chip)
{
    switch (chip) {
    case noite::DateChip::Overdue:
        return QStringLiteral("date:overdue");
    case noite::DateChip::DueToday:
        return QStringLiteral("date:today");
    case noite::DateChip::NoDate:
        return QStringLiteral("date:none");
    }
    return {};
}

QString dateLabel(noite::DateChip chip)
{
    switch (chip) {
    case noite::DateChip::Overdue:
        return ChipBar::tr("Overdue");
    case noite::DateChip::DueToday:
        return ChipBar::tr("Due today");
    case noite::DateChip::NoDate:
        return ChipBar::tr("No date");
    }
    return {};
}

QIcon swatch(noite::Color color, const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(CardDelegate::tint(color, dark));
    painter.setPen(CardDelegate::cardBorder(palette));
    painter.drawEllipse(QRectF(0.5, 0.5, 11, 11));
    return QIcon(pixmap);
}

} // namespace

ChipBar::ChipBar(QWidget* parent)
    : QWidget(parent)
    , layout_(new FlowLayout(this, 6))
{
    qRegisterMetaType<noite::BoardFilter>();
    hide();
}

void ChipBar::setFacets(const noite::ChipFacets& facets, const noite::BoardFilter& active)
{
    active_ = active;
    // Not delete: this runs inside a chip's own clicked() when the board answers a toggle.
    for (QAbstractButton* chip : std::as_const(chips_)) {
        chip->hide(); // the flow layout skips hidden items until the chip is gone
        chip->deleteLater();
    }
    chips_.clear();

    for (const auto& [color, count] : facets.colors)
        addChip("color:" + noite::colorName(color), QString::number(count), active.colors.contains(color),
                swatch(color, palette()));
    for (const auto& [date, count] : facets.dates)
        addChip(dateKey(date), QStringLiteral("%1 %2").arg(dateLabel(date)).arg(count), active.dates.contains(date));
    for (const auto& [tag, count] : facets.tags)
        addChip("tag:" + tag, QStringLiteral("#%1 %2").arg(tag).arg(count), active.tags.contains(tag));
    setVisible(!chips_.isEmpty());
    updateGeometry();
}

void ChipBar::addChip(const QString& key, const QString& text, bool active, const QIcon& icon)
{
    auto* chip = new QToolButton(this);
    chip->setProperty("chipKey", key);
    chip->setText(text);
    chip->setCheckable(true);
    chip->setChecked(active);
    chip->setAutoRaise(false);
    chip->setFocusPolicy(Qt::NoFocus); // the grid keeps the keyboard
    chip->setCursor(Qt::PointingHandCursor);
    if (!icon.isNull()) {
        chip->setIcon(icon);
        chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    connect(chip, &QToolButton::clicked, this, [this, key] { toggle(key); });
    layout_->addWidget(chip);
    chips_.append(chip);
}

void ChipBar::toggle(const QString& key)
{
    noite::BoardFilter next = active_;
    const QString value = key.section(':', 1);
    if (key.startsWith("color:")) {
        if (const auto color = noite::parseColor(value)) {
            if (!next.colors.remove(*color))
                next.colors.insert(*color);
        }
    } else if (key.startsWith("date:")) {
        const noite::DateChip date = value == "overdue" ? noite::DateChip::Overdue
            : value == "today"                          ? noite::DateChip::DueToday
                                                        : noite::DateChip::NoDate;
        if (!next.dates.remove(date))
            next.dates.insert(date);
    } else if (key.startsWith("tag:")) {
        if (!next.tags.removeOne(value))
            next.tags.append(value);
    }
    emit filterChanged(next);
}

QAbstractButton* ChipBar::chip(const QString& key) const
{
    for (QAbstractButton* chip : chips_)
        if (chip->property("chipKey").toString() == key)
            return chip;
    return nullptr;
}

QStringList ChipBar::keys() const
{
    QStringList out;
    for (const QAbstractButton* chip : chips_)
        out << chip->property("chipKey").toString();
    return out;
}
