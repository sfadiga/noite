#pragma once

#include <QString>
#include <QStringList>

// Command-line options. An empty dataDir means "<exe dir>/data" (D4).
struct AppOptions {
    QString dataDir;
    bool dataFromCommandLine = false; // --data was given (not the portable folder's own data)
    bool crashHandler = false;        // install the crash handler at start (main only; tests leave it off)
};

// The application name "noite" and the build's version (NOITE_VERSION); main calls it first.
void applyAppIdentity();

// Reads "--data <dir>" or "--data=<dir>" (used by tests and development).
AppOptions parseArgs(const QStringList& args);

// The data folder to use: --data if given, else <exeDir>/data next to the portable exe.
QString resolveDataDir(const AppOptions& options, const QString& exeDir);
