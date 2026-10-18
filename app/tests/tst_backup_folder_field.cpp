#include "registry.h"

#include "setup/BackupFolderField.h"

#include <QPushButton>
#include <QTest>

namespace {

QPushButton* changeButton(BackupFolderField& field)
{
    for (QPushButton* button : field.findChildren<QPushButton*>()) {
        if (button->text() == QString::fromUtf8("Change…"))
            return button;
    }
    return nullptr;
}

} // namespace

class BackupFolderFieldTest : public QObject {
    Q_OBJECT

private slots:
    void valuesRoundTrip()
    {
        BackupFolderField field([] { return std::optional<QString>(); });
        const BackupFolderField::Values in{QString("G:/Meu Drive"), noite::BackupFrequency::Weekly, 9};
        field.setValues(in);
        const auto out = field.values();
        QCOMPARE(out.folder, in.folder);
        QVERIFY(out.frequency == noite::BackupFrequency::Weekly);
        QCOMPARE(out.keep, 9);
        QCOMPARE(field.folderText(), QString("G:\\Meu Drive"));
    }

    void changeUsesAsker()
    {
        std::optional<QString> answer = QString("D:/bk");
        BackupFolderField field([&answer] { return answer; });
        field.setValues({QString("G:/old"), noite::BackupFrequency::Daily, 7});
        QPushButton* button = changeButton(field);
        QVERIFY(button);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(field.values().folder, std::optional<QString>("D:/bk"));
        answer.reset();
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(field.values().folder, std::optional<QString>("D:/bk"));
    }

    void unsetText()
    {
        BackupFolderField field([] { return std::optional<QString>(); });
        field.setValues({std::nullopt, noite::BackupFrequency::Daily, 7});
        QCOMPARE(field.folderText(), QString("Not set (Back up now will ask)"));
        QVERIFY(!field.values().folder);
    }

    void keepIsClamped()
    {
        BackupFolderField field([] { return std::optional<QString>(); });
        field.setValues({std::nullopt, noite::BackupFrequency::Daily, 5000});
        QCOMPARE(field.values().keep, 999);
        field.setValues({std::nullopt, noite::BackupFrequency::Daily, 0});
        QCOMPARE(field.values().keep, 1);
    }
};

NOITE_TEST(BackupFolderFieldTest)
#include "tst_backup_folder_field.moc"
