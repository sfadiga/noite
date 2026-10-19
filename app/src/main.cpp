#include "App.h"
#include "SelfCheck.h"
#include "Theme.h"
#include "log/Log.h"

#include <QApplication>

#include <cstring>

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-check") == 0)
            return selfCheckMain(argc, argv); // no log, no crash handler, no data folder, no window
    }
    applog::startBuffering(); // before anything logs; start() opens the file once the data folder is ours
    QApplication qapp(argc, argv);
    QApplication::setFont(theme::appFont());
    applyAppIdentity();
    qapp.setQuitOnLastWindowClosed(false);
    AppOptions options = parseArgs(QCoreApplication::arguments());
    options.crashHandler = true;
    App app(std::move(options), nullptr, std::make_unique<RunKeyAutostart>());
    return app.run();
}
