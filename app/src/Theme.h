#pragma once

#include <QFont>

namespace theme {

// The app-wide font: Segoe UI 10.5 pt, about 17% bigger than Qt's Windows default.
QFont appFont();
// Tab labels and the search box: `base` a little bigger and semibold.
QFont headerFont(const QFont& base);

} // namespace theme
