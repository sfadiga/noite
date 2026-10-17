#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

#include <optional>

namespace noite {

using ScratchId = QString;
using BundleId = QString;

enum class Status { Active, Done, Trashed };
enum class Color { Red, Orange, Yellow, Green, Blue, Purple };
enum class Tab { Today, All, Completed, Trash };

struct Scratch {
    ScratchId id;
    QString html; // Qt rich-text HTML (D7)
    QString title;
    qint64 createdAt{};
    qint64 modifiedAt{};
    std::optional<QDate> due;
    std::optional<Color> color;
    QStringList tags; // inline + manual, sorted, unique
    QStringList manualTags;
    std::optional<BundleId> bundleId;
    Status status{};
    std::optional<qint64> doneAt;
    std::optional<qint64> trashedAt;
};

// Lowercase names, as stored in the database.
QString statusName(Status status);
QString colorName(Color color);
std::optional<Status> parseStatus(const QString& text);
std::optional<Color> parseColor(const QString& text);

} // namespace noite
