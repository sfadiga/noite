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
        const std::optional<QDate> before = app.store()->lastReviewDay();
        app.store()->setLastReviewDay(QDate(2000, 1, 1));
        app.showReview();
        ReviewWindow* first = app.reviewWindow();
        QVERIFY(first && first->isVisible());
        app.showReview();
        QCOMPARE(app.reviewWindow(), first);
        QCOMPARE(app.store()->lastReviewDay(), std::optional<QDate>(QDate(2000, 1, 1)));
        Q_UNUSED(before);
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
};

NOITE_TEST(AppTest)
#include "tst_app.moc"
