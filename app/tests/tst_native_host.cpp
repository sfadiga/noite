#include "registry.h"

#include "platform/NativeHost.h"

#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <windows.h>

namespace {

QString uniqueKey()
{
    return "test-" + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace

class NativeHostTest : public QObject {
    Q_OBJECT

private slots:
    void hotkeyMessageEmitsSignal()
    {
        NativeHost host(uniqueKey());
        QSignalSpy spy(&host, &NativeHost::hotkeyPressed);
        QVERIFY(PostMessageW(reinterpret_cast<HWND>(host.nativeHandle()), WM_HOTKEY, 7, 0));
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toInt(), 7);
    }

    void activationReachesRunningInstance()
    {
        const QString key = uniqueKey();
        NativeHost host(key);
        QSignalSpy spy(&host, &NativeHost::activationRequested);
        QVERIFY(NativeHost::activateRunningInstance(key));
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(!NativeHost::activateRunningInstance(uniqueKey()));
    }

    void registerConflictIsReported()
    {
        const Hotkey rare{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F12};
        NativeHost a(uniqueKey());
        NativeHost b(uniqueKey());
        if (!a.registerHotkey(1, rare))
            QSKIP("Ctrl+Alt+Shift+F12 is already used on this machine");
        QVERIFY(!b.registerHotkey(1, rare));
        a.unregisterHotkey(1);
        QVERIFY(b.registerHotkey(1, rare));
        b.unregisterHotkey(1);
    }

    void destroyingHostReleasesHotkeys()
    {
        const Hotkey rare{MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F11};
        {
            NativeHost a(uniqueKey());
            if (!a.registerHotkey(3, rare))
                QSKIP("Ctrl+Alt+Shift+F11 is already used on this machine");
        }
        NativeHost b(uniqueKey());
        QVERIFY(b.registerHotkey(3, rare));
    }

    void instanceKeyIsStableAndPathInsensitive()
    {
        const QString k = instanceKeyFor("C:/X/data");
        QCOMPARE(k.size(), 16);
        QCOMPARE(instanceKeyFor(R"(c:\x\data\)"), k);
        QCOMPARE(instanceKeyFor(R"(C:\x\.\data)"), k);
        QVERIFY(instanceKeyFor(R"(C:\y\data)") != k);
    }
};

NOITE_TEST(NativeHostTest)
#include "tst_native_host.moc"
