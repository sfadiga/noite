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
    static constexpr QSize kCardSize{240, 180};

    // Without an attachment store no thumbnails are drawn.
    explicit CardDelegate(AttachmentStore* attachments, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Active notes only: "overdue", "today", the due date ("20 Oct"), or the age of an undated
    // note ("12d"); empty for notes created today and for done or trashed notes.
    static QString badgeText(const noite::ScratchSummary& summary, QDate today);
    // The card fill for a colour; invalid for no colour (the palette's base is used).
    static QColor tint(std::optional<noite::Color> color, bool dark);
    // Room for the title line, and the title elided to fit it.
    static int titleWidth();
    static QString elidedTitle(const QString& title, const QFont& font, int width);

private:
    QPixmap thumbnail(const QString& attachmentId) const;

    AttachmentStore* attachments_;
    mutable QCache<QString, QPixmap> thumbnails_{200};
};
