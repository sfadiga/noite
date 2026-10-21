#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

namespace noite {

using namespace detail;

QList<ReviewItem> Store::reviewQueue(QDate today, int staleDays) const
{
    const QString day = today.toString(Qt::ISODate);
    const qint64 now = nowMs();
    const QHash<QString, QStringList> tags = allTags(db());
    QList<ReviewItem> out;
    auto collect = [&](const QString& where, const QString& order, ReviewReason reason, auto bind) {
        Stmt s = db().prepare(QString("SELECT %1, modified_at FROM scratches WHERE status = 'active' AND %2 ORDER BY %3")
                                  .arg(kSummaryColumns, where, order));
        bind(s);
        while (s.step()) {
            ReviewItem item{summaryFromRow(s, tags), reason, 0};
            if (reason == ReviewReason::Overdue)
                item.days = int(item.note.due->daysTo(today));
            else if (reason == ReviewReason::Stale)
                item.days = int((now - s.int64(11)) / kDayMs);
            else if (reason == ReviewReason::Undated)
                item.days = int((now - item.note.createdAt) / kDayMs);
            out.append(item);
        }
    };
    collect("due_date < ?", "due_date, created_at, id", ReviewReason::Overdue, [&](Stmt& s) { s.bind(1, day); });
    collect("due_date = ?", "created_at, id", ReviewReason::DueToday, [&](Stmt& s) { s.bind(1, day); });
    collect("due_date IS NULL AND modified_at <= ?", "modified_at, id", ReviewReason::Stale,
            [&](Stmt& s) { s.bind(1, now - qint64(staleDays) * kDayMs); });
    if (out.isEmpty()) // nothing to decide: the review still opens, over the undated notes (M8d)
        collect("due_date IS NULL", "created_at, id", ReviewReason::Undated, [](Stmt&) {});
    return out;
}

void Store::touch(const ScratchId& id)
{
    requireStatus(db(), id);
    requireEditable(db(), id);
    detail::touch(db(), id, nowMs());
}

void Store::setModifiedAt(const ScratchId& id, qint64 ms)
{
    requireStatus(db(), id);
    detail::touch(db(), id, ms);
}

std::optional<QDate> Store::lastReviewDay() const
{
    Stmt s = db().prepare("SELECT value FROM settings WHERE key = 'last_review_day'");
    if (!s.step())
        return std::nullopt;
    const QDate day = QDate::fromString(s.text(0), Qt::ISODate);
    return day.isValid() ? std::optional<QDate>(day) : std::nullopt;
}

void Store::setLastReviewDay(QDate day)
{
    Stmt s = db().prepare("INSERT OR REPLACE INTO settings(key, value) VALUES ('last_review_day', ?)");
    s.bind(1, day.toString(Qt::ISODate));
    s.step();
}

} // namespace noite
