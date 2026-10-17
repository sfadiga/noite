#include "board/BoardModel.h"

#include <noite/store.h>

#include <QDebug>

BoardModel::BoardModel(noite::Store& store, std::function<QDate()> today, QObject* parent)
    : QAbstractListModel(parent)
    , store_(store)
    , today_(std::move(today))
{
}

void BoardModel::setTab(noite::Tab tab)
{
    tab_ = tab;
    reload();
}

void BoardModel::setSearch(const QString& search)
{
    search_ = search;
    reload();
}

void BoardModel::reload()
{
    QList<noite::ScratchSummary> rows;
    try {
        rows = store_.list(tab_, today_(), search_);
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        emit errorOccurred(QString::fromUtf8(error.what()));
        return;
    }
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

int BoardModel::rowOf(const noite::ScratchId& id) const
{
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].id == id)
            return row;
    }
    return -1;
}

int BoardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(rows_.size());
}

QVariant BoardModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    if (role == Qt::DisplayRole)
        return rows_[index.row()].title;
    return {};
}
