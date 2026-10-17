#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"

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
};

NOITE_TEST(BoardModelTest)
#include "tst_board_model.moc"
