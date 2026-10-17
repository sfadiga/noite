#pragma once

#include <QString>
#include <QStringList>

// Command-line options. An empty dataDir means "<exe dir>/data" (D4).
struct AppOptions {
    QString dataDir;
};

// Reads "--data <dir>" or "--data=<dir>" (used by tests and development).
AppOptions parseArgs(const QStringList& args);

// The data folder to use: --data if given, else <exeDir>/data next to the portable exe.
QString resolveDataDir(const AppOptions& options, const QString& exeDir);
