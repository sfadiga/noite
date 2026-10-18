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

    void formatHotkeyIsCanonical()
    {
        QCOMPARE(formatHotkey(*parseHotkey("shift+alt+ctrl+j")), QString("Ctrl+Alt+Shift+J"));
        QCOMPARE(formatHotkey(*parseHotkey("meta+Ctrl+f12")), QString("Ctrl+Win+F12"));
        QCOMPARE(formatHotkey(*parseHotkey("alt+space")), QString("Alt+Space"));
        for (const char* text : {"Ctrl+Alt+J", "Win+Shift+N", "Ctrl+7", "Alt+F1", "Ctrl+Alt+F24", "Ctrl+Space"})
            QVERIFY(parseHotkey(formatHotkey(*parseHotkey(text))) == parseHotkey(text));
    }

    void comboFromKeyFormats()
    {
        QCOMPARE(comboFromKey(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier), std::optional<QString>("Ctrl+Alt+J"));
        QCOMPARE(comboFromKey(Qt::Key_F5, Qt::ControlModifier), std::optional<QString>("Ctrl+F5"));
        QCOMPARE(comboFromKey(Qt::Key_F24, Qt::AltModifier), std::optional<QString>("Alt+F24"));
        QCOMPARE(comboFromKey(Qt::Key_7, Qt::ControlModifier), std::optional<QString>("Ctrl+7"));
        QCOMPARE(comboFromKey(Qt::Key_Space, Qt::AltModifier | Qt::ShiftModifier), std::optional<QString>("Alt+Shift+Space"));
        QCOMPARE(comboFromKey(Qt::Key_J, Qt::ControlModifier | Qt::MetaModifier), std::optional<QString>("Ctrl+J"));
        QVERIFY(!comboFromKey(Qt::Key_J, Qt::ShiftModifier));
        QVERIFY(!comboFromKey(Qt::Key_J, Qt::NoModifier));
        QVERIFY(!comboFromKey(Qt::Key_Escape, Qt::ControlModifier));
        QVERIFY(!comboFromKey(Qt::Key_Control, Qt::ControlModifier));
        QVERIFY(!comboFromKey(Qt::Key_J, Qt::MetaModifier));
        for (const auto& [key, mods] : {std::pair{Qt::Key_A, Qt::ControlModifier}, std::pair{Qt::Key_F12, Qt::AltModifier},
                                         std::pair{Qt::Key_Space, Qt::ControlModifier}}) {
            const auto combo = comboFromKey(key, mods);
            QVERIFY(combo && parseHotkey(*combo));
        }
    }
};

NOITE_TEST(HotkeyTest)
#include "tst_hotkey.moc"
