#pragma once

#include <noite/model.h>

#include <QAbstractListModel>
#include <QDate>
#include <QSet>

#include <functional>

namespace noite {
class Store;
}

// One grid entry: a note's card, or a bundle's stack (M3b C2).
struct BoardRow {
    enum class Kind { Card, Stack };
    Kind kind = Kind::Card;
    noite::ScratchSummary card;               // Card
    bool inBundle = false;                    // Card shown as a member of an expanded stack
    noite::BundleId bundleId;                 // Stack
    QString bundleName;                       // Stack
    QList<noite::ScratchSummary> members;     // Stack: matching members, store order
    bool expanded = false;                    // Stack
};

// The board's rows from Store::list(tab, today, search, filter) (D17, D18, D19). The store
// owns the order; a bundle's stack takes its first member's place and holds only matching
// members, which follow it while it is expanded. A failing query keeps the previous rows and
// reports through errorOccurred.
class BoardModel : public QAbstractListModel {
    Q_OBJECT
public:
    BoardModel(noite::Store& store, std::function<QDate()> today, QObject* parent = nullptr);

    void setTab(noite::Tab tab);
    noite::Tab tab() const { return tab_; }
    void setSearch(const QString& search);
    QString search() const { return search_; }
    void setFilter(const noite::BoardFilter& filter);
    noite::BoardFilter filter() const { return filter_; }
    void setExpanded(const noite::BundleId& bundle, bool expanded);
    bool isExpanded(const noite::BundleId& bundle) const { return expanded_.contains(bundle); }
    QSet<noite::BundleId> expanded() const { return expanded_; }
    void setExpandedSet(QSet<noite::BundleId> expanded);
    void reload();

    const BoardRow& row(int row) const { return rows_.at(row); }
    bool isStack(int row) const { return rows_.at(row).kind == BoardRow::Kind::Stack; }
    // A card's note; for a stack, its first member.
    const noite::ScratchSummary& summary(int row) const;
    int rowOf(const noite::ScratchId& id) const; // card rows only
    int rowOfStack(const noite::BundleId& bundle) const;
    QDate today() const { return today_(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    // Drag and drop (M3b): cards drag; cards, stacks and empty space take drops. A drop is
    // only reported (dropRequested); the board makes the store calls, so rows never move here.
    static constexpr const char* kMimeType = "application/x-noite-scratch";
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

signals:
    void errorOccurred(QString message);
    // `targetRow` is the card or stack dropped onto; -1 for empty space.
    void dropRequested(noite::ScratchId dragged, int targetRow);

private:
    noite::Store& store_;
    std::function<QDate()> today_;
    noite::Tab tab_ = noite::Tab::Today;
    QString search_;
    noite::BoardFilter filter_;
    QSet<noite::BundleId> expanded_;
    QList<BoardRow> rows_;
};
