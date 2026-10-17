#pragma once

#include <QString>

#include <optional>

namespace noite {

using ScratchId = QString;
using BundleId = QString;

enum class Status { Active, Done, Trashed };
enum class Color { Red, Orange, Yellow, Green, Blue, Purple };
enum class Tab { Today, All, Completed, Trash };

// Lowercase names, as stored in the database.
QString toString(Status status);
QString toString(Color color);
std::optional<Status> parseStatus(const QString& text);
std::optional<Color> parseColor(const QString& text);

} // namespace noite
