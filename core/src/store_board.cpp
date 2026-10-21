#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <noite/content.h>

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

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

constexpr const char* kFtsClause = " AND id IN (SELECT id FROM scratch_fts WHERE scratch_fts MATCH ?2)";

QString tabWhere(Tab tab)
{
    switch (tab) {
    case Tab::Today:
        return "status = 'active' AND (due_date IS NULL OR due_date <= ?1)";
    case Tab::All:
        return "status = 'active'";
    case Tab::Completed:
        return "status = 'done'";
    case Tab::Deferred:
        return "status = 'deferred'";
    case Tab::Trash:
        return "status = 'trashed'";
    }
    return "0";
}

QString tabOrder(Tab tab)
{
    switch (tab) {
    case Tab::Today:
    case Tab::All:
        return QString("%1, due_date, created_at, id").arg(kBucket);
    case Tab::Completed:
        return "done_at DESC, id";
    case Tab::Deferred:
        return "created_at, id";
    case Tab::Trash:
        return "trashed_at DESC, id";
    }
    return "id";
}

enum class Kind { Colors, Dates, Tags };

constexpr Color kSwatches[] = {Color::Red, Color::Orange, Color::Yellow, Color::Green, Color::Blue, Color::Purple};

// The chips as SQL over `scratches` columns, leaving out `skip`'s kind. Values are appended
// to `binds` and referenced as ?3, ?4, ... (?1 = today, ?2 = the FTS query); user text is
// never spliced into the SQL.
QString filterWhere(const BoardFilter& filter, std::optional<Kind> skip, QStringList& binds)
{
    auto placeholders = [&binds](const QStringList& values) {
        QStringList marks;
        for (const QString& value : values) {
            binds << value;
            marks << QString("?%1").arg(2 + binds.size());
        }
        return marks.join(", ");
    };
    QStringList parts;
    if (skip != Kind::Colors && !filter.colors.isEmpty()) {
        QStringList names;
        for (Color c : kSwatches)
            if (filter.colors.contains(c))
                names << colorName(c);
        parts << "color IN (" + placeholders(names) + ")";
    }
    if (skip != Kind::Dates && !filter.dates.isEmpty()) {
        QStringList any;
        if (filter.dates.contains(DateChip::Overdue))
            any << "due_date < ?1";
        if (filter.dates.contains(DateChip::DueToday))
            any << "due_date = ?1";
        if (filter.dates.contains(DateChip::NoDate))
            any << "due_date IS NULL";
        parts << "(" + any.join(" OR ") + ")";
    }
    if (skip != Kind::Tags && !filter.tags.isEmpty()) {
        QStringList tags;
        for (const QString& tag : filter.tags)
            tags << tag.toLower();
        tags.removeDuplicates();
        parts << "id IN (SELECT scratch_id FROM scratch_tags WHERE tag IN (" + placeholders(tags) + "))";
    }
    return parts.isEmpty() ? QString() : " AND " + parts.join(" AND ");
}

// Binds ?1 (today), ?2 (the FTS query, if any) and the filter's values. SQLite sizes the
// parameter list by the highest ?N used, so ?1 is in range whenever any placeholder is.
void bindBoard(Stmt& s, const QString& sql, QDate today, const std::optional<QString>& match, const QStringList& binds)
{
    if (sql.contains('?'))
        s.bind(1, today.toString(Qt::ISODate));
    if (match)
        s.bind(2, *match);
    for (int i = 0; i < binds.size(); ++i)
        s.bind(3 + i, binds[i]);
}

} // namespace

namespace detail {

QHash<QString, QStringList> allTags(Db& db)
{
    QHash<QString, QStringList> tags;
    Stmt s = db.prepare("SELECT DISTINCT scratch_id, tag FROM scratch_tags ORDER BY scratch_id, tag");
    while (s.step())
        tags[s.text(0)] << s.text(1);
    return tags;
}

ScratchSummary summaryFromRow(const Stmt& s, const QHash<QString, QStringList>& tags)
{
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
    return sum;
}

} // namespace detail

QList<ScratchSummary> Store::list(Tab tab, QDate today, const QString& search, const BoardFilter& filter) const
{
    const std::optional<QString> match = ftsQuery(search);
    QStringList binds;
    const QString sql = QString("SELECT %1 FROM scratches WHERE ").arg(kSummaryColumns)
        + tabWhere(tab) + (match ? kFtsClause : "") + filterWhere(filter, std::nullopt, binds) + " ORDER BY "
        + tabOrder(tab);
    Stmt s = db().prepare(sql);
    bindBoard(s, sql, today, match, binds);

    const QHash<QString, QStringList> tags = allTags(db());
    QList<ScratchSummary> out;
    while (s.step())
        out.append(summaryFromRow(s, tags));
    return out;
}

TabCounts Store::tabCounts(QDate today, const QString& search, const BoardFilter& filter) const
{
    const std::optional<QString> match = ftsQuery(search);
    QStringList binds;
    const QString sql = "SELECT coalesce(sum(status = 'active' AND (due_date IS NULL OR due_date <= ?1)), 0), "
                        "coalesce(sum(status = 'active'), 0), coalesce(sum(status = 'done'), 0), "
                        "coalesce(sum(status = 'deferred'), 0), coalesce(sum(status = 'trashed'), 0) "
                        "FROM scratches WHERE 1"
        + QString(match ? kFtsClause : "") + filterWhere(filter, std::nullopt, binds);
    Stmt s = db().prepare(sql);
    bindBoard(s, sql, today, match, binds);
    s.step();
    TabCounts out;
    for (int i = 0; i < 5; ++i)
        out.counts[i] = int(s.int64(i));
    return out;
}

ChipFacets Store::chipFacets(Tab tab, QDate today, const QString& search, const BoardFilter& filter) const
{
    const std::optional<QString> match = ftsQuery(search);
    // The tab and search, plus every kind's chips except `skip`'s own.
    auto run = [&](Kind skip, const QString& select, const QString& from, const QString& tail) {
        QStringList binds;
        const QString sql = select + " FROM " + from + " WHERE " + tabWhere(tab) + (match ? kFtsClause : "")
            + filterWhere(filter, skip, binds) + tail;
        Stmt s = db().prepare(sql);
        bindBoard(s, sql, today, match, binds);
        return s;
    };
    ChipFacets out;

    QHash<QString, int> byColor;
    Stmt colors = run(Kind::Colors, "SELECT color, count(*)", "scratches", " AND color IS NOT NULL GROUP BY color");
    while (colors.step())
        byColor.insert(colors.text(0), int(colors.int64(1)));
    for (Color c : kSwatches) {
        const int count = byColor.value(colorName(c));
        if (count > 0 || filter.colors.contains(c))
            out.colors.append({c, count});
    }

    Stmt dates = run(Kind::Dates,
                     "SELECT coalesce(sum(due_date < ?1), 0), coalesce(sum(due_date = ?1), 0), "
                     "coalesce(sum(due_date IS NULL), 0)",
                     "scratches", {});
    dates.step();
    const DateChip order[] = {DateChip::Overdue, DateChip::DueToday, DateChip::NoDate};
    for (int i = 0; i < 3; ++i) {
        const int count = int(dates.int64(i));
        if (count > 0 || filter.dates.contains(order[i]))
            out.dates.append({order[i], count});
    }

    Stmt tags = run(Kind::Tags, "SELECT t.tag, count(DISTINCT id)", "scratches JOIN scratch_tags t ON t.scratch_id = id",
                    " GROUP BY t.tag ORDER BY t.tag");
    while (tags.step())
        out.tags.append({tags.text(0), int(tags.int64(1))});
    for (const QString& active : filter.tags) {
        const QString tag = active.toLower();
        const bool listed = std::any_of(out.tags.cbegin(), out.tags.cend(), [&](const auto& t) { return t.first == tag; });
        if (!listed)
            out.tags.append({tag, 0});
    }
    std::sort(out.tags.begin(), out.tags.end());
    return out;
}

QStringList Store::listTags() const
{
    Stmt s = db().prepare("SELECT DISTINCT tag FROM scratch_tags ORDER BY tag");
    QStringList out;
    while (s.step())
        out << s.text(0);
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
