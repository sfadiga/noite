#include "fake_hotkeys.h"
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
#include "setup/BackupFolderField.h"
#include "setup/SettingsDialog.h"
#include "setup/SetupWizard.h"

#include <noite/store.h>

#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <sqlite3.h>

#include <windows.h>

using namespace noite;

namespace {

// Give a data folder rarely used hotkeys, so tests never take the user's real ones.
void prepare(const QString& root, const QString& hotkey, const QString& boardHotkey = "Ctrl+Alt+Shift+F8",
             bool setupCompleted = true)
{
    auto store = Store::open(DataDir::acquire(root), std::make_shared<SystemClock>());
    Settings settings = store->settings();
    settings.captureHotkey = hotkey;
    settings.boardHotkey = boardHotkey;
    settings.setupCompleted = setupCompleted;
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
void prepareOverdue(const QString& root, bool setupCompleted = true)
{
    prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", setupCompleted);
    auto store = Store::open(DataDir::acquire(root), std::make_shared<SystemClock>());
    const Scratch s = store->createScratch(test::html({"pay rent"}));
    store->setDue(s.id, QDate::currentDate().addDays(-2));
    Settings settings = store->settings();
    settings.dayStart = QTime(0, 0);
    store->saveSettings(settings);
}

// Runs `sql` on the data folder's database through a second connection.
void execSql(const QString& root, const char* sql)
{
    sqlite3* db = nullptr;
    QVERIFY(sqlite3_open((root + "/noite.db").toUtf8().constData(), &db) == SQLITE_OK);
    sqlite3_busy_timeout(db, 5000);
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    sqlite3_close(db);
    QCOMPARE(rc, SQLITE_OK);
}

// Settings → Run setup again.
void openSetupAgain(App& app)
{
    app.showSettings();
    QVERIFY(app.settingsDialog());
    emit app.settingsDialog()->runSetupRequested();
    QVERIFY(app.wizard() && app.wizard()->isVisible());
    QVERIFY(!app.setupPending());
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

    // Quitting while the snapshot copy is under way leaves no .tmp and no half-written .db.
    void quitDuringBackupIsClean()
    {
        QTemporaryDir tmp;
        QTemporaryDir dest;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        auto app = std::make_unique<App>(AppOptions{tmp.filePath("data")});
        QVERIFY(!app->start());
        for (int i = 0; i < 30; ++i) {
            QByteArray bytes(1'000'000, 0);
            QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(bytes.data()), bytes.size() / 4);
            app->store()->putAttachment(bytes, "bin");
        }
        Settings settings = app->store()->settings();
        settings.backupDir = dest.path();
        app->store()->saveSettings(settings);
        const QString snaps = dest.path() + "/noite-backups/" + app->store()->installId() + "/snapshots";
        app->backUpNow();
        QTRY_VERIFY(!QDir(snaps).entryList({"*.tmp"}, QDir::Files).isEmpty()); // the copy is under way
        app.reset();
        QVERIFY(QDir(snaps).entryList({"*.tmp"}, QDir::Files).isEmpty());
        const QStringList dbs = QDir(snaps).entryList({"*.db"}, QDir::Files);
        QVERIFY(dbs.size() <= 1);
        for (const QString& db : dbs) {
            QVERIFY_THROWS_NO_EXCEPTION(backup::verify(snaps + "/" + db));
        }
    }

    void backupActionDisabledWhileAskingForAFolder()
    {
        QTemporaryDir tmp;
        prepare(tmp.filePath("data"), "Ctrl+Alt+Shift+F9");
        App app(AppOptions{tmp.filePath("data")});
        QVERIFY(!app.start());
        int asked = 0;
        bool enabledWhileAsking = true;
        app.setBackupFolderAsker([&]() -> std::optional<QString> {
            ++asked;
            enabledWhileAsking = app.backupAction()->isEnabled();
            return std::nullopt;
        });
        QVERIFY(app.backupAction()->isEnabled());
        app.backUpNow();
        QCOMPARE(asked, 1);
        QVERIFY(!enabledWhileAsking);
        QVERIFY(app.backupAction()->isEnabled());
    }

    // ---- M6: setup wizard and Settings ----

    void wizardOnFirstRun()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root, false);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QCoreApplication::processEvents();
        QVERIFY(app.wizard() && app.wizard()->isVisible());
        QVERIFY(app.setupPending());
        QVERIFY(!app.reviewWindow());
    }

    void wizardFinishViaHotkey()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root, false);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QVERIFY(app.wizard());
        while (app.wizard()->currentId() != SetupWizard::kTryItPage)
            app.wizard()->next();
        QCOMPARE(app.wizard()->currentId(), int(SetupWizard::kTryItPage));

        emit app.host()->hotkeyPressed(App::kCaptureHotkeyId);
        QVERIFY(!app.wizard());
        QVERIFY(app.store()->settings().setupCompleted);
        QVERIFY(!app.setupPending());
        QVERIFY(app.captureWindow()->isVisible());
        QVERIFY(app.captureWindow()->editor()->toPlainText().startsWith(QStringLiteral("Welcome to noite")));

        // S2: the review would take the focus and so hide the capture; it waits for the capture.
        QCoreApplication::processEvents();
        QVERIFY(!app.reviewWindow());
        QVERIFY(!app.store()->lastReviewDay()); // not counted yet either
        QVERIFY(app.captureWindow()->isVisible());

        QTest::keyClick(app.captureWindow()->editor(), Qt::Key_Escape);
        QVERIFY(!app.captureWindow()->isVisible());
        QTRY_VERIFY(app.reviewWindow() && app.reviewWindow()->isVisible());
    }

    // A review held by a re-run wizard also waits for the Try-it capture.
    void heldReviewWaitsForTheTryItCapture()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QTRY_VERIFY(app.reviewWindow()); // the startup check
        app.reviewWindow()->close();
        QTRY_VERIFY(!app.reviewWindow());
        openSetupAgain(app);
        app.store()->setLastReviewDay(QDate::currentDate().addDays(-1));
        emit app.host()->sessionUnlocked(); // due now, held by the wizard
        while (app.wizard()->currentId() != SetupWizard::kTryItPage)
            app.wizard()->next();

        emit app.host()->hotkeyPressed(App::kCaptureHotkeyId);
        QVERIFY(!app.wizard());
        QVERIFY(app.captureWindow()->isVisible());
        QCoreApplication::processEvents();
        QVERIFY(!app.reviewWindow());
        app.store()->setLastReviewDay(QDate::currentDate().addDays(-1));
        emit app.host()->sessionUnlocked(); // due again while the capture is open: still held
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(QDate::currentDate()));
        QVERIFY(!app.reviewWindow());

        QTest::keyClick(app.captureWindow()->editor(), Qt::Key_Escape);
        QVERIFY(!app.captureWindow()->isVisible());
        QTRY_VERIFY(app.reviewWindow() && app.reviewWindow()->isVisible());
    }

    void wizardAbandonReturns()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root, false);
        {
            FakeBackend backend;
            App app(AppOptions{root}, nullptr, nullptr, &backend);
            QVERIFY(!app.start());
            QVERIFY(app.wizard() && app.wizard()->isVisible());
            app.wizard()->reject();
            QVERIFY(!app.wizard());
            QVERIFY(!app.store()->settings().setupCompleted);
        }
        FakeBackend backend;
        App second(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!second.start());
        QVERIFY(second.wizard() && second.wizard()->isVisible());
    }

    void completedSkipsWizard()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QVERIFY(!app.wizard());
        QVERIFY(!app.setupPending());
        QVERIFY(backend.has(App::kCaptureHotkeyId, "Ctrl+Alt+Shift+F9"));
        QVERIFY(backend.has(App::kBoardHotkeyId, "Ctrl+Alt+Shift+F8"));
    }

    void settingsInTrayOrder()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QVERIFY(app.trayMenu());
        QStringList texts;
        for (QAction* action : app.trayMenu()->actions()) {
            if (!action->isSeparator())
                texts.append(action->text());
        }
        QCOMPARE(texts,
                 QStringList({"New note", "Board", "Morning review", "Start with Windows", "Settings", "Back up now",
                              "Quit"}));
    }

    void settingsOpensOnce()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());

        app.showSettings();
        SettingsDialog* first = app.settingsDialog();
        QVERIFY(first && first->isVisible());
        app.showSettings();
        QCOMPARE(app.settingsDialog(), first);

        first->reject();
        QVERIFY(!app.settingsDialog());

        app.showWizard();
        QVERIFY(app.wizard() && app.wizard()->isVisible());
        app.showSettings();
        QVERIFY(!app.settingsDialog());
        QVERIFY(app.wizard() && app.wizard()->isVisible());
    }

    void settingsThatCannotOpenToast()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QSignalSpy toasts(&app, &App::toastShown);

        execSql(root, "ALTER TABLE settings RENAME TO settings_gone");
        QVERIFY_THROWS_NO_EXCEPTION(app.showSettings());
        execSql(root, "ALTER TABLE settings_gone RENAME TO settings");

        QVERIFY(!app.settingsDialog());
        QCOMPARE(toasts.count(), 1);
        QVERIFY2(toasts[0][1].toString().startsWith("Settings could not be opened: "),
                 qPrintable(toasts[0][1].toString()));
        QVERIFY(toasts[0][1].toString().contains("no such table"));
    }

    void runSetupAgainOpensWizard()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());

        app.showSettings();
        QVERIFY(app.settingsDialog() && app.settingsDialog()->isVisible());
        emit app.settingsDialog()->runSetupRequested();
        QVERIFY(!app.settingsDialog());
        QVERIFY(app.wizard() && app.wizard()->isVisible());
    }

    void wizardAbandonSavesPartialWhenHotkeyTaken()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        FakeBackend backend;
        backend.taken.insert("Ctrl+Alt+Shift+F8");
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QSignalSpy unavailable(&app, &App::hotkeyUnavailable);
        QVERIFY(!app.start());
        QVERIFY(app.wizard());

        noite::Settings partial = app.wizard()->collected();
        partial.trashRetentionDays = 45;
        emit app.wizard()->abandoned(partial);
        QCOMPARE(app.store()->settings().trashRetentionDays, 45);
        QVERIFY(!app.store()->settings().setupCompleted);
        QVERIFY(backend.has(App::kCaptureHotkeyId, "Ctrl+Alt+Shift+F9"));
        QCOMPARE(unavailable.count(), 1);
        QCOMPARE(unavailable[0][0].toString(), QString("Ctrl+Alt+Shift+F8"));
    }

    // ---- M6 fix round: nothing fires over a wizard that is open (S6) ----

    void reviewHeldWhileSetupRunsAgain()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QTRY_VERIFY(app.reviewWindow()); // the startup check
        app.reviewWindow()->close();
        QTRY_VERIFY(!app.reviewWindow());
        openSetupAgain(app);

        const QDate today = QDate::currentDate();
        app.store()->setLastReviewDay(today.addDays(-1));
        emit app.host()->sessionUnlocked();
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(today)); // checked, not shown
        app.store()->setLastReviewDay(today.addDays(-1));
        app.scheduler()->fireTimerForTest();
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(today));
        QCoreApplication::processEvents();
        QVERIFY(!app.reviewWindow());

        app.wizard()->reject();
        QVERIFY(!app.wizard());
        QTRY_VERIFY(app.reviewWindow() && app.reviewWindow()->isVisible());
    }

    void reviewHeldDuringFirstRun()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepareOverdue(root, false);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QVERIFY(app.wizard());

        // R7: nothing counts the day while the first run is open, so quitting now keeps the review.
        emit app.host()->sessionUnlocked();
        emit app.host()->resumed();
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>());
        QCoreApplication::processEvents();
        QVERIFY(!app.reviewWindow());

        // Nor does the wizard's own save (the applier rechecks the review for a changed stale days).
        noite::Settings partial = app.wizard()->collected();
        partial.staleDays += 1;
        emit app.wizard()->abandoned(partial);
        QVERIFY(!app.wizard());
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>());
        QTRY_VERIFY(app.reviewWindow() && app.reviewWindow()->isVisible());
    }

    void jobsHeldWhileWizardOpen()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        const Scratch s = app.store()->createScratch(test::html({"old"}));
        app.store()->trash(s.id);
        execSql(root, "UPDATE scratches SET trashed_at = trashed_at - 31 * 86400000");
        openSetupAgain(app);

        app.jobs()->check();
        QCOMPARE(app.store()->list(Tab::Trash, QDate::currentDate()).size(), 1);

        app.wizard()->reject(); // the skipped check runs now
        QCOMPARE(app.store()->list(Tab::Trash, QDate::currentDate()).size(), 0);
    }

    void hotkeysIgnoredWhileWizardOpen()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        openSetupAgain(app);

        emit app.host()->hotkeyPressed(App::kBoardHotkeyId);
        emit app.host()->hotkeyPressed(App::kCaptureHotkeyId); // not on the last page
        QVERIFY(!app.boardWindow());
        QVERIFY(!app.captureWindow()->isVisible());
        QVERIFY(app.wizard() && app.wizard()->isVisible());
    }

    void activationRaisesWizard()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QVERIFY(app.wizard());

        emit app.host()->activationRequested();
        QVERIFY(!app.captureWindow()->isVisible());
        QVERIFY(app.wizard() && app.wizard()->isVisible());
    }

    // A capture already open is focused; no starter note is made behind it.
    void starterNoteSkippedWhenCaptureOpen()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        app.captureWindow()->summon();
        QTest::keyClicks(app.captureWindow()->editor(), "draft");
        while (app.wizard()->currentId() != SetupWizard::kTryItPage)
            app.wizard()->next();

        emit app.host()->hotkeyPressed(App::kCaptureHotkeyId);
        QVERIFY(!app.wizard());
        QVERIFY(app.store()->settings().setupCompleted);
        QVERIFY(app.captureWindow()->isVisible());
        QCOMPARE(app.captureWindow()->editor()->toPlainText(), QString("draft"));
        QCOMPARE(app.store()->list(Tab::All, QDate::currentDate(), "Welcome").size(), 0);
    }

    void wizardFinishSaveFailureToasts()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        {
            FakeBackend backend;
            App app(AppOptions{root}, nullptr, nullptr, &backend);
            QVERIFY(!app.start());
            QSignalSpy toasts(&app, &App::toastShown);
            while (app.wizard()->currentId() != SetupWizard::kTryItPage)
                app.wizard()->next();

            execSql(root, "CREATE TRIGGER no_save BEFORE INSERT ON settings WHEN NEW.key = 'settings' "
                          "BEGIN SELECT RAISE(ABORT, 'disk full'); END");
            app.wizard()->button(QWizard::FinishButton)->click();
            execSql(root, "DROP TRIGGER no_save");

            QVERIFY(!app.wizard());
            QCOMPARE(toasts.count(), 1);
            QVERIFY2(toasts[0][1].toString().startsWith("Setup could not be saved: "),
                     qPrintable(toasts[0][1].toString()));
            QVERIFY(toasts[0][1].toString().contains("disk full"));
            QVERIFY(!app.store()->settings().setupCompleted);
        }
        FakeBackend backend;
        App second(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!second.start());
        QVERIFY(second.wizard() && second.wizard()->isVisible());
    }

    // R8, R10: the fallback goes through the applier, keeping the hotkeys and saving the rest; a
    // taken hotkey alone still completes setup.
    void wizardFinishWithTakenHotkeyKeepsTheRest()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        FakeBackend backend;
        backend.taken.insert("Ctrl+Alt+Shift+F8");
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QSignalSpy toasts(&app, &App::toastShown);
        QVERIFY(!app.start());
        QVERIFY(app.wizard());

        noite::Settings collected = app.wizard()->collected();
        collected.trashRetentionDays = 45;
        emit app.wizard()->finished(collected, false);
        QVERIFY(!app.wizard());
        QCOMPARE(app.store()->settings().trashRetentionDays, 45);
        QCOMPARE(app.store()->settings().boardHotkey, QString("Ctrl+Alt+Shift+F8"));
        QVERIFY(app.store()->settings().setupCompleted);
        QVERIFY(!app.setupPending());
        QVERIFY(backend.has(App::kCaptureHotkeyId, "Ctrl+Alt+Shift+F9"));
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts[0][1].toString(),
                 QString("Ctrl+Alt+Shift+F8 is used by another program. Change it in Settings."));
    }

    // R10: Skip setup on the first page with a taken board hotkey completes setup.
    void skipWithTakenHotkeyCompletesSetup()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
        {
            FakeBackend backend;
            backend.taken.insert("Ctrl+Alt+Shift+F8");
            App app(AppOptions{root}, nullptr, nullptr, &backend);
            QVERIFY(!app.start());
            QVERIFY(app.wizard());
            QCOMPARE(app.wizard()->currentId(), int(SetupWizard::kWelcomePage));
            QSignalSpy toasts(&app, &App::toastShown);

            app.wizard()->button(QWizard::CustomButton1)->click();
            QVERIFY(!app.wizard());
            QVERIFY(app.store()->settings().setupCompleted);
            QVERIFY(backend.has(App::kCaptureHotkeyId, "Ctrl+Alt+Shift+F9"));
            QCOMPARE(toasts.count(), 1);
            QCOMPARE(toasts[0][1].toString(),
                     QString("Ctrl+Alt+Shift+F8 is used by another program. Change it in Settings."));
        }
        FakeBackend backend;
        backend.taken.insert("Ctrl+Alt+Shift+F8");
        App second(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!second.start());
        QVERIFY(!second.wizard());
        QVERIFY(!second.setupPending());
    }

    // A store that can't be read when the wizard closes: reported, and nothing stays held.
    void wizardCloseWithUnreadableStoreReleasesHolds()
    {
        for (const bool finish : {true, false}) {
            QTemporaryDir tmp;
            const QString root = tmp.filePath("data");
            prepare(root, "Ctrl+Alt+Shift+F9", "Ctrl+Alt+Shift+F8", false);
            FakeBackend backend;
            App app(AppOptions{root}, nullptr, nullptr, &backend);
            QVERIFY(!app.start());
            QVERIFY(app.wizard());
            const Scratch s = app.store()->createScratch(test::html({"old"}));
            app.store()->trash(s.id);
            execSql(root, "UPDATE scratches SET trashed_at = trashed_at - 31 * 86400000");
            QSignalSpy toasts(&app, &App::toastShown);
            const noite::Settings collected = app.wizard()->collected();

            execSql(root, "ALTER TABLE settings RENAME TO settings_gone");
            if (finish)
                QVERIFY_THROWS_NO_EXCEPTION(emit app.wizard()->finished(collected, false));
            else
                QVERIFY_THROWS_NO_EXCEPTION(emit app.wizard()->abandoned(collected));
            execSql(root, "ALTER TABLE settings_gone RENAME TO settings");

            QVERIFY(!app.wizard());
            QVERIFY(!app.setupPending());
            QCOMPARE(toasts.count(), 1);
            QVERIFY2(toasts[0][1].toString().startsWith("Setup could not be saved: "),
                     qPrintable(toasts[0][1].toString()));
            QVERIFY(toasts[0][1].toString().contains("no such table"));
            app.jobs()->check(); // no longer held
            QCOMPARE(app.store()->list(Tab::Trash, QDate::currentDate()).size(), 0);
        }
    }

    void runSetupAgainWithUnreadableStoreToasts()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        const Scratch s = app.store()->createScratch(test::html({"old"}));
        app.store()->trash(s.id);
        execSql(root, "UPDATE scratches SET trashed_at = trashed_at - 31 * 86400000");
        QSignalSpy toasts(&app, &App::toastShown);

        execSql(root, "ALTER TABLE settings RENAME TO settings_gone");
        QVERIFY_THROWS_NO_EXCEPTION(app.showWizard());
        execSql(root, "ALTER TABLE settings_gone RENAME TO settings");

        QVERIFY(!app.wizard());
        QCOMPARE(toasts.count(), 1);
        QVERIFY2(toasts[0][1].toString().startsWith("Setup could not be opened: "),
                 qPrintable(toasts[0][1].toString()));
        QVERIFY(toasts[0][1].toString().contains("no such table"));
        app.jobs()->check(); // not held
        QCOMPARE(app.store()->list(Tab::Trash, QDate::currentDate()).size(), 0);
    }

    // The tray's Start with Windows reaches an open Settings, so its Save doesn't undo it.
    void trayAutostartToggleSyncsOpenSettings()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        std::optional<QString> value;
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, std::make_unique<FakeAutostart>(&value), &backend);
        QVERIFY(!app.start());
        app.showSettings();
        QVERIFY(app.settingsDialog());
        QVERIFY(app.settingsDialog()->startWithWindowsCheck()->isChecked());

        app.autostartAction()->trigger();
        QVERIFY(!app.settingsDialog()->startWithWindowsCheck()->isChecked());
        app.settingsDialog()->save();
        QVERIFY(!app.store()->settings().startWithWindows);
        QVERIFY(!value);
        QVERIFY(!app.autostartAction()->isChecked());
    }

    // R9: the dialog's folder is used (and saved) without asking.
    void backUpNowFromSettingsUsesTheFieldsFolder()
    {
        QTemporaryDir tmp;
        QTemporaryDir dest;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QSignalSpy toasts(&app, &App::toastShown);
        int asked = 0;
        app.setBackupFolderAsker([&asked] {
            ++asked;
            return std::optional<QString>();
        });
        app.showSettings();
        QVERIFY(app.settingsDialog());
        BackupFolderField::Values values = app.settingsDialog()->backupField()->values();
        values.folder = dest.path();
        app.settingsDialog()->backupField()->setValues(values);

        emit app.settingsDialog()->backUpNowRequested();
        QCOMPARE(asked, 0);
        QCOMPARE(app.store()->settings().backupDir, std::optional<QString>(dest.path()));
        QTRY_COMPARE_WITH_TIMEOUT(toasts.count(), 1, 10000);
        QVERIFY(toasts[0][1].toString().startsWith(QString::fromUtf8("Backed up · ")));
    }

    void backUpNowFromSettingsSyncsBackupField()
    {
        QTemporaryDir tmp;
        QTemporaryDir dest;
        const QString root = tmp.filePath("data");
        prepare(root, "Ctrl+Alt+Shift+F9");
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());
        QSignalSpy toasts(&app, &App::toastShown);

        const QString folder = dest.path();
        app.setBackupFolderAsker([folder] { return std::optional<QString>(folder); });
        app.showSettings();
        QVERIFY(app.settingsDialog());
        QVERIFY(!app.settingsDialog()->backupField()->values().folder);

        emit app.settingsDialog()->backUpNowRequested();
        QTRY_COMPARE_WITH_TIMEOUT(toasts.count(), 1, 10000);
        QCOMPARE(app.settingsDialog()->backupField()->values().folder, std::optional<QString>(folder));

        app.settingsDialog()->save();
        QCOMPARE(app.store()->settings().backupDir, std::optional<QString>(folder));
    }
};

NOITE_TEST(AppTest)
#include "tst_app.moc"
