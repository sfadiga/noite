#include "AppIcon.h"

QIcon appIcon()
{
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/noite-%1.png").arg(size), QSize(size, size));
    return icon;
}
