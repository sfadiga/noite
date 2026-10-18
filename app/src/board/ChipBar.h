#pragma once

#include <noite/model.h>

#include <QWidget>

class FlowLayout;
class QAbstractButton;

// The board's filter chips (D18): colour swatches, Overdue / Due today / No date, then
// #tags, each with its count, wrapping onto more lines. Clicking a chip asks for the filter
// with it toggled (filterChanged); the board answers with setFacets. Hidden with no chips.
class ChipBar : public QWidget {
    Q_OBJECT
public:
    explicit ChipBar(QWidget* parent = nullptr);

    void setFacets(const noite::ChipFacets& facets, const noite::BoardFilter& active);
    // Keys: "color:<name>", "date:overdue|today|none", "tag:<tag>". Null if not shown.
    QAbstractButton* chip(const QString& key) const;
    QStringList keys() const;

signals:
    void filterChanged(noite::BoardFilter filter);

private:
    void addChip(const QString& key, const QString& text, bool active, const QIcon& icon = {});
    void toggle(const QString& key);

    FlowLayout* layout_;
    noite::BoardFilter active_;
    QList<QAbstractButton*> chips_;
};

Q_DECLARE_METATYPE(noite::BoardFilter)
