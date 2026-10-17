#include <noite/model.h>

#include <array>
#include <utility>

namespace noite {

namespace {

constexpr std::array<std::pair<Status, const char*>, 3> kStatuses = {{
    {Status::Active, "active"},
    {Status::Done, "done"},
    {Status::Trashed, "trashed"},
}};

constexpr std::array<std::pair<Color, const char*>, 6> kColors = {{
    {Color::Red, "red"},
    {Color::Orange, "orange"},
    {Color::Yellow, "yellow"},
    {Color::Green, "green"},
    {Color::Blue, "blue"},
    {Color::Purple, "purple"},
}};

template <typename E, std::size_t N>
QString nameOf(const std::array<std::pair<E, const char*>, N>& table, E value)
{
    for (const auto& [e, name] : table)
        if (e == value)
            return QString::fromLatin1(name);
    return {};
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const std::array<std::pair<E, const char*>, N>& table, const QString& text)
{
    for (const auto& [e, name] : table)
        if (text == QLatin1String(name))
            return e;
    return std::nullopt;
}

} // namespace

QString toString(Status status) { return nameOf(kStatuses, status); }
QString toString(Color color) { return nameOf(kColors, color); }
std::optional<Status> parseStatus(const QString& text) { return valueOf(kStatuses, text); }
std::optional<Color> parseColor(const QString& text) { return valueOf(kColors, text); }

} // namespace noite
