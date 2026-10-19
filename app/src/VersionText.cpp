#include "VersionText.h"

#include <noite_version.h>

#include <QtGlobal>

QString boardVersionText(const QString& fullVersion)
{
    const int plus = fullVersion.indexOf('+');
    return "v" + (plus < 0 ? fullVersion : fullVersion.left(plus));
}

QString versionToolTip()
{
    return QString::fromUtf8("noite %1 \xC2\xB7 Qt %2").arg(NOITE_VERSION, qVersion());
}
