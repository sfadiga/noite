#include "capture/NoteFooter.h"

#include <QCoreApplication>
#include <QLocale>
#include <QStringList>

QString noteFooterText(std::optional<QDate> due, std::optional<noite::Color> color)
{
    QStringList parts;
    if (due)
        parts << QCoreApplication::translate("NoteFooter", "Due %1").arg(QLocale().toString(*due, QLocale::ShortFormat));
    if (color)
        parts << noite::colorName(*color);
    return parts.join(QStringLiteral("  ·  "));
}
