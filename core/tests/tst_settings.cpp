#include "helpers.h"
#include "registry.h"

#include <noite/settings.h>

#include <sqlite3.h>

using namespace noite;
using namespace noite::test;

class SettingsTest : public QObject {
    Q_OBJECT

private slots:
    void defaultsMatchSpec()
    {
        const Settings d;
        QCOMPARE(d.captureHotkey, QString("Ctrl+Alt+J"));
        QCOMPARE(d.boardHotkey, QString("Ctrl+Alt+B"));
        QCOMPARE(d.trashRetentionDays, 30);
        QCOMPARE(d.dayStart, QTime(6, 0));
        QVERIFY(!d.backupDir);
        QVERIFY(d.backupFrequency == BackupFrequency::Daily);
        QCOMPARE(d.backupKeep, 14);
        QVERIFY(d.startWithWindows);
        QVERIFY(!d.setupCompleted);

        auto f = fixture();
        QVERIFY(f->store->settings() == d);
    }

    void roundTrip()
    {
        auto f = fixture();
        Settings s;
        s.captureHotkey = "Ctrl+Shift+Space";
        s.trashRetentionDays = 7;
        s.dayStart = QTime(5, 30);
        s.backupDir = R"(G:\My Drive\noite-backups)";
        s.backupFrequency = BackupFrequency::Weekly;
        s.backupKeep = 4;
        s.startWithWindows = false;
        s.setupCompleted = true;
        f->store->saveSettings(s);
        reopen(*f);
        QVERIFY(f->store->settings() == s);
    }

    void validation()
    {
        auto f = fixture();
        Settings ok;
        ok.trashRetentionDays = 10;
        f->store->saveSettings(ok);
        for (int days : {0, 366}) {
            Settings bad = ok;
            bad.trashRetentionDays = days;
            NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->saveSettings(bad));
        }
        Settings bad = ok;
        bad.backupKeep = 0;
        NOITE_EXPECT_ERROR(ErrorCode::InvalidSetting, f->store->saveSettings(bad));
        QVERIFY(f->store->settings() == ok);
    }

    void partialJsonFillsDefaults()
    {
        auto f = fixture();
        f->store.reset();
        sqlite3* db = nullptr;
        sqlite3_open((f->root() + "/noite.db").toUtf8().constData(), &db);
        QCOMPARE(sqlite3_exec(db, "INSERT INTO settings(key, value) VALUES('settings', '{\"trashRetentionDays\":7}')",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(db);
        reopen(*f);
        Settings expected;
        expected.trashRetentionDays = 7;
        QVERIFY(f->store->settings() == expected);
    }
};

NOITE_TEST(SettingsTest)
#include "tst_settings.moc"
