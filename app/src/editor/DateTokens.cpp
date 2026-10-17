#include "editor/DateTokens.h"

#include <QHash>
#include <QRegularExpression>

namespace {

QString fold(const QString& text)
{
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing)
            out += c.toLower();
    return out;
}

std::optional<QDate> valid(QDate date)
{
    return date.isValid() ? std::optional<QDate>(date) : std::nullopt;
}

} // namespace

std::optional<QDate> parseDateToken(const QString& token, QDate today)
{
    const QString t = fold(token);
    if (t == "today" || t == "hoje")
        return today;
    if (t == "tomorrow" || t == "amanha")
        return today.addDays(1);

    static const QHash<QString, int> weekdays = {
        {"mon", 1}, {"tue", 2}, {"wed", 3}, {"thu", 4}, {"fri", 5}, {"sat", 6}, {"sun", 7},
        {"seg", 1}, {"ter", 2}, {"qua", 3}, {"qui", 4}, {"sex", 5}, {"sab", 6}, {"dom", 7},
    };
    if (const auto it = weekdays.constFind(t); it != weekdays.constEnd())
        return today.addDays((*it - today.dayOfWeek() + 7) % 7);

    static const QRegularExpression iso(R"(^(\d{4})-(\d{2})-(\d{2})$)");
    if (const auto m = iso.match(t); m.hasMatch())
        return valid(QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));

    static const QRegularExpression dayMonth(R"(^(\d{1,2})/(\d{1,2})(?:/(\d{4}))?$)");
    if (const auto m = dayMonth.match(t); m.hasMatch()) {
        const int day = m.captured(1).toInt();
        const int month = m.captured(2).toInt();
        if (m.hasCaptured(3))
            return valid(QDate(m.captured(3).toInt(), month, day));
        const QDate thisYear(today.year(), month, day);
        if (!thisYear.isValid())
            return std::nullopt;
        return thisYear >= today ? thisYear : valid(QDate(today.year() + 1, month, day));
    }
    return std::nullopt;
}
