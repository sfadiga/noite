#include "registry.h"

#include "platform/Hotkey.h"

#include <QTest>

#include <windows.h>

class HotkeyTest : public QObject {
    Q_OBJECT

private slots:
    void parsesCommonCombos()
    {
        QVERIFY(parseHotkey("Ctrl+Alt+J") == (Hotkey{MOD_CONTROL | MOD_ALT, 'J'}));
        QVERIFY(parseHotkey("ctrl + shift + space") == (Hotkey{MOD_CONTROL | MOD_SHIFT, VK_SPACE}));
        QVERIFY(parseHotkey("Win+Shift+N") == (Hotkey{MOD_WIN | MOD_SHIFT, 'N'}));
        QVERIFY(parseHotkey("Ctrl+Alt+F12") == (Hotkey{MOD_CONTROL | MOD_ALT, VK_F12}));
        QVERIFY(parseHotkey("Control+Meta+7") == (Hotkey{MOD_CONTROL | MOD_WIN, '7'}));
    }

    void rejectsInvalid()
    {
        for (const char* bad : {"", "J", "Shift+J", "Ctrl+Alt", "Ctrl+Ctrl+J", "Ctrl+Alt+J+K", "Ctrl+Alt+F25", "Ctrl++J"})
            QVERIFY2(!parseHotkey(bad), bad);
        QVERIFY(!parseHotkey(QString::fromUtf8("Ctrl+Alt+Ä")));
    }
};

NOITE_TEST(HotkeyTest)
#include "tst_hotkey.moc"
