#include "helpers.h"
#include "registry.h"

using namespace noite;
using namespace noite::test;

class ScratchTest : public QObject {
    Q_OBJECT

private slots:
    void createDerivesFields()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"Buy milk #home", "and eggs #Errands"}));
        QVERIFY(!s.id.isEmpty());
        QCOMPARE(s.title, QString("Buy milk #home"));
        QCOMPARE(s.tags, QStringList({"errands", "home"}));
        QVERIFY(s.manualTags.isEmpty());
        QCOMPARE(s.status, Status::Active);
        QCOMPARE(s.createdAt, t0());
        QCOMPARE(s.modifiedAt, t0());
    }

    void updateRederivesAndBumpsModified()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"Buy milk #home"}));
        f->clock->advance(1000);
        const Scratch u = f->store->updateContent(s.id, html({"Call bob #work"}));
        QCOMPARE(u.title, QString("Call bob #work"));
        QCOMPARE(u.tags, QStringList({"work"}));
        QCOMPARE(u.modifiedAt, t0() + 1000);
        QCOMPARE(u.createdAt, t0());
    }

    void manualTagsSurviveContentUpdates()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x #inline"}));
        f->store->setManualTags(s.id, {"#Urgent"});
        QCOMPARE(f->store->get(s.id).tags, QStringList({"inline", "urgent"}));
        f->store->updateContent(s.id, html({"no tags now"}));
        const Scratch u = f->store->get(s.id);
        QCOMPARE(u.tags, QStringList({"urgent"}));
        QCOMPARE(u.manualTags, QStringList({"urgent"}));
    }

    void invalidManualTagRejected()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x"}));
        f->store->setManualTags(s.id, {"keep"});
        NOITE_EXPECT_ERROR(ErrorCode::InvalidTag, f->store->setManualTags(s.id, {"ok", "two words"}));
        QCOMPARE(f->store->get(s.id).manualTags, QStringList({"keep"}));
    }

    void dueAndColorRoundTrip()
    {
        auto f = fixture();
        const Scratch s = f->store->createScratch(html({"x"}));
        f->store->setDue(s.id, QDate(2026, 10, 20));
        f->store->setColor(s.id, Color::Blue);
        Scratch g = f->store->get(s.id);
        QVERIFY(g.due == QDate(2026, 10, 20));
        QVERIFY(g.color == Color::Blue);
        f->store->setDue(s.id, std::nullopt);
        f->store->setColor(s.id, std::nullopt);
        g = f->store->get(s.id);
        QVERIFY(!g.due);
        QVERIFY(!g.color);
    }

    void discardIfEmptyRules()
    {
        auto f = fixture();
        const Scratch empty = f->store->createScratch(html({""}));
        QVERIFY(f->store->discardIfEmpty(empty.id));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->get(empty.id));

        const Scratch text = f->store->createScratch(html({"x"}));
        QVERIFY(!f->store->discardIfEmpty(text.id));
        QCOMPARE(f->store->get(text.id).title, QString("x"));

        const Scratch image = f->store->createScratch(htmlWithImage("", fakeAttachmentId()));
        QVERIFY(!f->store->discardIfEmpty(image.id));
    }

    void missingIdIsNotFound()
    {
        auto f = fixture();
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->get("nope"));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->updateContent("nope", html({"x"})));
        NOITE_EXPECT_ERROR(ErrorCode::NotFound, f->store->setColor("nope", Color::Red));
    }

    void persistsAcrossReopen()
    {
        auto f = fixture();
        const QString content = html({"Keep me", "second line #tag"});
        const Scratch s = f->store->createScratch(content);
        f->store->setManualTags(s.id, {"m"});
        reopen(*f);
        const Scratch g = f->store->get(s.id);
        QCOMPARE(g.html, content);
        QCOMPARE(g.title, QString("Keep me"));
        QCOMPARE(g.tags, QStringList({"m", "tag"}));
    }
};

NOITE_TEST(ScratchTest)
#include "tst_scratch.moc"
