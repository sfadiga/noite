#include "helpers.h"
#include "registry.h"

#include "setup/SetupTexts.h"

#include <noite/data_dir.h>
#include <noite/model.h>
#include <noite/settings.h>
#include <noite/store.h>

#include <QDateTime>
#include <QTest>
#include <QTextDocument>

using namespace noite;

class SetupTextsTest : public QObject {
    Q_OBJECT

private slots:
    void starterNoteUsesSettings()
    {
        Settings s;
        s.captureHotkey = "Ctrl+Alt+K";
        s.boardHotkey = "Ctrl+Alt+L";
        s.staleDays = 9;
        const QString html = starterNoteHtml(s);

        QTextDocument doc;
        doc.setHtml(html);
        const QString text = doc.toPlainText();
        QVERIFY(text.contains("Welcome to noite"));
        QVERIFY(text.contains("Ctrl+Alt+K opens this window"));
        QVERIFY(text.contains("Ctrl+Alt+L opens the board"));
        QVERIFY(text.contains("untouched for 9 days"));

        auto f = test::fixture();
        QCOMPARE(f->store->createScratch(html).title, QString("Welcome to noite"));
    }

    void lastBackupTexts()
    {
        const QDate today(2026, 10, 18);
        JobState job;
        QCOMPARE(lastBackupText(job, today), QString("never"));
        job.lastBackupDay = QDate(2026, 10, 18);
        QCOMPARE(lastBackupText(job, today), QString("today"));
        job.lastBackupDay = QDate(2026, 10, 17);
        QCOMPARE(lastBackupText(job, today), QString("yesterday"));
        job.lastBackupDay = QDate(2026, 10, 15);
        QCOMPARE(lastBackupText(job, today), QString("Oct 15"));
        job.backupFailingSinceMs = QDateTime(QDate(2026, 10, 15), QTime(9, 0)).toMSecsSinceEpoch();
        QCOMPARE(lastBackupText(job, today), QString("failing since Oct 15"));
    }

    void syncWarningNamesProvider()
    {
        QVERIFY(syncWarningText(SyncProvider::GoogleDrive).contains("inside Google Drive."));
        QVERIFY(syncWarningText(SyncProvider::OneDrive).contains("inside OneDrive."));
        QVERIFY(syncWarningText(SyncProvider::Dropbox).contains("inside Dropbox."));
    }
};

NOITE_TEST(SetupTextsTest)
#include "tst_setup_texts.moc"
