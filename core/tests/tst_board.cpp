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
