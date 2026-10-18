#include "App.h"
#include "Theme.h"
#include "log/Log.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    applog::startBuffering(); // before anything logs; start() opens the file once the data folder is ours
    QApplication qapp(argc, argv);
    QApplication::setFont(theme::appFont());
    qapp.setQuitOnLastWindowClosed(false);
    AppOptions options = parseArgs(QCoreApplication::arguments());
    options.crashHandler = true;
    App app(std::move(options), nullptr, std::make_unique<RunKeyAutostart>());
    return app.run();
}
