#include "registry.h"

#include "platform/HotkeyManager.h"

#include <QMap>
#include <QSet>
#include <QTest>

namespace {

// Records registrations; `taken` holds combinations "another program" owns.
struct FakeBackend : HotkeyBackend {
    QSet<QString> taken; // normalised: "Ctrl+Alt+K"
    QMap<int, Hotkey> registered;

    bool registerHotkey(int id, Hotkey hotkey) override
    {
        for (const QString& combo : std::as_const(taken)) {
            if (parseHotkey(combo) == hotkey)
                return false;
        }
        if (registered.contains(id))
            return false; // like Win32: an id registers once per window
        registered.insert(id, hotkey);
        return true;
    }
    void unregisterHotkey(int id) override { registered.remove(id); }

    bool has(int id, const QString& combo) const { return registered.value(id) == parseHotkey(combo); }
};

} // namespace

class HotkeyManagerTest : public QObject {
    Q_OBJECT

private slots:
    void setRegistersAndKeepsOldOnFailure()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(1, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        backend.taken.insert("Ctrl+Alt+N");
        QCOMPARE(manager.set(1, "Ctrl+Alt+N"), HotkeyStatus::Taken);
        QCOMPARE(manager.combo(1), QString("Ctrl+Alt+J"));
        QVERIFY(manager.isRegistered(1));
        QVERIFY(backend.has(1, "Ctrl+Alt+J"));
    }

    void setRejectsInvalidAndSame()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(1, "J"), HotkeyStatus::Invalid);
        QCOMPARE(manager.set(2, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(1, "ctrl+alt+b"), HotkeyStatus::SameAsOther);
        QVERIFY(!manager.isRegistered(1));
    }

    void applySwapsBoth()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(1, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(2, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        const HotkeyOutcome outcome = manager.apply("Ctrl+Alt+B", "Ctrl+Alt+J");
        QCOMPARE(outcome.status, HotkeyStatus::Ok);
        QCOMPARE(manager.combo(1), QString("Ctrl+Alt+B"));
        QCOMPARE(manager.combo(2), QString("Ctrl+Alt+J"));
        QVERIFY(backend.has(1, "Ctrl+Alt+B"));
        QVERIFY(backend.has(2, "Ctrl+Alt+J"));
    }

    void applyIsAllOrNothing()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(1, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(2, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        backend.taken.insert("Ctrl+Alt+K");
        const HotkeyOutcome outcome = manager.apply("Ctrl+Alt+J", "Ctrl+Alt+K");
        QCOMPARE(outcome.status, HotkeyStatus::Taken);
        QCOMPARE(outcome.failedId, 2);
        QCOMPARE(manager.combo(1), QString("Ctrl+Alt+J"));
        QCOMPARE(manager.combo(2), QString("Ctrl+Alt+B"));
        QVERIFY(backend.has(1, "Ctrl+Alt+J"));
        QVERIFY(backend.has(2, "Ctrl+Alt+B"));
    }

    void probeLeavesNothing()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.probe("Ctrl+Alt+K"), HotkeyStatus::Ok);
        QVERIFY(!backend.registered.contains(99));
        backend.taken.insert("Ctrl+Alt+K");
        QCOMPARE(manager.probe("Ctrl+Alt+K"), HotkeyStatus::Taken);
        QCOMPARE(manager.probe("J"), HotkeyStatus::Invalid);
        QVERIFY(backend.registered.isEmpty());
    }

    void pauseResume()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(1, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(2, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        manager.pause();
        QVERIFY(manager.isPaused());
        QVERIFY(backend.registered.isEmpty());
        manager.pause(); // idempotent
        QVERIFY(backend.registered.isEmpty());
        QCOMPARE(manager.combo(1), QString("Ctrl+Alt+J"));
        manager.resume();
        QVERIFY(!manager.isPaused());
        QVERIFY(backend.has(1, "Ctrl+Alt+J"));
        QVERIFY(backend.has(2, "Ctrl+Alt+B"));
    }

    void messages()
    {
        QCOMPARE(hotkeyMessage(HotkeyStatus::Ok, "Ctrl+Alt+J", 2), QString::fromUtf8("✓ Ctrl+Alt+J"));
        QCOMPARE(hotkeyMessage(HotkeyStatus::Taken, "Ctrl+Alt+N", 2), QString("Ctrl+Alt+N is used by another program. Try another."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::Invalid, "J", 2), QString("Use Ctrl or Alt with a key."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::SameAsOther, "Ctrl+Alt+J", 2), QString("That's already the board hotkey."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::SameAsOther, "Ctrl+Alt+J", 1), QString("That's already the new-note hotkey."));
    }
};

NOITE_TEST(HotkeyManagerTest)
#include "tst_hotkey_manager.moc"
