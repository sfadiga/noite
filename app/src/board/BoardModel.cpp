#include "board/BoardModel.h"

#include <noite/store.h>

#include <QDebug>
#include <QHash>
#include <QMimeData>

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

void BoardModel::setFilter(const noite::BoardFilter& filter)
{
    filter_ = filter;
    reload();
}

void BoardModel::setExpanded(const noite::BundleId& bundle, bool expanded)
{
    if (expanded)
        expanded_.insert(bundle);
    else
        expanded_.remove(bundle);
    reload();
}

void BoardModel::setExpandedSet(QSet<noite::BundleId> expanded)
{
    expanded_ = std::move(expanded);
    reload();
}

void BoardModel::setView(noite::Tab tab, const QString& search, const noite::BoardFilter& filter,
                         QSet<noite::BundleId> expanded)
{
    tab_ = tab;
    search_ = search;
    filter_ = filter;
    expanded_ = std::move(expanded);
    reload();
}

void BoardModel::reload()
{
    QList<noite::ScratchSummary> notes;
    QHash<noite::BundleId, QString> names;
    try {
        notes = store_.list(tab_, today_(), search_, filter_);
        for (const noite::Bundle& bundle : store_.listBundles())
            names.insert(bundle.id, bundle.name);
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        emit errorOccurred(QString::fromUtf8(error.what()));
        return;
    }

    // Stacks first take their first member's place, then collect the rest.
    QList<BoardRow> rows;
    QHash<noite::BundleId, int> stackAt;
    for (const noite::ScratchSummary& note : notes) {
        if (!note.bundleId) {
            rows.append({BoardRow::Kind::Card, note});
            continue;
        }
        const auto it = stackAt.constFind(*note.bundleId);
        if (it != stackAt.cend()) {
            rows[*it].members.append(note);
            continue;
        }
        BoardRow stack;
        stack.kind = BoardRow::Kind::Stack;
        stack.bundleId = *note.bundleId;
        stack.bundleName = names.value(stack.bundleId);
        stack.members = {note};
        stack.expanded = expanded_.contains(stack.bundleId);
        stackAt.insert(stack.bundleId, int(rows.size()));
        rows.append(stack);
    }
    QList<BoardRow> out;
    out.reserve(rows.size());
    for (const BoardRow& row : rows) {
        out.append(row);
        if (row.kind == BoardRow::Kind::Stack && row.expanded) {
            for (const noite::ScratchSummary& member : row.members) {
                BoardRow card{BoardRow::Kind::Card, member};
                card.inBundle = true;
                out.append(card);
            }
        }
    }

    beginResetModel();
    rows_ = std::move(out);
    endResetModel();
}

const noite::ScratchSummary& BoardModel::summary(int row) const
{
    const BoardRow& r = rows_.at(row);
    return r.kind == BoardRow::Kind::Stack ? r.members.first() : r.card;
}

int BoardModel::rowOf(const noite::ScratchId& id) const
{
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].kind == BoardRow::Kind::Card && rows_[row].card.id == id)
            return row;
    }
    return -1;
}

int BoardModel::rowOfStack(const noite::BundleId& bundle) const
{
    for (int row = 0; row < rows_.size(); ++row) {
        if (rows_[row].kind == BoardRow::Kind::Stack && rows_[row].bundleId == bundle)
            return row;
    }
    return -1;
}

int BoardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(rows_.size());
}

Qt::ItemFlags BoardModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled; // empty space: out of a bundle
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
    if (!isStack(index.row()))
        f |= Qt::ItemIsDragEnabled;
    return f;
}

QStringList BoardModel::mimeTypes() const
{
    return {QString::fromLatin1(kMimeType)};
}

QMimeData* BoardModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty() || !indexes.first().isValid() || isStack(indexes.first().row()))
        return nullptr;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMimeType), summary(indexes.first().row()).id.toUtf8());
    return mime;
}

Qt::DropActions BoardModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool BoardModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex&) const
{
    return data && data->hasFormat(QString::fromLatin1(kMimeType));
}

bool BoardModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                              const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, column, parent))
        return false;
    const noite::ScratchId dragged = QString::fromUtf8(data->data(QString::fromLatin1(kMimeType)));
    emit dropRequested(dragged, parent.isValid() ? parent.row() : -1);
    return false; // the store changes and the board reloads; the view moves nothing
}

QVariant BoardModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rows_.size())
        return {};
    if (role == Qt::DisplayRole) {
        const BoardRow& r = rows_[index.row()];
        return r.kind == BoardRow::Kind::Stack ? r.bundleName : r.card.title;
    }
    return {};
}
