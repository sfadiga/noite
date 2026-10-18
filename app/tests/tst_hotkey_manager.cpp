#include "registry.h"

#include "platform/HotkeyManager.h"

#include <QMap>
#include <QSet>
#include <QTest>

namespace {

constexpr int kCap = HotkeyManager::kCaptureId;
constexpr int kBrd = HotkeyManager::kBoardId;
constexpr int kProbe = HotkeyManager::kProbeId;

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
        for (const Hotkey& held : std::as_const(registered)) {
            if (held == hotkey)
                return false; // the OS refuses a combination this window already holds
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
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        backend.taken.insert("Ctrl+Alt+N");
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+N"), HotkeyStatus::Taken);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QVERIFY(manager.isRegistered(kCap));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
    }

    void setRejectsInvalidAndSame()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "J"), HotkeyStatus::Invalid);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kCap, "ctrl+alt+b"), HotkeyStatus::SameAsOther);
        QVERIFY(!manager.isRegistered(kCap));
    }

    void setInvalidKeepsOldRegistration()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kCap, "J"), HotkeyStatus::Invalid);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
    }

    void settingSameComboAgainIsOk()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
    }

    void storesCanonicalCombo()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "ctrl+alt+j"), HotkeyStatus::Ok);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QCOMPARE(manager.set(kBrd, "shift + alt + ctrl + f5"), HotkeyStatus::Ok);
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+Shift+F5"));
        QCOMPARE(manager.apply("meta+ctrl+space", "alt+k").status, HotkeyStatus::Ok);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Win+Space"));
        QCOMPARE(manager.combo(kBrd), QString("Alt+K"));
    }

    void applySwapsBoth()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        const HotkeyOutcome outcome = manager.apply("Ctrl+Alt+B", "Ctrl+Alt+J");
        QCOMPARE(outcome.status, HotkeyStatus::Ok);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+B"));
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+J"));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+B"));
        QVERIFY(backend.has(kBrd, "Ctrl+Alt+J"));
    }

    void applyIsAllOrNothing()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        backend.taken.insert("Ctrl+Alt+K");
        const HotkeyOutcome outcome = manager.apply("Ctrl+Alt+J", "Ctrl+Alt+K");
        QCOMPARE(outcome.status, HotkeyStatus::Taken);
        QCOMPARE(outcome.failedId, kBrd);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+B"));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
        QVERIFY(backend.has(kBrd, "Ctrl+Alt+B"));
    }

    void applyFailingOnCaptureReportsCaptureId()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        backend.taken.insert("Ctrl+Alt+K");
        const HotkeyOutcome outcome = manager.apply("Ctrl+Alt+K", "Ctrl+Alt+N");
        QCOMPARE(outcome.status, HotkeyStatus::Taken);
        QCOMPARE(outcome.failedId, kCap);
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
        QVERIFY(backend.has(kBrd, "Ctrl+Alt+B"));
    }

    void applyRejectsInvalidAndSame()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        HotkeyOutcome outcome = manager.apply("J", "Ctrl+Alt+B");
        QCOMPARE(outcome.status, HotkeyStatus::Invalid);
        QCOMPARE(outcome.failedId, kCap);
        outcome = manager.apply("Ctrl+Alt+B", "Shift");
        QCOMPARE(outcome.status, HotkeyStatus::Invalid);
        QCOMPARE(outcome.failedId, kBrd);
        outcome = manager.apply("Ctrl+Alt+B", "ctrl+alt+b");
        QCOMPARE(outcome.status, HotkeyStatus::SameAsOther);
        QCOMPARE(outcome.failedId, kBrd);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
    }

    void probeLeavesNothing()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.probe("Ctrl+Alt+K"), HotkeyStatus::Ok);
        QVERIFY(!backend.registered.contains(kProbe));
        backend.taken.insert("Ctrl+Alt+K");
        QCOMPARE(manager.probe("Ctrl+Alt+K"), HotkeyStatus::Taken);
        QCOMPARE(manager.probe("J"), HotkeyStatus::Invalid);
        QVERIFY(backend.registered.isEmpty());
    }

    void heldComboProbesTaken()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.probe("Ctrl+Alt+J"), HotkeyStatus::Taken);
        QVERIFY(!backend.registered.contains(kProbe));
        manager.pause();
        QCOMPARE(manager.probe("Ctrl+Alt+J"), HotkeyStatus::Ok);
    }

    void pauseResume()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        manager.pause();
        QVERIFY(manager.isPaused());
        QVERIFY(backend.registered.isEmpty());
        manager.pause(); // idempotent
        QVERIFY(backend.registered.isEmpty());
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        manager.resume();
        QVERIFY(!manager.isPaused());
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
        QVERIFY(backend.has(kBrd, "Ctrl+Alt+B"));
    }

    void setWhilePausedRegistersOnResume()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        manager.pause();
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+M"), HotkeyStatus::Ok);
        QVERIFY(backend.registered.isEmpty());
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+M"));
        manager.resume();
        QVERIFY(backend.has(kCap, "Ctrl+Alt+M"));
        QVERIFY(manager.isRegistered(kCap));
    }

    void setTakenWhilePausedKeepsOld()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        manager.pause();
        backend.taken.insert("Ctrl+Alt+N");
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+N"), HotkeyStatus::Taken);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QVERIFY(backend.registered.isEmpty());
        backend.taken.clear();
        manager.resume();
        QVERIFY(backend.has(kCap, "Ctrl+Alt+J"));
    }

    void applyWhilePaused()
    {
        FakeBackend backend;
        HotkeyManager manager(backend);
        QCOMPARE(manager.set(kCap, "Ctrl+Alt+J"), HotkeyStatus::Ok);
        QCOMPARE(manager.set(kBrd, "Ctrl+Alt+B"), HotkeyStatus::Ok);
        manager.pause();

        backend.taken.insert("Ctrl+Alt+K");
        const HotkeyOutcome failed = manager.apply("Ctrl+Alt+M", "Ctrl+Alt+K");
        QCOMPARE(failed.status, HotkeyStatus::Taken);
        QCOMPARE(failed.failedId, kBrd);
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+J"));
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+B"));
        QVERIFY(backend.registered.isEmpty());

        backend.taken.clear();
        QCOMPARE(manager.apply("Ctrl+Alt+M", "Ctrl+Alt+K").status, HotkeyStatus::Ok);
        QVERIFY(backend.registered.isEmpty());
        QCOMPARE(manager.combo(kCap), QString("Ctrl+Alt+M"));
        QCOMPARE(manager.combo(kBrd), QString("Ctrl+Alt+K"));
        manager.resume();
        QVERIFY(backend.has(kCap, "Ctrl+Alt+M"));
        QVERIFY(backend.has(kBrd, "Ctrl+Alt+K"));
    }

    void messages()
    {
        QCOMPARE(hotkeyMessage(HotkeyStatus::Ok, "Ctrl+Alt+J", kBrd), QString::fromUtf8("✓ Ctrl+Alt+J"));
        QCOMPARE(hotkeyMessage(HotkeyStatus::Taken, "Ctrl+Alt+N", kBrd), QString("Ctrl+Alt+N is used by another program. Try another."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::Invalid, "J", kBrd), QString("Use Ctrl or Alt with a key."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::SameAsOther, "Ctrl+Alt+J", kBrd), QString("That's already the board hotkey."));
        QCOMPARE(hotkeyMessage(HotkeyStatus::SameAsOther, "Ctrl+Alt+J", kCap), QString("That's already the new-note hotkey."));
    }
};

NOITE_TEST(HotkeyManagerTest)
#include "tst_hotkey_manager.moc"
