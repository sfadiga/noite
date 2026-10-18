#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/CardDelegate.h"

#include <noite/store.h>

#include <QSignalSpy>
#include <QTest>

using namespace noite;

namespace {

struct Board {
    std::unique_ptr<test::Fixture> f = test::fixture();
    BoardModel model{*f->store, [] { return test::today(); }};

    ScratchId add(const QString& text) { return f->store->createScratch(test::html({text})).id; }
};

// a, then bundle "b" holding b and c, then d (all undated, oldest first).
struct Bundled : Board {
    ScratchId a, b, c, d;
    BundleId bundle;
    Bundled()
    {
        a = add("a");
        f->clock->advance(1000);
        b = add("b");
        f->clock->advance(1000);
        c = add("c");
        f->clock->advance(1000);
        d = add("d");
        bundle = f->store->bundleTogether(b, c);
        model.reload();
    }
    QStringList shape() const // "a", "[b]" for a stack, "b*" for a member shown in an expanded stack
    {
        QStringList out;
        for (int r = 0; r < model.rowCount(); ++r) {
            const BoardRow& row = model.row(r);
            if (row.kind == BoardRow::Kind::Stack)
                out << "[" + row.bundleName + "]";
            else
                out << row.card.title + (row.inBundle ? "*" : "");
        }
        return out;
    }
};

} // namespace

class BoardModelTest : public QObject {
    Q_OBJECT

private slots:
    void rowsMatchStoreForEachTab()
    {
        Board b;
        const ScratchId open = b.add("open");
        b.f->store->markDone(b.add("done"));
        b.f->store->trash(b.add("trashed"));
        b.model.reload();
        QCOMPARE(b.model.rowCount(), 1);
        QCOMPARE(b.model.summary(0).id, open);
        b.model.setTab(Tab::Completed);
        QCOMPARE(b.model.rowCount(), 1);
        QCOMPARE(b.model.summary(0).title, QString("done"));
        b.model.setTab(Tab::Trash);
        QCOMPARE(b.model.rowCount(), 1);
        QCOMPARE(b.model.summary(0).title, QString("trashed"));
        QCOMPARE(b.model.tab(), Tab::Trash);
    }

    void searchNarrowsRows()
    {
        Board b;
        b.add("buy milk");
        b.add("call bob");
        b.model.setSearch("mil");
        QCOMPARE(b.model.search(), QString("mil"));
        QCOMPARE(b.model.rowCount(), 1);
        QCOMPARE(b.model.summary(0).title, QString("buy milk"));
        QCOMPARE(b.model.data(b.model.index(0), Qt::DisplayRole).toString(), QString("buy milk"));
        b.model.setSearch("");
        QCOMPARE(b.model.rowCount(), 2);
    }

    void reloadPicksUpNewNote()
    {
        Board b;
        b.model.reload();
        QCOMPARE(b.model.rowCount(), 0);
        b.add("arrived later");
        QSignalSpy resets(&b.model, &QAbstractItemModel::modelReset);
        b.model.reload();
        QCOMPARE(b.model.rowCount(), 1);
        QCOMPARE(resets.count(), 1);
    }

    void rowOfFindsId()
    {
        Board b;
        b.add("first");
        const ScratchId second = b.add("second");
        b.model.reload();
        QVERIFY(b.model.rowOf(second) >= 0);
        QCOMPARE(b.model.summary(b.model.rowOf(second)).id, second);
        QCOMPARE(b.model.rowOf("unknown"), -1);
    }

    // Review Focus 3.
    void searchWithQueryOperatorsDoesNotThrow()
    {
        Board b;
        b.add("plain note");
        QSignalSpy errors(&b.model, &BoardModel::errorOccurred);
        for (const QString& s : {QString("\""), QString("*"), QString("-x"), QString("AND"), QString("("),
                                 QString("NEAR"), QString("a\"b")}) {
            b.model.setSearch(s);
            QCOMPARE(errors.count(), 0);
        }
    }

    void stackTakesFirstMemberPosition()
    {
        Bundled t;
        QCOMPARE(t.shape(), QStringList({"a", "[b]", "d"}));
        QVERIFY(t.model.isStack(1));
        QCOMPARE(t.model.row(1).bundleId, t.bundle);
        QCOMPARE(t.model.row(1).members.size(), 2);
        QCOMPARE(t.model.summary(1).id, t.b); // a stack's summary is its first member
    }

    void expandedStackListsMembersAfterIt()
    {
        Bundled t;
        t.model.setExpanded(t.bundle, true);
        QVERIFY(t.model.isExpanded(t.bundle));
        QCOMPARE(t.shape(), QStringList({"a", "[b]", "b*", "c*", "d"}));
        QVERIFY(t.model.row(1).expanded);
        t.model.setExpanded(t.bundle, false);
        QCOMPARE(t.shape(), QStringList({"a", "[b]", "d"}));
        t.model.setExpandedSet({t.bundle});
        QCOMPARE(t.model.expanded(), QSet<BundleId>({t.bundle}));
    }

    // Review Focus 1.
    void stackShowsOnlyMembersInTab()
    {
        Bundled t;
        t.f->store->markDone(t.c);
        t.model.reload();
        QCOMPARE(t.model.row(1).members.size(), 1);
        QCOMPARE(CardDelegate::stackLines(t.model.row(1)).value(1), QString("1 note"));
        t.model.setTab(Tab::Completed);
        QCOMPARE(t.shape(), QStringList({"[b]"}));
        QCOMPARE(t.model.row(0).members.first().id, t.c);
    }

    void filterHidesEmptyStack()
    {
        Bundled t;
        t.f->store->setColor(t.a, Color::Red);
        BoardFilter red;
        red.colors = {Color::Red};
        t.model.setFilter(red);
        QCOMPARE(t.model.filter(), red);
        QCOMPARE(t.shape(), QStringList({"a"}));
    }

    void rowOfIgnoresStacks()
    {
        Bundled t;
        QCOMPARE(t.model.rowOf(t.b), -1);
        QCOMPARE(t.model.rowOf(t.d), 2);
        QCOMPARE(t.model.rowOfStack(t.bundle), 1);
        t.model.setExpanded(t.bundle, true);
        QCOMPARE(t.model.rowOf(t.c), 3);
    }
};

NOITE_TEST(BoardModelTest)
#include "tst_board_model.moc"
