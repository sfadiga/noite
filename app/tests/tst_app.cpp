#include "helpers.h"
#include "registry.h"

#include "App.h"
#include "AppOptions.h"
#include "board/BoardModel.h"
#include "board/BoardWindow.h"
#include "board/TabStrip.h"
#include "capture/CaptureWindow.h"
#include "editor/NoteEdit.h"
#include "editor/OpenFolder.h"
#include "jobs/JobScheduler.h"
#include "platform/Hotkey.h"
#include "platform/Autostart.h"
#include "platform/NativeHost.h"
#include "radar/ReviewScheduler.h"
#include "radar/ReviewWindow.h"

#include <noite/store.h>

#include <QAction>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

#include <sqlite3.h>

#include <windows.h>

using namespace noite;

namespace {

// Give a data folder rarely used hotkeys, so tests never take the user's real ones.
void prepare(const QString& root, const QString& hotkey, const QString& boardHotkey = "Ctrl+Alt+Shift+F8")
{
    auto store = Store::open(DataDir::acquire(root), std::make_shared<SystemClock>());
    Settings settings = store->settings();
    settings.captureHotkey = hotkey;
    settings.boardHotkey = boardHotkey;
    store->saveSettings(settings);
}

// Stands in for the Run key: tests never touch the real registry.
struct FakeAutostart final : AutostartRegistry {
    std::optional<QString>* value;
    explicit FakeAutostart(std::optional<QString>* v)
        : value(v)
    {
    }
    std::optional<QString> current() const override { return *value; }
    bool set(const QString& command) override
    {
        *value = command;
        return true;
    }
    bool remove() override
    {
        value->reset();
        return true;
    }
};

// A data folder holding one overdue note, with the day starting at midnight.
void prepareOverdue(const QString& root)
{
    prepare(root, "Ctrl+Alt+Shift+F9");
    auto store = Store::open(DataDir::acquire(root), std::make_shared<SystemClock>());
    const Scratch s = store->createScratch(test::html({"pay rent"}));
    store->setDue(s.id, QDate::currentDate().addDays(-2));
    Settings settings = store->settings();
    settings.dayStart = QTime(0, 0);
    store->saveSettings(settings);
}

} // namespace

class AppTest : public QObject {
    Q_OBJECT

private slots:
    void parseArgsReadsData()
    {
        QCOMPARE(parseArgs({"noite.exe", "--data", "D:/x"}).dataDir, QString("D:/x"));
        QCOMPARE(parseArgs({"noite.exe", "--data=D:/y"}).dataDir, QString("D:/y"));
        QVERIFY(parseArgs({"noite.exe"}).dataDir.isEmpty());
        QVERIFY(parseArgs({"noite.exe", "--data", "D:/x"}).dataFromCommandLine);
        QVERIFY(!parseArgs({"noite.exe"}).dataFromCommandLine);
    }

    // Final review I1 (the author's call): a run on another folder (--data, e.g. a test
    // folder) never touches Start with Windows; only the portable folder's own data does.
    void autostartLeftAloneUnderDataFlag()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        std::optional<QString> value = QString("\"C:\\real\\noite.exe\"");
        AppOptions options{root};
        options.dataFromCommandLine = true;
        App app(options, nullptr, std::make_unique<FakeAutostart>(&value));
        QVERIFY(!app.start());
        QCOMPARE(value, std::optional<QString>("\"C:\\real\\noite.exe\""));
        QVERIFY(!app.autostartAction()->isEnabled());
        app.autostartAction()->trigger();
        QCOMPARE(value, std::optional<QString>("\"C:\\real\\noite.exe\""));
    }

    void defaultDataDirIsNextToExe()
    {
        QCOMPARE(resolveDataDir(AppOptions{}, "C:/apps/noite"), QString("C:/apps/noite/data"));
        QCOMPARE(resolveDataDir(AppOptions{"D:/elsewhere/data"}, "C:/apps/noite"), QString("D:/elsewhere/data"));
    }

    // A5: files opened from chips last one session.
    void startupClearsOpenFolder()
    {
        const QString stale = defaultOpenFolder() + "/abcdef012345/x.txt";
        QVERIFY(QDir().mkpath(QFileInfo(stale).absolutePath()));
        QFile file(stale);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QVERIFY(!QFile::exists(stale));
    }

    // Review Focus 5.
    void nonAsciiDataDirWorks()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath(QString::fromUtf8("João/noite/data"));
        prepare(root, "Ctrl+Alt+Shift+F9");
        App app(AppOptions{root});
        QVERIFY(!app.start());
        const Scratch s = app.store()->createScratch(test::html({QString::fromUtf8("olá")}));
        QCOMPARE(app.store()->get(s.id).title, QString::fromUtf8("olá"));
    }

    // Review Focus 4.
    void secondAppOnSameDataDirActivatesFirst()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        App first(AppOptions{root});
        QVERIFY(!first.start());
        QSignalSpy activations(first.host(), &NativeHost::activationRequested);

        App second(AppOptions{root});
        QCOMPARE(second.start(), std::optional<int>(0));
        QTRY_COMPARE(activations.count(), 1);
        QTRY_VERIFY(first.captureWindow()->isVisible());
    }

    // Review Focus 3.
    void unavailableHotkeyKeepsRunning()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F10");
        NativeHost blocker("blocker-" + instanceKeyFor(root));
        blocker.registerHotkey(1, *parseHotkey("Ctrl+Alt+Shift+F10"));

        App app(AppOptions{root});
        QSignalSpy unavailable(&app, &App::hotkeyUnavailable);
        QVERIFY(!app.start());
        QCOMPARE(unavailable.count(), 1);
        QCOMPARE(unavailable[0][0].toString(), QString("Ctrl+Alt+Shift+F10"));
        app.captureWindow()->summon(); // the tray's "New note" path still works
        QVERIFY(app.captureWindow()->isVisible());
    }

    // ---- Board ----

    void boardHotkeyOpensBoard()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QVERIFY(!app.boardWindow());
        PostMessageW(reinterpret_cast<HWND>(app.host()->nativeHandle()), WM_HOTKEY, App::kBoardHotkeyId, 0);
        QTRY_VERIFY(app.boardWindow() && app.boardWindow()->isVisible());
    }

    void closingBoardDestroysItAndKeepsViewState()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        app.showBoard();
        app.boardWindow()->setTab(Tab::Completed);
        app.boardWindow()->searchField()->setText("x");
        app.boardWindow()->close();
        QTRY_VERIFY(!app.boardWindow());

        app.showBoard();
        QVERIFY(app.boardWindow()->isVisible());
        QCOMPARE(app.boardWindow()->tabStrip()->currentIndex(), int(Tab::Completed));
        QCOMPARE(app.boardWindow()->searchField()->text(), QString("x"));
        QCOMPARE(app.boardWindow()->model().search(), QString("x"));
    }

    void captureWhileBoardOpenShowsOnBoard()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        app.showBoard();
        QCOMPARE(app.boardWindow()->model().rowCount(), 0);
        app.captureWindow()->summon();
        QTest::keyClicks(app.captureWindow()->editor(), "fresh");
        QTest::keyClick(app.captureWindow()->editor(), Qt::Key_Escape);
        QTRY_COMPARE(app.boardWindow()->model().rowCount(), 1);
    }

    void boardHotkeyTakenKeepsRunning()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F7");
        NativeHost blocker("blocker-" + instanceKeyFor(root));
        QVERIFY(blocker.registerHotkey(1, *parseHotkey("Ctrl+Alt+Shift+F7")));

        App app(AppOptions{root});
        QSignalSpy unavailable(&app, &App::hotkeyUnavailable);
        QVERIFY(!app.start());
        QCOMPARE(unavailable.count(), 1);
        QCOMPARE(unavailable[0][0].toString(), QString("Ctrl+Alt+Shift+F7"));
        app.showBoard(); // the tray path still works
        QVERIFY(app.boardWindow()->isVisible());
    }

    // ---- M4: the radar ----

    void reviewDueOpensReviewInFront()
    {
        QTemporaryDir tmp;
        prepareOverdue(tmp.filePath("data"));
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QTRY_VERIFY(app.reviewWindow() && app.reviewWindow()->isVisible());
        QCOMPARE(app.reviewWindow()->total(), 1);
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(QDate::currentDate()));
    }

    void trayReviewOpensFreshQueue()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QCoreApplication::processEvents(); // the startup check (nothing due: marked silently)
        app.store()->setLastReviewDay(QDate(2000, 1, 1));
        app.showReview();
        ReviewWindow* first = app.reviewWindow();
        QVERIFY(first && first->isVisible());
        QCOMPARE(first->total(), 0);
        // Final review I6: an open window gets a fresh queue (e.g. left open since yesterday).
        const Scratch late = app.store()->createScratch(test::html({"late"}));
        app.store()->setDue(late.id, QDate::currentDate().addDays(-1));
        app.showReview();
        QCOMPARE(app.reviewWindow(), first);
        QCOMPARE(first->total(), 1);
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(QDate(2000, 1, 1)));
    }

    void autostartAppliedOnStart()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        std::optional<QString> value;
        App app(AppOptions{root}, nullptr, std::make_unique<FakeAutostart>(&value));
        QVERIFY(!app.start());
        const QString exeDir = QCoreApplication::applicationDirPath();
        QCOMPARE(value, std::optional<QString>(autostartCommand(QCoreApplication::applicationFilePath(), root,
                                                                exeDir + "/data")));
    }

    void autostartToggleSavesSetting()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        std::optional<QString> value;
        App app(AppOptions{tmp.filePath("data")}, nullptr, std::make_unique<FakeAutostart>(&value));
        QVERIFY(!app.start());
        QVERIFY(app.autostartAction()->isChecked());
        app.autostartAction()->trigger();
        QVERIFY(!value);
        QVERIFY(!app.store()->settings().startWithWindows);
        app.autostartAction()->trigger();
        QVERIFY(value.has_value());
        QVERIFY(app.store()->settings().startWithWindows);
    }

    void openBoardFromReview()
    {
        QTemporaryDir tmp;
        prepareOverdue(tmp.filePath("data"));
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QTRY_VERIFY(app.reviewWindow());
        app.reviewWindow()->requestOpenBoard();
        QVERIFY(app.boardWindow() && app.boardWindow()->isVisible());
    }

    // ---- M5: background jobs ----

    void logFileAfterStart()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        {
            App app(AppOptions{root});
            QVERIFY(!app.start());
            QFile log(root + "/logs/noite.log");
            QVERIFY(log.open(QIODevice::ReadOnly));
            QVERIFY(QString::fromUtf8(log.readAll()).contains(QString::fromUtf8("noite started · data ")));
        }
        QFile log(root + "/logs/noite.log");
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(log.readAll()).contains("noite quit"));
    }

    void backupActionInTray()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QCOMPARE(app.backupAction()->text(), QString("Back up now"));
        QVERIFY(app.backupAction()->isEnabled());
        emit app.jobs()->busyChanged(true);
        QCOMPARE(app.backupAction()->text(), QString::fromUtf8("Backing up…"));
        QVERIFY(!app.backupAction()->isEnabled());
        emit app.jobs()->busyChanged(false);
        QCOMPARE(app.backupAction()->text(), QString("Back up now"));
        QVERIFY(app.backupAction()->isEnabled());
    }

    void backUpNowAsksWhenNoFolder()
    {
        QTemporaryDir tmp;
        QTemporaryDir dest;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        QSignalSpy toasts(&app, &App::toastShown);

        app.setBackupFolderAsker([] { return std::nullopt; });
        app.backUpNow();
        QVERIFY(!app.store()->settings().backupDir);
        QTest::qWait(100);
        QCOMPARE(toasts.count(), 0);

        const QString folder = dest.path();
        app.setBackupFolderAsker([folder] { return std::optional<QString>(folder); });
        app.backUpNow();
        QCOMPARE(app.store()->settings().backupDir, std::optional<QString>(folder));
        QTRY_COMPARE_WITH_TIMEOUT(toasts.count(), 1, 10000);
        QVERIFY(toasts[0][1].toString().startsWith(QString::fromUtf8("Backed up · ")));
    }

    void backUpNowWithMissingFolderToastsError()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        const QString gone = tmp.filePath("gone");
        Settings settings = app.store()->settings();
        settings.backupDir = gone;
        app.store()->saveSettings(settings);
        int asked = 0;
        app.setBackupFolderAsker([&asked] {
            ++asked;
            return std::nullopt;
        });
        QSignalSpy toasts(&app, &App::toastShown);
        app.backUpNow();
        QTRY_COMPARE_WITH_TIMEOUT(toasts.count(), 1, 10000);
        QCOMPARE(asked, 0);
        QVERIFY(toasts[0][1].toString().startsWith("Backup failed: backup folder not found: "));
        QCOMPARE(app.store()->settings().backupDir, std::optional<QString>(gone));
    }

    void purgeReloadsOpenBoard()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        App app(AppOptions{root});
        QVERIFY(!app.start());
        const Scratch s = app.store()->createScratch(test::html({"old"}));
        app.store()->trash(s.id);
        // The app's store uses the system clock: backdate the trashing 31 days with SQL.
        sqlite3* db = nullptr;
        QVERIFY(sqlite3_open((root + "/noite.db").toUtf8().constData(), &db) == SQLITE_OK);
        sqlite3_busy_timeout(db, 5000);
        QVERIFY(sqlite3_exec(db, "UPDATE scratches SET trashed_at = trashed_at - 31 * 86400000", nullptr, nullptr, nullptr)
                == SQLITE_OK);
        sqlite3_close(db);

        app.showBoard();
        app.boardWindow()->setTab(Tab::Trash);
        QCOMPARE(app.boardWindow()->model().rowCount(), 1);
        app.jobs()->check();
        QCOMPARE(app.boardWindow()->model().rowCount(), 0);
    }

    void quitDuringBackupIsClean()
    {
        QTemporaryDir tmp;
        QTemporaryDir dest;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        auto app = std::make_unique<App>(AppOptions{tmp.filePath("data")});
        QVERIFY(!app->start());
        Settings settings = app->store()->settings();
        settings.backupDir = dest.path();
        app->store()->saveSettings(settings);
        app->backUpNow();
        app.reset();
        QVERIFY(QDir(dest.path()).entryList({"*.tmp"}).isEmpty());
    }
};

NOITE_TEST(AppTest)
#include "tst_app.moc"
