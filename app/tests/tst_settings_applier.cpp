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
            *f->store, manager, [](bool) {}, [&] { scheduler.check(); });

        Settings later = early;
        later.dayStart = QTime(10, 0);
        QVERIFY(!applier.apply(later));
        QCOMPARE(due.size(), 0); // 08:00 is before the new day start
        QCOMPARE(f->store->lastReviewDay(), std::optional<QDate>());
        QCOMPARE(scheduler.msUntilNextCheck(), 7'200'000);

        now = QDateTime(QDate(2026, 10, 15), QTime(10, 0));
        scheduler.check();
        QCOMPARE(due.size(), 1);
    }
};

NOITE_TEST(SettingsApplierTest)
#include "tst_settings_applier.moc"
