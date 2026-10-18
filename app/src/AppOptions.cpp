#include "AppOptions.h"

#include <QDir>

AppOptions parseArgs(const QStringList& args)
{
    AppOptions options;
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg == "--data" && i + 1 < args.size()) {
            options.dataDir = args[++i];
            options.dataFromCommandLine = true;
        } else if (arg.startsWith("--data=")) {
            options.dataDir = arg.mid(7);
            options.dataFromCommandLine = true;
        }
    }
    return options;
}

QString resolveDataDir(const AppOptions& options, const QString& exeDir)
{
    if (!options.dataDir.isEmpty())
        return QDir::cleanPath(QDir::fromNativeSeparators(options.dataDir));
    return QDir::cleanPath(QDir::fromNativeSeparators(exeDir) + "/data");
}
