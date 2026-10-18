#include "registry.h"

#include "jobs/BackupFolderDialog.h"

#include <QPushButton>
#include <QTest>

using namespace noite;

class BackupFolderDialogTest : public QObject {
    Q_OBJECT
private slots:
    void showsDrivePath()
    {
        BackupFolderDialog dialog("G:/Meu Drive", [] { return QString(); });
        QCOMPARE(dialog.windowTitle(), QString("Back up noite"));
        QCOMPARE(dialog.shownPath(), QString("G:\\Meu Drive\\noite-backups"));
        QCOMPARE(dialog.driveButton()->text(), QString("Back up to Drive"));
        QCOMPARE(dialog.otherButton()->text(), QString("Choose another folder\u2026"));
        QCOMPARE(dialog.cancelButton()->text(), QString("Cancel"));
        QVERIFY(dialog.driveButton()->isDefault());
    }

    void driveButtonChoosesDrive()
    {
        BackupFolderDialog dialog("G:/Meu Drive", [] { return QString(); });
        dialog.show();
        dialog.driveButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.chosenFolder(), std::optional<QString>("G:/Meu Drive"));
    }

    void otherButtonUsesPicker()
    {
        QString answer = "D:/bk";
        BackupFolderDialog dialog("G:/Meu Drive", [&] { return answer; });
        dialog.show();
        dialog.otherButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.chosenFolder(), std::optional<QString>("D:/bk"));
    }

    void pickerCancelledKeepsDialogOpen()
    {
        BackupFolderDialog dialog("G:/Meu Drive", [] { return QString(); });
        dialog.show();
        dialog.otherButton()->click();
        QVERIFY(dialog.isVisible());
        QVERIFY(!dialog.chosenFolder());
    }

    void cancelChoosesNothing()
    {
        BackupFolderDialog dialog("G:/Meu Drive", [] { return QString(); });
        dialog.show();
        dialog.cancelButton()->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!dialog.chosenFolder());
    }
};

NOITE_TEST(BackupFolderDialogTest)
#include "tst_backup_folder_dialog.moc"
