#include "registry.h"

#include "platform/DriveFolder.h"

#include <QTest>

using namespace noite;

class DriveFolderTest : public QObject {
    Q_OBJECT
private slots:
    void picksMeuDrive()
    {
        const QList<DriveVolume> volumes{{"C:/", "Windows"}, {"G:/", "Google Drive"}};
        const auto folder = suggestedDriveFolder(volumes, [](const QString& p) { return p == "G:/Meu Drive"; });
        QCOMPARE(folder, std::optional<QString>("G:/Meu Drive"));
    }

    void picksMyDrive()
    {
        const QList<DriveVolume> volumes{{"G:/", "GOOGLE DRIVE"}};
        const auto folder = suggestedDriveFolder(volumes, [](const QString& p) { return p == "G:/My Drive"; });
        QCOMPARE(folder, std::optional<QString>("G:/My Drive"));
    }

    void fallsBackToRoot()
    {
        const QList<DriveVolume> volumes{{"G:/", "Google Drive"}};
        const auto folder = suggestedDriveFolder(volumes, [](const QString&) { return false; });
        QCOMPARE(folder, std::optional<QString>("G:/"));
    }

    void noDriveVolume()
    {
        const QList<DriveVolume> volumes{{"C:/", "Windows"}};
        const auto folder = suggestedDriveFolder(volumes, [](const QString&) { return true; });
        QVERIFY(!folder);
    }
};

NOITE_TEST(DriveFolderTest)
#include "tst_drive_folder.moc"
