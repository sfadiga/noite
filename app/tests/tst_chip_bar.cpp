#include "registry.h"

#include "board/ChipBar.h"

#include <QAbstractButton>
#include <QSignalSpy>
#include <QTest>

using namespace noite;

namespace {

ChipFacets sample()
{
    ChipFacets f;
    f.colors = {{Color::Red, 2}, {Color::Blue, 1}};
    f.dates = {{DateChip::Overdue, 1}, {DateChip::NoDate, 3}};
    f.tags = {{"trip", 2}};
    return f;
}

} // namespace

class ChipBarTest : public QObject {
    Q_OBJECT

private slots:
    void keysInDisplayOrder()
    {
        ChipBar bar;
        bar.setFacets(sample(), {});
        QCOMPARE(bar.keys(), QStringList({"color:red", "color:blue", "date:overdue", "date:none", "tag:trip"}));
        QVERIFY(!bar.chip("tag:gone"));
    }

    void chipTextShowsCount()
    {
        ChipBar bar;
        bar.setFacets(sample(), {});
        QCOMPARE(bar.chip("tag:trip")->text(), QString("#trip 2"));
        QCOMPARE(bar.chip("date:overdue")->text(), QString("Overdue 1"));
        QCOMPARE(bar.chip("date:none")->text(), QString("No date 3"));
        QCOMPARE(bar.chip("color:red")->text(), QString("2"));
    }

    void clickTogglesFilter()
    {
        ChipBar bar;
        bar.setFacets(sample(), {});
        bar.show();
        QSignalSpy spy(&bar, &ChipBar::filterChanged);
        QTest::mouseClick(bar.chip("color:red"), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
        BoardFilter f = spy.last().first().value<BoardFilter>();
        QCOMPARE(f.colors, QSet<Color>({Color::Red}));

        BoardFilter active;
        active.colors = {Color::Red};
        bar.setFacets(sample(), active); // the board answers with the new facets
        QVERIFY(bar.chip("color:red")->isChecked());
        QTest::mouseClick(bar.chip("tag:trip"), Qt::LeftButton);
        f = spy.last().first().value<BoardFilter>();
        QCOMPARE(f.colors, QSet<Color>({Color::Red}));
        QCOMPARE(f.tags, QStringList({"trip"}));

        active.tags = {"trip"};
        bar.setFacets(sample(), active);
        QTest::mouseClick(bar.chip("color:red"), Qt::LeftButton);
        f = spy.last().first().value<BoardFilter>();
        QVERIFY(f.colors.isEmpty());
        QCOMPARE(f.tags, QStringList({"trip"}));
    }

    void hiddenWithoutChips()
    {
        QWidget parent;
        auto* bar = new ChipBar(&parent);
        parent.show();
        bar->setFacets({}, {});
        QVERIFY(!bar->isVisible());
        bar->setFacets(sample(), {});
        QVERIFY(bar->isVisible());
    }
};

NOITE_TEST(ChipBarTest)
#include "tst_chip_bar.moc"
