#include "registry.h"

#include "Theme.h"

#include <QTest>

class ThemeTest : public QObject {
    Q_OBJECT

private slots:
    // Segoe UI about 17% bigger than Qt's Windows default (Segoe UI 9 pt).
    void appFontIsBiggerSegoeUi()
    {
        const QFont font = theme::appFont();
        QCOMPARE(font.families().value(0), QString("Segoe UI"));
        QCOMPARE(font.pointSizeF(), 10.5);
    }

    void headerFontIsBiggerSemibold()
    {
        const QFont base = theme::appFont();
        const QFont header = theme::headerFont(base);
        QVERIFY(header.pointSizeF() > base.pointSizeF());
        QCOMPARE(header.weight(), QFont::DemiBold);
        QCOMPARE(header.families(), base.families());
    }
};

NOITE_TEST(ThemeTest)
#include "tst_theme.moc"
