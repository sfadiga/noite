#include "fake_hotkeys.h"
#include "helpers.h"
#include "registry.h"

#include "App.h"
#include "AppOptions.h"
#include "VersionText.h"
#include "board/BoardWindow.h"
#include "editor/AttachmentStore.h"
#include "setup/SettingsDialog.h"

#include <noite/store.h>
#include <noite_version.h>

#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTest>

using namespace noite;

class VersionTest : public QObject {
    Q_OBJECT

private slots:
    void versionHasTheExpectedForm()
    {
        const QRegularExpression form("^\\d+\\.\\d+\\.\\d+(-dev(\\+[0-9a-f]+(\\.dirty)?)?)?$");
        QVERIFY2(form.match(QString(NOITE_VERSION)).hasMatch(), NOITE_VERSION);
    }

    void versionStartsWithProjectVersion()
    {
        const QString base = QString("%1.%2.%3").arg(NOITE_VERSION_MAJOR).arg(NOITE_VERSION_MINOR).arg(NOITE_VERSION_PATCH);
        QVERIFY2(QString(NOITE_VERSION).startsWith(base), NOITE_VERSION);
    }

    void baseVersionIsVersionTxt()
    {
        QFile file(QString(NOITE_SOURCE_DIR) + "/version.txt");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString(NOITE_VERSION_BASE), QString::fromUtf8(file.readAll()).trimmed());
        QVERIFY2(QString(NOITE_VERSION).startsWith(NOITE_VERSION_BASE), NOITE_VERSION);
    }

    void identityCarriesTheVersion()
    {
        applyAppIdentity();
        QCOMPARE(QCoreApplication::applicationName(), QString("noite"));
        QCOMPARE(QCoreApplication::applicationVersion(), QString(NOITE_VERSION));
    }

    void boardVersionTextForms()
    {
        QCOMPARE(boardVersionText("1.0.0"), QString("v1.0.0"));
        QCOMPARE(boardVersionText("1.0.0-dev+abc1234.dirty"), QString("v1.0.0-dev"));
        QCOMPARE(boardVersionText("1.0.0-dev"), QString("v1.0.0-dev"));
    }

    void boardShowsVersionLabel()
    {
        auto f = test::fixture();
        StoreAttachments attachments{*f->store};
        BoardWindow board(*f->store, attachments, [] { return test::today(); });
        QLabel* label = board.versionLabel();
        QVERIFY(label);
        QCOMPARE(label->text(), boardVersionText(NOITE_VERSION));
        QCOMPARE(label->toolTip(), versionToolTip());
        QVERIFY(label->toolTip().contains(QString::fromUtf8("\xC2\xB7")));
        QCOMPARE(label->focusPolicy(), Qt::NoFocus);
        board.show();
        QVERIFY(label->mapTo(&board, QPoint()).x() > board.searchField()->mapTo(&board, QPoint()).x());
    }

    void boardLabelAtMinimumWidth()
    {
        auto f = test::fixture();
        StoreAttachments attachments{*f->store};
        BoardWindow board(*f->store, attachments, [] { return test::today(); });
        board.show();
        board.resize(board.minimumSizeHint().width(), board.height());
        QCoreApplication::processEvents();
        QVERIFY(board.searchField()->width() >= 260);
        QVERIFY(board.versionLabel()->isVisible());
    }

    void appShowsVersion()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath("data");
        {
            auto store = Store::open(DataDir::acquire(root), std::make_shared<SystemClock>());
            Settings settings = store->settings();
            settings.captureHotkey = "Ctrl+Alt+Shift+F9";
            settings.boardHotkey = "Ctrl+Alt+Shift+F8";
            settings.setupCompleted = true;
            store->saveSettings(settings);
        }
        FakeBackend backend;
        App app(AppOptions{root}, nullptr, nullptr, &backend);
        QVERIFY(!app.start());

        QFile log(root + "/logs/noite.log");
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString expectedStart = QString::fromUtf8("noite %1 started · data ").arg(NOITE_VERSION);
        QVERIFY(QString::fromUtf8(log.readAll()).contains(expectedStart));

        const QString text = QString("noite %1").arg(NOITE_VERSION);
        QCOMPARE(app.trayToolTip(), text);

        app.showSettings();
        QVERIFY(app.settingsDialog());
        bool found = false;
        for (QLabel* label : app.settingsDialog()->findChildren<QLabel*>())
            found = found || label->text() == text;
        QVERIFY(found);
    }
};

NOITE_TEST(VersionTest)
#include "tst_version.moc"
