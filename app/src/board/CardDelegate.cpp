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

QString CardDelegate::badgeText(const noite::ScratchSummary& s, QDate today, qint64 now)
{
    if (s.status != noite::Status::Active)
        return {};
    const QString age = ageText(s.createdAt, today, now);
    if (!s.due)
        return age;
    QString due;
    if (*s.due < today)
        due = QStringLiteral("overdue");
    else if (*s.due == today)
        due = QStringLiteral("today");
    else
        due = QLocale().toString(*s.due, QStringLiteral("d MMM"));
    return due + QStringLiteral(" · ") + age;
}

QString CardDelegate::ageText(qint64 createdAt, QDate today, qint64 now)
{
    const qint64 days = QDateTime::fromMSecsSinceEpoch(createdAt).date().daysTo(today);
    if (days > 0)
        return QStringLiteral("%1d").arg(days);
    const qint64 hours = (now - createdAt) / 3'600'000;
    return hours > 0 ? QStringLiteral("%1h").arg(hours) : QStringLiteral("now");
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

namespace {

// `from` moved `amount` of the way towards `to`.
QColor blend(const QColor& from, const QColor& to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Base).lightness() < 128;
}

} // namespace

QColor CardDelegate::cardFill(std::optional<noite::Color> color, const QPalette& palette)
{
    if (const QColor t = tint(color, isDark(palette)); t.isValid())
        return t;
    return blend(palette.color(QPalette::Base), palette.color(QPalette::Text), 0.07);
}

QColor CardDelegate::cardBorder(const QPalette& palette)
{
    return blend(palette.color(QPalette::Base), palette.color(QPalette::Text), 0.28);
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

QStringList CardDelegate::stackLines(const BoardRow& stack)
{
    QStringList lines = {stack.bundleName,
                         stack.members.size() == 1 ? QStringLiteral("1 note")
                                                   : QStringLiteral("%1 notes").arg(stack.members.size())};
    for (qsizetype i = 0; i < std::min<qsizetype>(2, stack.members.size()); ++i)
        lines << stack.members[i].title;
    return lines;
}

void CardDelegate::paintStack(QPainter* painter, const QStyleOptionViewItem& option, const BoardRow& stack,
                              QDate today) const
{
    const QPalette& palette = option.palette;
    const bool selected = option.state & QStyle::State_Selected;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // Two cards peeking out behind the front one.
    const QRectF front = QRectF(option.rect).adjusted(kMargin, kMargin + 8, -kMargin - 8, -kMargin);
    for (int offset : {8, 4}) {
        QPainterPath back;
        back.addRoundedRect(front.translated(offset, -offset), kRadius, kRadius);
        painter->fillPath(back, cardFill(std::nullopt, palette));
        painter->setPen(QPen(cardBorder(palette), 1));
        painter->drawPath(back);
    }
    QPainterPath shape;
    shape.addRoundedRect(front, kRadius, kRadius);
    painter->fillPath(shape, cardFill(stack.members.first().color, palette));
    painter->setPen(QPen(selected ? palette.color(QPalette::Highlight) : cardBorder(palette), selected ? 2 : 1));
    painter->drawPath(shape);

    const QRect content = front.toRect().adjusted(kPadding, kPadding - 2, -kPadding, -kPadding + 2);
    painter->setClipRect(content);
    painter->setPen(palette.color(QPalette::Text));
    const QStringList lines = stackLines(stack);
    QFont titleFont = option.font;
    titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);
    painter->setFont(titleFont);
    const QString name = stack.expanded ? lines[0] + QStringLiteral("  ▾") : lines[0];
    painter->drawText(QRect(content.left(), content.top(), content.width(), titleMetrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, elidedTitle(name, titleFont, content.width()));
    painter->setFont(option.font);
    const QFontMetrics metrics(option.font);
    int y = content.top() + titleMetrics.height() + 4;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        painter->drawText(QRect(content.left(), y, content.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(i == 1 ? lines[i] : QStringLiteral("· ") + lines[i], Qt::ElideRight,
                                             content.width()));
        y += metrics.lineSpacing();
    }

    // The age of the oldest member: how long the bundle's work has been waiting.
    qint64 oldest = stack.members.first().createdAt;
    for (const noite::ScratchSummary& m : stack.members)
        oldest = std::min(oldest, m.createdAt);
    QFont small = option.font;
    small.setPointSizeF(option.font.pointSizeF() * 0.85);
    painter->setFont(small);
    const QFontMetrics smallMetrics(small);
    painter->drawText(QRect(content.left(), content.bottom() - smallMetrics.height() + 1, content.width(),
                            smallMetrics.height()),
                      Qt::AlignRight | Qt::AlignVCenter, ageText(oldest, today, QDateTime::currentMSecsSinceEpoch()));
    painter->restore();
}

void CardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto* model = static_cast<const BoardModel*>(index.model());
    const BoardRow& row = model->row(index.row());
    if (row.kind == BoardRow::Kind::Stack) {
        paintStack(painter, option, row, model->today());
        return;
    }
    const noite::ScratchSummary& s = row.card;
    const QPalette& palette = option.palette;
    const bool selected = option.state & QStyle::State_Selected;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(option.rect).adjusted(kMargin, kMargin, -kMargin, -kMargin);
    QPainterPath shape;
    shape.addRoundedRect(card, kRadius, kRadius);
    painter->fillPath(shape, cardFill(s.color, palette));
    if (row.inBundle) { // a member of the expanded stack before it
        QColor accent = palette.color(QPalette::Highlight);
        accent.setAlphaF(selected ? 1.0 : 0.6);
        painter->setPen(QPen(accent, 3));
    } else {
        painter->setPen(QPen(selected ? palette.color(QPalette::Highlight) : cardBorder(palette), selected ? 2 : 1));
    }
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
    const QString badge = badgeText(s, model->today(), QDateTime::currentMSecsSinceEpoch());
    const int badgeWidth = badge.isEmpty() ? 0 : smallMetrics.horizontalAdvance(badge) + 8;
    painter->drawText(footer, Qt::AlignRight | Qt::AlignVCenter, badge);
    painter->drawText(footer.adjusted(0, 0, -badgeWidth, 0), Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(s.tags.join(' '), Qt::ElideRight, footer.width() - badgeWidth));
    painter->restore();
}
