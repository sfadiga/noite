#pragma once

#include <QString>

// Command-line options. An empty dataDir means "<exe dir>/data" (D4).
struct AppOptions {
    QString dataDir;
};
