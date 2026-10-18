#include "App.h"
#include "Theme.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication qapp(argc, argv);
    QApplication::setFont(theme::appFont());
    qapp.setQuitOnLastWindowClosed(false);
    App app(parseArgs(QCoreApplication::arguments()), nullptr, std::make_unique<RunKeyAutostart>());
    return app.run();
}
