#include "registry.h"

#include "platform/Autostart.h"

#include <QTest>

namespace {

struct FakeAutostart final : AutostartRegistry {
    std::optional<QString> value;
    int sets = 0;
    bool fail = false;
    std::optional<QString> current() const override { return value; }
    bool set(const QString& command) override
    {
        ++sets;
        if (fail)
            return false;
        value = command;
        return true;
    }
    bool remove() override
    {
        value.reset();
        return true;
    }
};

} // namespace

class AutostartTest : public QObject {
    Q_OBJECT

private slots:
    void commandForDefaultFolder()
    {
        QCOMPARE(autostartCommand("C:/noite/noite.exe", "C:/noite/data", "C:/noite/data"),
                 QString("\"C:\\noite\\noite.exe\""));
        QCOMPARE(autostartCommand("C:/noite/noite.exe", "c:/NOITE/data/", "C:/noite/data"),
                 QString("\"C:\\noite\\noite.exe\"")); // same folder, other spelling
    }

    // Review Focus 5.
    void commandQuotesPathsWithSpaces()
    {
        QCOMPARE(autostartCommand("D:/My Apps/noite/noite.exe", QString::fromUtf8("D:/Notas João/data"),
                                  "D:/My Apps/noite/data"),
                 QString::fromUtf8("\"D:\\My Apps\\noite\\noite.exe\" --data \"D:\\Notas João\\data\""));
    }

    void applyOnSetsOnlyWhenDifferent()
    {
        FakeAutostart fake;
        fake.value = "\"C:\\noite\\noite.exe\"";
        QVERIFY(applyAutostart(fake, true, "\"C:\\noite\\noite.exe\""));
        QCOMPARE(fake.sets, 0);
        QVERIFY(applyAutostart(fake, true, "\"D:\\noite\\noite.exe\""));
        QCOMPARE(fake.sets, 1);
        QCOMPARE(fake.value, std::optional<QString>("\"D:\\noite\\noite.exe\""));
    }

    void applyOffRemoves()
    {
        FakeAutostart fake;
        fake.value = "x";
        QVERIFY(applyAutostart(fake, false, "x"));
        QCOMPARE(fake.current(), std::optional<QString>());
    }

    void applyReportsFailure()
    {
        FakeAutostart fake;
        fake.fail = true;
        QVERIFY(!applyAutostart(fake, true, "x"));
    }
};

NOITE_TEST(AutostartTest)
#include "tst_autostart.moc"
