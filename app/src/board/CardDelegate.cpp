#include "board/CardDelegate.h"

#include "board/BoardModel.h"
#include "editor/AttachmentStore.h"

#include <noite/error.h>

#include <QDateTime>
#include <QDebug>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kMargin = 6;   // between cards
constexpr int kPadding = 12; // inside a card
constexpr int kRadius = 8;
constexpr int kThumb = 64;
constexpr int kPreviewLines = 4;

struct Tint {
    noite::Color color;
    QRgb light;
    QRgb dark;
};

// Pastel on light, muted on dark, so the palette's text colour reads on both.
constexpr Tint kTints[] = {
    {noite::Color::Red, 0xF8D7D5, 0x5C2B29},    {noite::Color::Orange, 0xFBE3C8, 0x614A19},
    {noite::Color::Yellow, 0xFFF4BF, 0x635D19}, {noite::Color::Green, 0xD9F2D0, 0x345920},
    {noite::Color::Blue, 0xD3E3FD, 0x1E3A5F},   {noite::Color::Purple, 0xE8DAF8, 0x42275E},
};

// The preview without the title line it starts with.
QString previewBody(const noite::ScratchSummary& s)
{
    QString text = s.preview;
    if (!s.title.isEmpty() && text.startsWith(s.title))
        text = text.mid(s.title.size());
    return text.trimmed();
}

} // namespace

CardDelegate::CardDelegate(AttachmentStore* attachments, QObject* parent)
    : QStyledItemDelegate(parent)
    , attachments_(attachments)
{
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return kCardSize;
}

QString CardDelegate::badgeText(const noite::ScratchSummary& s, QDate today)
{
    if (s.status != noite::Status::Active)
        return {};
    if (s.due) {
        if (*s.due < today)
            return QStringLiteral("overdue");
        if (*s.due == today)
            return QStringLiteral("today");
        return QLocale().toString(*s.due, QStringLiteral("d MMM"));
    }
    const qint64 days = QDateTime::fromMSecsSinceEpoch(s.createdAt).date().daysTo(today);
    return days > 0 ? QStringLiteral("%1d").arg(days) : QString();
}

QColor CardDelegate::tint(std::optional<noite::Color> color, bool dark)
{
    if (!color)
        return {};
    for (const Tint& t : kTints) {
        if (t.color == *color)
            return QColor(dark ? t.dark : t.light);
    }
    return {};
}

int CardDelegate::titleWidth()
{
    return kCardSize.width() - 2 * kMargin - 2 * kPadding;
}

QString CardDelegate::elidedTitle(const QString& title, const QFont& font, int width)
{
    return QFontMetrics(font).elidedText(title, Qt::ElideRight, width);
}

QPixmap CardDelegate::thumbnail(const QString& attachmentId) const
{
    if (QPixmap* cached = thumbnails_.object(attachmentId))
        return *cached;
    QPixmap pixmap;
    QImage image;
    try {
        image = QImage::fromData(attachments_->bytes(attachmentId));
    } catch (const noite::Error& error) {
        qWarning() << "thumbnail:" << error.what();
    }
    if (!image.isNull())
        pixmap = QPixmap::fromImage(image.scaled(kThumb, kThumb, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    thumbnails_.insert(attachmentId, new QPixmap(pixmap)); // a failed load is cached as null
    return pixmap;
}

void CardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = static_cast<const BoardModel*>(index.model());
    const noite::ScratchSummary& s = model->summary(index.row());
    const QPalette& palette = option.palette;
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    const bool selected = option.state & QStyle::State_Selected;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(option.rect).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    QPainterPath shape;
    shape.addRoundedRect(card, kRadius, kRadius);
    const QColor fill = tint(s.color, dark);
    painter->fillPath(shape, fill.isValid() ? fill : palette.color(QPalette::Base));
    QColor border = selected ? palette.color(QPalette::Highlight) : palette.color(QPalette::Mid);
    if (!selected)
        border.setAlpha(90);
    painter->setPen(QPen(border, selected ? 2 : 1));
    painter->drawPath(shape);

    const QRect content = card.toRect().adjusted(kPadding, kPadding - 2, -kPadding, -kPadding + 2);
    painter->setClipRect(content);
    painter->setPen(palette.color(QPalette::Text));

    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);
    painter->setFont(titleFont);
    painter->drawText(QRect(content.left(), content.top(), content.width(), titleMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, elidedTitle(s.title, titleFont, content.width()));

    const QFontMetrics metrics(option.font);
    const int bodyTop = content.top() + titleMetrics.height() + 4;
    int previewWidth = content.width();
    if (attachments_ && s.firstImage) {
        const QPixmap thumb = thumbnail(*s.firstImage);
        if (!thumb.isNull()) {
            painter->drawPixmap(content.right() - thumb.width() + 1, bodyTop, thumb);
            previewWidth -= kThumb + 8;
        }
    }
    painter->setFont(option.font);
    painter->drawText(QRect(content.left(), bodyTop, previewWidth, metrics.lineSpacing() * kPreviewLines),
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, previewBody(s));

    QFont small = option.font;
    small.setPointSizeF(option.font.pointSizeF() * 0.85);
    const QFontMetrics smallMetrics(small);
    painter->setFont(small);
    const QRect footer(content.left(), content.bottom() - smallMetrics.height() + 1, content.width(),
                       smallMetrics.height());
    const QString badge = badgeText(s, model->today());
    const int badgeWidth = badge.isEmpty() ? 0 : smallMetrics.horizontalAdvance(badge) + 8;
    painter->drawText(footer, Qt::AlignRight | Qt::AlignVCenter, badge);
    painter->drawText(footer.adjusted(0, 0, -badgeWidth, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(s.tags.join(' '), Qt::ElideRight, footer.width() - badgeWidth));
    painter->restore();
}
