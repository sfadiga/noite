#include "Theme.h"

#include <QStringList>

namespace theme {

QFont appFont()
{
    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI")});
    font.setPointSizeF(10.5);
    return font;
}

QFont headerFont(const QFont& base)
{
    QFont font = base;
    font.setPointSizeF(base.pointSizeF() * 1.1);
    font.setWeight(QFont::DemiBold);
    return font;
}

} // namespace theme
