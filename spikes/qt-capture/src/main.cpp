// Throwaway spike: native Qt Widgets capture window behind a global hotkey.
#include "CaptureWindow.h"
#include "HotkeyFilter.h"
#include "Trials.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>

#include <windows.h>

int main(int argc, char* argv[])
{
    QElapsedTimer coldStart;
    coldStart.start();

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    Trials trials;
    CaptureWindow window(&trials);
    window.winId(); // create the native window now, while it is still hidden

    HotkeyFilter hotkey;
    app.installNativeEventFilter(&hotkey);
    if (!hotkey.registerHotkey(MOD_CONTROL | MOD_ALT, 'J')) {
        QMessageBox::critical(nullptr, "qt-capture", "Ctrl+Alt+J is already used by another app.");
        return 2;
    }

    QObject::connect(&hotkey, &HotkeyFilter::pressed, [&] {
        trials.pressed();
        window.summon();
    });
    QObject::connect(&window, &CaptureWindow::dismissed,
                     [&](const QString& text) { trials.dismissed(text); });

    Trials::logStartup(coldStart.elapsed());
    return app.exec();
}
