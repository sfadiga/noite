#include "registry.h"

#include "editor/DateTokens.h"
#include "editor/NoteEdit.h"

#include <QSignalSpy>
#include <QTest>

namespace {

const QDate kThursday(2026, 10, 15);

std::optional<QDate> parse(const char* token)
{
    return parseDateToken(QString::fromUtf8(token), kThursday);
}

} // namespace

class DatesTest : public QObject {
    Q_OBJECT

private slots:
    void wordsAndWeekdays()
    {
        QCOMPARE(kThursday.dayOfWeek(), 4);
        QVERIFY(parse("today") == kThursday);
        QVERIFY(parse("hoje") == kThursday);
        QVERIFY(parse("Tomorrow") == QDate(2026, 10, 16));
        QVERIFY(parse("amanhã") == QDate(2026, 10, 16));
        QVERIFY(parse("amanha") == QDate(2026, 10, 16));
        QVERIFY(parse("fri") == QDate(2026, 10, 16));
        QVERIFY(parse("sex") == QDate(2026, 10, 16));
        QVERIFY(parse("thu") == kThursday); // a weekday includes today
        QVERIFY(parse("qui") == kThursday);
        QVERIFY(parse("sáb") == QDate(2026, 10, 17));
        QVERIFY(parse("sab") == QDate(2026, 10, 17));
        QVERIFY(parse("mon") == QDate(2026, 10, 19));
        QVERIFY(parse("dom") == QDate(2026, 10, 18));
        QVERIFY(!parse("someone"));
        QVERIFY(!parse(""));
    }

    void numericForms()
    {
        QVERIFY(parse("2026-12-01") == QDate(2026, 12, 1));
        QVERIFY(parse("20/10") == QDate(2026, 10, 20));
        QVERIFY(parse("15/10") == kThursday);
        QVERIFY(parse("01/02") == QDate(2027, 2, 1)); // already past this year -> next year
        QVERIFY(parse("20/10/2027") == QDate(2027, 10, 20));
        QVERIFY(!parse("31/02"));
        QVERIFY(!parse("2026-13-01"));
        QVERIFY(!parse("1/2/3"));
    }

    void typingTokenSetsDueAndRemovesText()
    {
        NoteEdit edit;
        edit.setToday([] { return kThursday; });
        QSignalSpy spy(&edit, &NoteEdit::dueDateTyped);
        QTest::keyClicks(&edit, "call bob @tomorrow x");
        QCOMPARE(edit.toPlainText(), QString("call bob x"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toDate(), QDate(2026, 10, 16));
    }

    void tokenAtStartOfLine()
    {
        NoteEdit edit;
        edit.setToday([] { return kThursday; });
        QSignalSpy spy(&edit, &NoteEdit::dueDateTyped);
        QTest::keyClicks(&edit, "@20/10 pay rent");
        QCOMPARE(edit.toPlainText(), QString("pay rent"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy[0][0].toDate(), QDate(2026, 10, 20));
    }

    void unknownTokenStaysText()
    {
        NoteEdit edit;
        QSignalSpy spy(&edit, &NoteEdit::dueDateTyped);
        QTest::keyClicks(&edit, "ask @someone now");
        QCOMPARE(edit.toPlainText(), QString("ask @someone now"));
        QCOMPARE(spy.count(), 0);
    }

    void emailIsNotAToken()
    {
        NoteEdit edit;
        edit.setToday([] { return kThursday; });
        QSignalSpy spy(&edit, &NoteEdit::dueDateTyped);
        QTest::keyClicks(&edit, "mail a@fri.com now");
        QCOMPARE(edit.toPlainText(), QString("mail a@fri.com now"));
        QCOMPARE(spy.count(), 0);
    }
};

NOITE_TEST(DatesTest)
#include "tst_dates.moc"
