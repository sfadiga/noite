#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/content.h>

#include <QHash>
#include <QRegularExpression>

namespace noite {

namespace detail {

std::optional<QString> ftsQuery(const QString& input)
{
    static const QRegularExpression space(R"(\s+)");
    QStringList terms;
    for (QString term : input.split(space, Qt::SkipEmptyParts))
        terms << "\"" + term.replace("\"", "\"\"") + "\"*";
    if (terms.isEmpty())
        return std::nullopt;
    return terms.join(' ');
}

} // namespace detail

using namespace detail;

namespace {

// 0 overdue, 1 due today, 2 undated, 3 upcoming; ?1 = today (YYYY-MM-DD).
constexpr const char* kBucket =
    "CASE WHEN due_date IS NULL THEN 2 WHEN due_date < ?1 THEN 0 WHEN due_date = ?1 THEN 1 ELSE 3 END";

QString tabQuery(Tab tab, bool searching)
{
    QString where;
    QString order;
    switch (tab) {
    case Tab::Today:
        where = "status = 'active' AND (due_date IS NULL OR due_date <= ?1)";
        order = QString("%1, due_date, created_at, id").arg(kBucket);
        break;
    case Tab::All:
        where = "status = 'active'";
        order = QString("%1, due_date, created_at, id").arg(kBucket);
        break;
    case Tab::Completed:
        where = "status = 'done'";
        order = "done_at DESC, id";
        break;
    case Tab::Trash:
        where = "status = 'trashed'";
        order = "trashed_at DESC, id";
        break;
    }
    if (searching)
        where += " AND id IN (SELECT id FROM scratch_fts WHERE scratch_fts MATCH ?2)";
    return "SELECT id, title, plain_text, first_image, color, due_date, bundle_id, status, created_at, done_at, "
           "trashed_at FROM scratches WHERE "
        + where + " ORDER BY " + order;
}

QHash<QString, QStringList> allTags(Db& db)
{
    QHash<QString, QStringList> tags;
    Stmt s = db.prepare("SELECT DISTINCT scratch_id, tag FROM scratch_tags ORDER BY scratch_id, tag");
    while (s.step())
        tags[s.text(0)] << s.text(1);
    return tags;
}

} // namespace

QList<ScratchSummary> Store::list(Tab tab, QDate today, const QString& search) const
{
    const std::optional<QString> match = ftsQuery(search);
    Stmt s = db().prepare(tabQuery(tab, match.has_value()));
    // ?1 (today) appears only in the Today/All queries; with no ?N at all, binding 1 is out of range.
    if (tab == Tab::Today || tab == Tab::All || match)
        s.bind(1, today.toString(Qt::ISODate));
    if (match)
        s.bind(2, *match);

    const QHash<QString, QStringList> tags = allTags(db());
    QList<ScratchSummary> out;
    while (s.step()) {
        ScratchSummary sum;
        sum.id = s.text(0);
        sum.title = s.text(1);
        sum.preview = content::preview(s.text(2));
        sum.firstImage = s.optText(3);
        if (const auto color = s.optText(4))
            sum.color = parseColor(*color);
        if (const auto due = s.optText(5))
            sum.due = QDate::fromString(*due, Qt::ISODate);
        sum.bundleId = s.optText(6);
        sum.status = parseStatus(s.text(7)).value();
        sum.createdAt = s.int64(8);
        sum.doneAt = s.optInt64(9);
        sum.trashedAt = s.optInt64(10);
        sum.tags = tags.value(sum.id);
        out.append(sum);
    }
    return out;
}

ReviewCounts Store::reviewCounts(QDate today) const
{
    Stmt s = db().prepare("SELECT coalesce(sum(due_date < ?1), 0), coalesce(sum(due_date = ?1), 0), count(*) "
                          "FROM scratches WHERE status = 'active'");
    s.bind(1, today.toString(Qt::ISODate));
    s.step();
    return {int(s.int64(0)), int(s.int64(1)), int(s.int64(2))};
}

} // namespace noite
