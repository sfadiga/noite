#include "registry.h"

#include "capture/TagField.h"

#include <QAbstractItemModel>
#include <QCompleter>
#include <QTest>

class TagFieldTest : public QObject {
    Q_OBJECT

private slots:
    // Review Focus 3.
    void tagFieldNormalizesAndRejects()
    {
        TagField field;
        field.setText("#Trip, work  #Trip");
        QCOMPARE(field.parsedTags(), std::optional<QStringList>(QStringList({"trip", "work"})));
        field.setText("#1abc ok");
        QString invalid;
        QCOMPARE(field.parsedTags(&invalid), std::optional<QStringList>());
        QCOMPARE(invalid, QString("1abc"));
        field.setText("work #"); // a lone '#' while typing is not an error
        QCOMPARE(field.parsedTags(), std::optional<QStringList>(QStringList({"work"})));
        field.setText("");
        QCOMPARE(field.parsedTags(), std::optional<QStringList>(QStringList()));
    }

    void setTagsShowsHashes()
    {
        TagField field;
        field.setTags({"trip", "work"});
        QCOMPARE(field.text(), QString("#trip #work"));
    }

    void completesWordUnderCursor()
    {
        TagField field;
        field.setCompletions({"travel", "trip"});
        field.show();
        QTest::keyClicks(&field, "tr");
        QCOMPARE(field.completer()->completionModel()->rowCount(), 2);
        field.clear();
        QTest::keyClicks(&field, "home #tra");
        QCOMPARE(field.completer()->completionModel()->rowCount(), 1);
        field.insertCompletion("travel");
        QCOMPARE(field.text(), QString("home #travel "));
    }
};

NOITE_TEST(TagFieldTest)
#include "tst_tag_field.moc"
