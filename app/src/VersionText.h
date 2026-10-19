#pragma once

#include <QString>

// The board's version label: "1.0.0" -> "v1.0.0"; any dev build ("1.0.0-dev+abc1234.dirty") -> "v1.0.0-dev".
QString boardVersionText(const QString& fullVersion);
// "noite <NOITE_VERSION> · Qt <qVersion()>"
QString versionToolTip();
