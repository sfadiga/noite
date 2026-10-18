#include "helpers.h"
#include "registry.h"

#include "store_internal.h"

using namespace noite;
using namespace noite::test;

namespace {

QStringList ids(const QList<ScratchSummary>& list)
{
    QStringList out;
    for (const ScratchSummary& s : list)
        out << s.id;
    return out;
}

struct Board {
    std::unique_ptr<Fixture> f = fixture();
    QString overdueA, overdueB, dueToday, upcoming, undatedOld, undatedNew, done, trashed;

    QString make(const QString& title, std::optional<QDate> due = std::nullopt)
    {
        const QString id = f->store->createScratch(html({title})).id;
        if (due)
            f->store->setDue(id, due);
        return id;
    }

    Board()
    {
        overdueB = make("overdue b", QDate(2026, 10, 14));
        overdueA = make("overdue a", QDate(2026, 10, 13));
        upcoming = make("upcoming", QDate(2026, 10, 20));
        dueToday = make("due today", QDate(2026, 10, 15));
        undatedOld = make("undated old");
        f->clock->advance(3'600'000);
        undatedNew = make("undated new");
        done = make("done");
        f->store->markDone(done);
        trashed = make("trashed");
        f->store->trash(trashed);
    }
};

// For chip filters: colours, tags and dates across tabs (M3b).
struct Chips {
    std::unique_ptr<Fixture> f = fixture();
    QString red1, blue1, red2, plain, doneRed;

    QString make(const QString& text, std::optional<Color> color, std::optional<QDate> due = std::nullopt)
    {
        const QString id = f->store->createScratch(html({text})).id;
        if (color)
            f->store->setColor(id, color);
        if (due)
            f->store->setDue(id, due);
        f->clock->advance(60'000);
        return id;
    }

    Chips()
    {
        red1 = make("red one #trip", Color::Red, QDate(2026, 10, 14));  // overdue
        blue1 = make("blue one #trip", Color::Blue, QDate(2026, 10, 15)); // due today
        red2 = make("red two zebra #work", Color::Red);
        plain = make("plain", std::nullopt);
        doneRed = make("done red #trip", Color::Red);
        f->store->markDone(doneRed);
    }
    QStringList today(const BoardFilter& filter, const QString& search = {})
    {
        return ids(f->store->list(Tab::Today, test::today(), search, filter));
    }
};

BoardFilter colors(std::initializer_list<Color> c)
{
    BoardFilter f;
    f.colors = QSet<Color>(c);
    return f;
}

} // namespace

class BoardTest : public QObject {
    Q_OBJECT

private slots:
    void todayTabOrder()
    {
        Board b;
        QCOMPARE(ids(b.f->store->list(Tab::Today, today())),
                 QStringList({b.overdueA, b.overdueB, b.dueToday, b.undatedOld, b.undatedNew}));
    }

    void allTabOrder()
    {
        Board b;
        QCOMPARE(ids(b.f->store->list(Tab::All, today())),
                 QStringList({b.overdueA, b.overdueB, b.dueToday, b.undatedOld, b.undatedNew, b.upcoming}));
    }

    void completedAndTrashNewestFirst()
    {
        Board b;
        const QString done2 = b.make("done later");
        b.f->clock->advance(1000);
        b.f->store->markDone(done2);
        QCOMPARE(ids(b.f->store->list(Tab::Completed, today())), QStringList({done2, b.done}));

        const QString trashed2 = b.make("trashed later");
        b.f->clock->advance(1000);
        b.f->store->trash(trashed2);
        QCOMPARE(ids(b.f->store->list(Tab::Trash, today())), QStringList({trashed2, b.trashed}));
    }

    void searchPrefixAndAccentInsensitive()
    {
        auto f = fixture();
        const QString milk = f->store->createScratch(html({"Buy milk"})).id;
        const QString reuniao = f->store->createScratch(html({"Reunião amanhã"})).id;
        const QString oldMilk = f->store->createScratch(html({"milk old"})).id;
        f->store->trash(oldMilk);
        QCOMPARE(ids(f->store->list(Tab::All, today(), "mil")), QStringList({milk}));
        QCOMPARE(ids(f->store->list(Tab::All, today(), "reuniao")), QStringList({reuniao}));
        QCOMPARE(ids(f->store->list(Tab::All, today(), "REUN")), QStringList({reuniao}));
        QCOMPARE(ids(f->store->list(Tab::Trash, today(), "mil")), QStringList({oldMilk}));
    }

    void searchNeverThrowsOnFtsSyntax()
    {
        Board b;
        for (const char* q : {"\"", "-", "*", "(", "AND", "a OR", "NEAR(", "\"open", "#home", "x:y", "^"}) {
            try {
                b.f->store->list(Tab::All, today(), q);
            } catch (const std::exception& e) {
                QFAIL(qPrintable(QString("query %1 threw: %2").arg(q, e.what())));
            }
        }
        QCOMPARE(ids(b.f->store->list(Tab::All, today(), "   ")), ids(b.f->store->list(Tab::All, today())));
    }

    void tabCountsMatchLists()
    {
        Board b;
        const TabCounts c = b.f->store->tabCounts(today());
        for (Tab tab : {Tab::Today, Tab::All, Tab::Completed, Tab::Trash})
            QCOMPARE(c.of(tab), int(b.f->store->list(tab, today()).size()));
        QCOMPARE(c.of(Tab::Today), 5);
        QCOMPARE(c.of(Tab::All), 6);
        QCOMPARE(c.of(Tab::Completed), 1);
        QCOMPARE(c.of(Tab::Trash), 1);
    }

    void tabCountsFollowSearch()
    {
        Board b;
        const TabCounts c = b.f->store->tabCounts(today(), "undated");
        QCOMPARE(c.of(Tab::Today), 2);
        QCOMPARE(c.of(Tab::All), 2);
        QCOMPARE(c.of(Tab::Completed), 0);
        QCOMPARE(c.of(Tab::Trash), 0);
        const TabCounts none = b.f->store->tabCounts(today(), "\"(");
        QCOMPARE(none.of(Tab::All), 0);
    }

    void filterOrWithinKindAndAcrossKinds()
    {
        Chips c;
        QCOMPARE(c.today(colors({Color::Red})), QStringList({c.red1, c.red2}));
        QCOMPARE(c.today(colors({Color::Red, Color::Blue})), QStringList({c.red1, c.blue1, c.red2}));
        BoardFilter redTrip = colors({Color::Red});
        redTrip.tags = {"trip"};
        QCOMPARE(c.today(redTrip), QStringList({c.red1}));
        BoardFilter undated;
        undated.dates = {DateChip::NoDate};
        QCOMPARE(c.today(undated), QStringList({c.red2, c.plain}));
        BoardFilter datedTrip;
        datedTrip.dates = {DateChip::Overdue, DateChip::DueToday};
        datedTrip.tags = {"trip"};
        QCOMPARE(c.today(datedTrip), QStringList({c.red1, c.blue1}));
        QVERIFY(BoardFilter().isEmpty());
        QVERIFY(!redTrip.isEmpty());
    }

    void filterCombinesWithSearch()
    {
        Chips c;
        QCOMPARE(c.today(colors({Color::Red}), "zebra"), QStringList({c.red2}));
        QCOMPARE(c.today(colors({Color::Blue}), "zebra"), QStringList());
    }

    void tabCountsFollowFilter()
    {
        Chips c;
        const BoardFilter red = colors({Color::Red});
        const TabCounts counts = c.f->store->tabCounts(today(), {}, red);
        for (Tab tab : {Tab::Today, Tab::All, Tab::Completed, Tab::Trash})
            QCOMPARE(counts.of(tab), int(c.f->store->list(tab, today(), {}, red).size()));
        QCOMPARE(counts.of(Tab::Today), 2);
        QCOMPARE(counts.of(Tab::All), 2);
        QCOMPARE(counts.of(Tab::Completed), 1);
        QCOMPARE(counts.of(Tab::Trash), 0);
    }

    // C6: a kind's chips ignore that kind's own selection; other kinds narrow them.
    void facetsFollowOtherKinds()
    {
        Chips c;
        const ChipFacets facets = c.f->store->chipFacets(Tab::Today, today(), {}, colors({Color::Red}));
        using ColorCount = std::pair<Color, int>;
        using DateCount = std::pair<DateChip, int>;
        using TagCount = std::pair<QString, int>;
        QCOMPARE(facets.colors, QList<ColorCount>({{Color::Red, 2}, {Color::Blue, 1}}));
        QCOMPARE(facets.tags, QList<TagCount>({{"trip", 1}, {"work", 1}}));
        QCOMPARE(facets.dates, QList<DateCount>({{DateChip::Overdue, 1}, {DateChip::NoDate, 1}}));
    }

    void facetsKeepActiveZeroChips()
    {
        Chips c;
        BoardFilter gone;
        gone.tags = {"gone"};
        const ChipFacets facets = c.f->store->chipFacets(Tab::Today, today(), {}, gone);
        QVERIFY(facets.tags.contains(std::pair<QString, int>("gone", 0)));
        QVERIFY(c.today(gone).isEmpty());
    }

    void listTagsIncludesInlineAndManual()
    {
        Chips c;
        c.f->store->setManualTags(c.plain, {"Errand"});
        QCOMPARE(c.f->store->listTags(), QStringList({"errand", "trip", "work"}));
    }

    void filterIgnoresTagCase()
    {
        Chips c;
        BoardFilter upper;
        upper.tags = {"TRIP"};
        QCOMPARE(c.today(upper), QStringList({c.red1, c.blue1}));
    }

    void reviewCountsMatchFixture()
    {
        Board b;
        const ReviewCounts c = b.f->store->reviewCounts(today());
        QCOMPARE(c.overdue, 2);
        QCOMPARE(c.dueToday, 1);
        QCOMPARE(c.open, 6);
    }

    void summaryPreviewTagsAndFirstImage()
    {
        auto f = fixture();
        const QString longId = f->store->createScratch(html({QString(250, 'x') + " #t"})).id;
        const QString image = fakeAttachmentId('c');
        const QString imageId = f->store->createScratch(htmlWithImage("pic", image)).id;
        const QList<ScratchSummary> all = f->store->list(Tab::All, today());
        for (const ScratchSummary& s : all) {
            if (s.id == longId) {
                QCOMPARE(s.preview, QString(200, 'x'));
                QCOMPARE(s.tags, QStringList({"t"}));
                QVERIFY(!s.firstImage);
            } else {
                QCOMPARE(s.id, imageId);
                QVERIFY(s.firstImage == image);
            }
        }
        QCOMPARE(all.size(), 2);
    }

    void ftsQueryEscaping()
    {
        QVERIFY(detail::ftsQuery("a \"b") == QString("\"a\"* \"\"\"b\"*"));
        QVERIFY(!detail::ftsQuery(" "));
        QVERIFY(!detail::ftsQuery(""));
    }
};

NOITE_TEST(BoardTest)
#include "tst_board.moc"
