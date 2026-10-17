#pragma once

#include <noite/model.h>

#include <QAbstractListModel>
#include <QDate>

#include <functional>

namespace noite {
class Store;
}

// The board's cards: one row per Store::list(tab, today, search) result (D17, D19). The
// store owns the order; this model never re-sorts. A failing query keeps the previous rows
// and reports through errorOccurred.
class BoardModel : public QAbstractListModel {
    Q_OBJECT
public:
    BoardModel(noite::Store& store, std::function<QDate()> today, QObject* parent = nullptr);

    void setTab(noite::Tab tab);
    noite::Tab tab() const { return tab_; }
    void setSearch(const QString& search);
    QString search() const { return search_; }
    void reload();

    const noite::ScratchSummary& summary(int row) const { return rows_.at(row); }
    int rowOf(const noite::ScratchId& id) const;
    QDate today() const { return today_(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

signals:
    void errorOccurred(QString message);

private:
    noite::Store& store_;
    std::function<QDate()> today_;
    noite::Tab tab_ = noite::Tab::Today;
    QString search_;
    QList<noite::ScratchSummary> rows_;
};
