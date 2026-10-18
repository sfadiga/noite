#pragma once

#include <QDate>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

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

// What a board card needs (D17).
struct ScratchSummary {
    ScratchId id;
    QString title;
    QString preview; // first 200 graphemes of the plain text
    std::optional<QString> firstImage;
    std::optional<Color> color;
    QStringList tags;
    std::optional<QDate> due;
    std::optional<BundleId> bundleId;
    Status status{};
    qint64 createdAt{};
    std::optional<qint64> doneAt;
    std::optional<qint64> trashedAt;
};

// The start-of-day notification's numbers (D16). `open` = all active scratches.
// Filter chips (D18, M3b C1): OR within a kind, AND across kinds; empty = no filter.
enum class DateChip { Overdue, DueToday, NoDate };

struct BoardFilter {
    QSet<Color> colors;
    QSet<DateChip> dates;
    QStringList tags; // matched lowercase
    bool isEmpty() const { return colors.isEmpty() && dates.isEmpty() && tags.isEmpty(); }
    bool operator==(const BoardFilter&) const = default;
};

// The chips worth showing for a tab, with how many notes each would show (C6).
struct ChipFacets {
    QList<std::pair<Color, int>> colors;    // swatch order
    QList<std::pair<DateChip, int>> dates;  // Overdue, DueToday, NoDate
    QList<std::pair<QString, int>> tags;    // alphabetical
};

// How many notes each board tab holds; indexed by Tab.
struct TabCounts {
    int counts[4]{};
    int of(Tab tab) const { return counts[int(tab)]; }
};

struct ReviewCounts {
    int overdue{};
    int dueToday{};
    int open{};
};

struct PurgeReport {
    int scratchesDeleted{};
    int attachmentsDeleted{};
};

struct Bundle {
    BundleId id;
    QString name;
    qint64 createdAt{};
};

// Lowercase names, as stored in the database.
QString statusName(Status status);
QString colorName(Color color);
std::optional<Status> parseStatus(const QString& text);
std::optional<Color> parseColor(const QString& text);

} // namespace noite
