#include "helpers.h"
#include "registry.h"

#include "fake_hotkeys.h"
#include "radar/ReviewScheduler.h"
#include "setup/SettingsApplier.h"

#include <noite/store.h>

#include <QSignalSpy>
#include <QTest>

using namespace noite;

namespace {

constexpr int kCap = HotkeyManager::kCaptureId;
constexpr int kBrd = HotkeyManager::kBoardId;

// A store holding the defaults, a manager registered to match, and counters for the callbacks.
struct Rig {
    std::unique_ptr<test::Fixture> f = test::fixture();
    FakeBackend backend;
    HotkeyManager manager{backend};
    int autostartCalls = 0;
    bool autostartValue = false;
    int recheckCalls = 0;
    SettingsApplier applier;
    Settings current;

    Rig()
        : applier(
              *f->store, manager,
              [this](bool on) {
                  ++autostartCalls;
                  autostartValue = on;
              },
              [this] { ++recheckCalls; })
    {
        current = f->store->settings();
        manager.set(kCap, current.captureHotkey);
        manager.set(kBrd, current.boardHotkey);
    }
};

} // namespace

class SettingsApplierTest : public QObject {
    Q_OBJECT

private slots:
    void savesAndReregisters()
    {
        Rig r;
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        next.trashRetentionDays = 40;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.f->store->settings(), next);
        QCOMPARE(r.manager.combo(kBrd), QString("Ctrl+Alt+L"));
        QVERIFY(r.backend.has(kBrd, "Ctrl+Alt+L"));
    }

    void takenSavesNothing()
    {
        Rig r;
        r.backend.taken.insert("Ctrl+Alt+L");
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        next.trashRetentionDays = 40;
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->message, QString("Ctrl+Alt+L is used by another program. Try another."));
        QCOMPARE(error->hotkeyId, kBrd);
        QCOMPARE(r.f->store->settings(), r.current);
        QCOMPARE(r.manager.combo(kBrd), r.current.boardHotkey);
        QCOMPARE(r.autostartCalls, 0);
    }

    void swapHotkeysSaves()
    {
        Rig r;
        Settings next = r.current;
        next.captureHotkey = r.current.boardHotkey;
        next.boardHotkey = r.current.captureHotkey;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.manager.combo(kCap), r.current.boardHotkey);
        QCOMPARE(r.manager.combo(kBrd), r.current.captureHotkey);
        QCOMPARE(r.f->store->settings(), next);
    }

    void failedSaveRollsBackHotkeys()
    {
        Rig r;
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        next.staleDays = 0; // invalid: saveSettings throws
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, 0);
        QVERIFY(!error->message.isEmpty());
        QCOMPARE(r.manager.combo(kBrd), r.current.boardHotkey);
        QCOMPARE(r.manager.combo(kCap), r.current.captureHotkey);
        QVERIFY(r.backend.has(kBrd, r.current.boardHotkey));
        QCOMPARE(r.f->store->settings(), r.current);
        QCOMPARE(r.autostartCalls, 0);
    }

    void autostartOnlyWhenChanged()
    {
        Rig r;
        Settings next = r.current;
        next.trashRetentionDays = 45;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.autostartCalls, 0);
        next.startWithWindows = !r.current.startWithWindows;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.autostartCalls, 1);
        QCOMPARE(r.autostartValue, next.startWithWindows);
    }

    void dayStartChangeRechecksReview()
    {
        Rig r;
        Settings next = r.current;
        next.dayStart = QTime(6, 0);
        QVERIFY(!r.applier.apply(next));
        r.recheckCalls = 0;

        next.dayStart = QTime(10, 0);
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.recheckCalls, 1);

        next.trashRetentionDays = 50;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.recheckCalls, 1);

        next.staleDays = 9;
        QVERIFY(!r.applier.apply(next));
        QCOMPARE(r.recheckCalls, 2);
    }

    // A startup set() that failed leaves the slot empty: a failed save puts it back to empty.
    void failedSaveClearsASlotThatWasEmpty()
    {
        Rig r;
        r.manager.clear(kBrd);
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        next.staleDays = 0;
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, 0);
        QVERIFY(r.manager.combo(kBrd).isEmpty());
        QVERIFY(!r.backend.registered.contains(kBrd));
        QVERIFY(r.backend.has(kCap, r.current.captureHotkey));
        QCOMPARE(r.f->store->settings(), r.current);
    }

    void failedRestoreIsReportedAndLeavesTheSlotEmpty()
    {
        Rig r;
        r.backend.taken.insert(r.current.boardHotkey); // grabbed by another program meanwhile
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        next.staleDays = 0;
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, 0);
        QVERIFY(error->message.contains(QString("The previous board hotkey %1 could not be restored.")
                                            .arg(r.current.boardHotkey)));
        QVERIFY(!r.backend.registered.contains(kBrd));
        QVERIFY(r.manager.combo(kBrd).isEmpty());
        QVERIFY(r.backend.has(kCap, r.current.captureHotkey));
    }

    void failedSaveAfterSwapRestoresBoth()
    {
        Rig r;
        Settings next = r.current;
        next.captureHotkey = r.current.boardHotkey;
        next.boardHotkey = r.current.captureHotkey;
        next.staleDays = 0;
        QVERIFY(r.applier.apply(next).has_value());
        QCOMPARE(r.manager.combo(kCap), r.current.captureHotkey);
        QCOMPARE(r.manager.combo(kBrd), r.current.boardHotkey);
        QVERIFY(r.backend.has(kCap, r.current.captureHotkey));
        QVERIFY(r.backend.has(kBrd, r.current.boardHotkey));
    }

    void pausedApplyRegistersOnResume()
    {
        Rig r;
        r.manager.pause();
        Settings next = r.current;
        next.boardHotkey = "Ctrl+Alt+L";
        QVERIFY(!r.applier.apply(next));
        QVERIFY(r.backend.registered.isEmpty());
        r.manager.resume();
        QVERIFY(r.backend.has(kBrd, "Ctrl+Alt+L"));
        QVERIFY(r.backend.has(kCap, r.current.captureHotkey));
        QCOMPARE(r.f->store->settings(), next);
    }

    void invalidHotkeySavesNothing()
    {
        Rig r;
        Settings next = r.current;
        next.boardHotkey = "J";
        next.trashRetentionDays = 40;
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, kBrd);
        QCOMPARE(error->message, QString("Use Ctrl or Alt with a key."));
        QCOMPARE(r.f->store->settings(), r.current);
    }

    void sameAsOtherSavesNothing()
    {
        Rig r;
        Settings next = r.current;
        next.boardHotkey = next.captureHotkey;
        const auto error = r.applier.apply(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, kBrd);
        QCOMPARE(error->message, QString("That's already the new-note hotkey."));
        QCOMPARE(r.f->store->settings(), r.current);
        QCOMPARE(r.manager.combo(kBrd), r.current.boardHotkey);
    }

    // Moving the day start earlier past the clock makes the review due at once; only the
    // recheck callback can make that happen (verified by removing the callback: fails).
    void dayStartMovedEarlierOpensTheReview()
    {
        auto f = test::fixture();
        const ScratchId id = f->store->createScratch(test::html({"pay rent"})).id;
        f->store->setDue(id, QDate(2026, 10, 10));
        Settings late = f->store->settings();
        late.dayStart = QTime(10, 0);
        f->store->saveSettings(late);

        QDateTime now(QDate(2026, 10, 15), QTime(8, 0));
        ReviewScheduler scheduler(*f->store, [&] { return now; }, [] { return true; });
        QSignalSpy due(&scheduler, &ReviewScheduler::reviewDue);
        scheduler.check("test"); // arms the timer; 08:00 is before 10:00
        QCOMPARE(due.size(), 0);

        FakeBackend backend;
        HotkeyManager manager(backend);
        manager.set(kCap, late.captureHotkey);
        manager.set(kBrd, late.boardHotkey);
        SettingsApplier applier(
            *f->store, manager, [](bool) {}, [&] { scheduler.check("test"); });

        Settings early = late;
        early.dayStart = QTime(6, 0);
        QVERIFY(!applier.apply(early));
        QCOMPARE(due.size(), 1);
    }

    // The brief's scenario: at 08:00 a review would be due under a 06:00 day start; moving the
    // day start to 10:00 and rechecking holds it back and re-arms the timer for 10:00.
    void dayStartMovedLaterHoldsTheReview()
    {
        auto f = test::fixture();
        const ScratchId id = f->store->createScratch(test::html({"pay rent"})).id;
        f->store->setDue(id, QDate(2026, 10, 10));
        Settings early = f->store->settings();
        early.dayStart = QTime(6, 0);
        f->store->saveSettings(early);

        QDateTime now(QDate(2026, 10, 15), QTime(8, 0));
        ReviewScheduler scheduler(*f->store, [&] { return now; }, [] { return true; });
        QSignalSpy due(&scheduler, &ReviewScheduler::reviewDue);

        FakeBackend backend;
        HotkeyManager manager(backend);
        manager.set(kCap, early.captureHotkey);
        manager.set(kBrd, early.boardHotkey);
        SettingsApplier applier(
            *f->store, manager, [](bool) {}, [&] { scheduler.check("test"); });

        Settings later = early;
        later.dayStart = QTime(10, 0);
        QVERIFY(!applier.apply(later));
        QCOMPARE(due.size(), 0); // 08:00 is before the new day start
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>());
        QCOMPARE(scheduler.msUntilNextCheck(), 7'200'000);

        now = QDateTime(QDate(2026, 10, 15), QTime(10, 0));
        scheduler.check("test");
        QCOMPARE(due.size(), 1);
    }

    // The wizard's fallback when its hotkeys could not be applied: everything else is saved.
    void keepingHotkeysSavesTheRest()
    {
        Rig r;
        Settings next = r.current;
        next.captureHotkey = "Ctrl+Alt+K";
        next.boardHotkey = "Ctrl+Alt+L";
        next.trashRetentionDays = 40;
        next.staleDays = 9;
        next.startWithWindows = !r.current.startWithWindows;
        QVERIFY(!r.applier.applyKeepingHotkeys(next));

        Settings expected = next;
        expected.captureHotkey = r.current.captureHotkey;
        expected.boardHotkey = r.current.boardHotkey;
        QCOMPARE(r.f->store->settings(), expected);
        QCOMPARE(r.manager.combo(kCap), r.current.captureHotkey);
        QCOMPARE(r.manager.combo(kBrd), r.current.boardHotkey);
        QVERIFY(r.backend.has(kCap, r.current.captureHotkey));
        QVERIFY(r.backend.has(kBrd, r.current.boardHotkey));
        QCOMPARE(r.autostartCalls, 1);
        QCOMPARE(r.autostartValue, next.startWithWindows);
        QCOMPARE(r.recheckCalls, 1);
    }

    void keepingHotkeysUsesTheStoredComboForAnEmptySlot()
    {
        Rig r;
        Settings stored = r.current;
        stored.boardHotkey = "Ctrl+Alt+Shift+F8";
        r.f->store->saveSettings(stored);
        r.manager.clear(kBrd);

        Settings next = stored;
        next.boardHotkey = "Ctrl+Alt+L";
        next.trashRetentionDays = 40;
        QVERIFY(!r.applier.applyKeepingHotkeys(next));
        QCOMPARE(r.f->store->settings().boardHotkey, QString("Ctrl+Alt+Shift+F8"));
        QCOMPARE(r.f->store->settings().trashRetentionDays, 40);
        QVERIFY(!r.manager.isRegistered(kBrd));
        QVERIFY(!r.backend.registered.contains(kBrd));
    }

    void keepingHotkeysReportsAStoreFailure()
    {
        Rig r;
        Settings next = r.current;
        next.trashRetentionDays = 0;
        const auto error = r.applier.applyKeepingHotkeys(next);
        QVERIFY(error.has_value());
        QCOMPARE(error->hotkeyId, 0);
        QCOMPARE(error->message, QString("trash retention must be 1-365 days"));
        QCOMPARE(r.f->store->settings(), r.current);
        QCOMPARE(r.autostartCalls, 0);
    }
};

NOITE_TEST(SettingsApplierTest)
#include "tst_settings_applier.moc"
