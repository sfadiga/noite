#pragma once

#include <noite/model.h>

#include <QCache>
#include <QPixmap>
#include <QStyledItemDelegate>

struct AttachmentStore;

// Paints a BoardModel row as a fixed-size card (D17): colour tint, bold title, a few lines
// of preview, the first image as a thumbnail, a due/age badge and the tags. Tints come from
// a fixed light/dark table picked by the palette, so cards follow the Windows theme (D30).
class CardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    static constexpr QSize kCardSize{264, 200};

    // Without an attachment store no thumbnails are drawn.
    explicit CardDelegate(AttachmentStore* attachments, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Active notes only: the due label ("overdue", "today", "20 Oct") and the age, joined
    // with " · " ("overdue · 12d", "3h"); empty for done or trashed notes.
    static QString badgeText(const noite::ScratchSummary& summary, QDate today, qint64 now);
    // How long a note has been on the board: "now" under an hour, "3h" on the day it was
    // created, then calendar days ("12d").
    static QString ageText(qint64 createdAt, QDate today, qint64 now);
    // The card fill for a colour; invalid for no colour.
    static QColor tint(std::optional<noite::Color> color, bool dark);
    // What a card is painted with: its tint, or for no colour a shade just off the grid's
    // background (Base), so plain cards keep visible edges in dark mode too.
    static QColor cardFill(std::optional<noite::Color> color, const QPalette& palette);
    static QColor cardBorder(const QPalette& palette);
    // Room for the title line, and the title elided to fit it.
    static int titleWidth();
    static QString elidedTitle(const QString& title, const QFont& font, int width);

private:
    QPixmap thumbnail(const QString& attachmentId) const;

    AttachmentStore* attachments_;
    mutable QCache<QString, QPixmap> thumbnails_{200};
};
