#include "helpers.h"
#include "registry.h"

#include <noite/content.h>

#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

using namespace noite;

namespace {

const QString kA = QString(64, 'a') + ".png";
const QString kB = QString(64, 'b') + ".pdf";

QString tags(const QString& text)
{
    return content::inlineTags(text).join(',');
}

} // namespace

class ContentTest : public QObject {
    Q_OBJECT

private slots:
    void plainTextFromQtHtml()
    {
        QTextDocument doc;
        QTextCursor c(&doc);
        QTextBlockFormat heading;
        heading.setHeadingLevel(1);
        c.setBlockFormat(heading);
        c.insertText("Title");
        c.insertBlock(QTextBlockFormat());
        c.insertText(QString("a") + QChar(QChar::LineSeparator) + "b");
        c.insertBlock(QTextBlockFormat());
        c.createList(QTextListFormat::ListDisc); // insertList() would add a second, empty block
        QTextBlockFormat task = c.blockFormat();
        task.setMarker(QTextBlockFormat::MarkerType::Unchecked);
        c.setBlockFormat(task);
        c.insertText("x");
        QCOMPARE(content::plainText(doc.toHtml()), QString("Title\na\nb\nx"));
    }

    void plainTextDropsImages()
    {
        QCOMPARE(content::plainText("<p>pic<img src=\"noite-att:" + kA + "\"/> end</p>"), QString("pic end"));
    }

    void titleFirstNonEmptyLine()
    {
        QCOMPARE(content::deriveTitle("\n\n  Buy milk  \nmore"), QString("Buy milk"));
        QCOMPARE(content::deriveTitle(""), QString());
    }

    void titleTruncatesByGrapheme()
    {
        QCOMPARE(content::deriveTitle(QString(70, QChar(0x00E9))), QString(60, QChar(0x00E9)));
        const QString family = QString::fromUtf8("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
        QCOMPARE(content::deriveTitle(family.repeated(61)), family.repeated(60));
    }

    void inlineTagRules()
    {
        QCOMPARE(tags("buy milk #home #Work"), QString("home,work"));
        QCOMPARE(tags(QString::fromUtf8("#reunião amanhã")), QString::fromUtf8("reunião"));
        QCOMPARE(tags("(#todo) #a-b_c-"), QString("a-b_c,todo"));
        QCOMPARE(tags("see https://x.com/#section, C# and issue #12, a#b"), QString());
    }

    void normalizeTagRules()
    {
        QCOMPARE(content::normalizeTag("#Urgent"), std::optional<QString>("urgent"));
        QCOMPARE(content::normalizeTag("urgent"), std::optional<QString>("urgent"));
        for (const char* bad : {"two words", "", "#12", "#"})
            QVERIFY2(!content::normalizeTag(bad), bad);
    }

    void attachmentRefsAndFirstImage()
    {
        const QString html = "<p><img src=\"noite-att:" + kA + "\"/> text <a href=\"noite-att:" + kB
            + "\">file</a><img src=\"noite-att:" + kA + "\"/></p>";
        QCOMPARE(content::attachmentRefs(html), QStringList({kA, kB}));
        QCOMPARE(content::firstImageRef(html), std::optional<QString>(kA));
        QVERIFY(!content::firstImageRef("<p>no images</p>"));
        QVERIFY(content::attachmentRefs("<img src=\"noite-att://" + kA + "\"/>").isEmpty());
    }

    void emptiness()
    {
        QVERIFY(content::isEmpty("<p></p>"));
        QVERIFY(content::isEmpty("<p>  </p>"));
        QVERIFY(content::isEmpty(QTextDocument().toHtml()));
        QVERIFY(!content::isEmpty("<p><img src=\"noite-att:" + kA + "\"/></p>"));
        QVERIFY(!content::isEmpty("<p>x</p>"));
    }

    void previewIsFirst200Graphemes()
    {
        QCOMPARE(content::preview(QString(250, 'x')), QString(200, 'x'));
        QCOMPARE(content::preview("short"), QString("short"));
    }

    void attachmentIdValidation()
    {
        QVERIFY(content::isValidAttachmentId(kA));
        for (const QString& bad : {QString("../noite.db"), QString("abc.png"), QString(), QString(64, 'a') + ".PNG",
                                   QString(64, 'a') + ".toolongext", QString(64, 'g') + ".png"})
            QVERIFY2(!content::isValidAttachmentId(bad), qPrintable(bad));
    }
};

NOITE_TEST(ContentTest)
#include "tst_content.moc"
