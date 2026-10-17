#include "App.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication qapp(argc, argv);
    qapp.setQuitOnLastWindowClosed(false);
    App app(AppOptions{});
    return app.run();
}
