// Spike capability tests: can QTextEdit deliver the D7/D21 editor?
#include "NoteEdit.h"

#include <QElapsedTimer>
#include <QMimeData>
#include <QStyleFactory>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

#include <memory>

namespace {

QTextFragment findFragment(QTextDocument* doc, const QString& text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().text() == text)
                return it.fragment();
    return {};
}

QTextBlock findBlock(QTextDocument* doc, const QString& text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.text() == text)
            return b;
    return {};
}

QStringList imageNames(QTextDocument* doc)
{
    QStringList names;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat())
                names << it.fragment().charFormat().toImageFormat().name();
    return names;
}

bool hasTable(QTextDocument* doc)
{
    for (QTextFrame* f : doc->rootFrame()->childFrames())
        if (qobject_cast<QTextTable*>(f))
            return true;
    return false;
}

bool hasList(QTextDocument* doc)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.textList())
            return true;
    return false;
}

QString withoutObjects(QString s)
{
    return s.remove(QChar::ObjectReplacementCharacter).trimmed();
}

QImage testImage()
{
    QImage img(32, 32, QImage::Format_ARGB32);
    img.fill(Qt::red);
    return img;
}

} // namespace

class EditorTest : public QObject {
    Q_OBJECT

    std::unique_ptr<NoteEdit> edit;
    QTextDocument* doc() { return edit->document(); }

    void paste(const QImage& img)
    {
        QMimeData mime;
        mime.setImageData(img);
        edit->insertFromMimeData(&mime);
    }

    void pasteHtml(const QString& html)
    {
        QMimeData mime;
        mime.setHtml(html);
        edit->insertFromMimeData(&mime);
    }

private slots:
    void init()
    {
        edit = std::make_unique<NoteEdit>();
        edit->resize(480, 360);
        edit->show();
        QVERIFY(QTest::qWaitForWindowExposed(edit.get()));
        edit->setFocus();
    }

    void cleanup() { edit.reset(); }

    void headingShortcut()
    {
        QTest::keyClicks(edit.get(), "# Title");
        const QTextBlock block = doc()->firstBlock();
        QCOMPARE(block.blockFormat().headingLevel(), 1);
        QCOMPARE(block.text(), QString("Title"));
    }

    void bulletAndNumbered()
    {
        QTest::keyClicks(edit.get(), "- a");
        QVERIFY(doc()->firstBlock().textList());
        QCOMPARE(doc()->firstBlock().textList()->format().style(), QTextListFormat::ListDisc);
        QCOMPARE(doc()->firstBlock().text(), QString("a"));

        edit->clear();
        QTest::keyClicks(edit.get(), "1. b");
        QVERIFY(doc()->firstBlock().textList());
        QCOMPARE(doc()->firstBlock().textList()->format().style(), QTextListFormat::ListDecimal);
        QCOMPARE(doc()->firstBlock().text(), QString("b"));
    }

    void checkboxShortcut()
    {
        QTest::keyClicks(edit.get(), "- [ ] task");
        QTextBlock block = doc()->firstBlock();
        QCOMPARE(block.text(), QString("task"));
        QCOMPARE(block.blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);

        const QRect start = edit->cursorRect(QTextCursor(block));
        QVERIFY2(start.left() > 12, "list indent leaves no room for a marker");
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, {}, QPoint(start.left() - 8, start.center().y()));
        QCOMPARE(doc()->firstBlock().blockFormat().marker(), QTextBlockFormat::MarkerType::Checked);
    }

    // The user types "[]" without the inner space (as in Notion); also "[]" with no "- " first.
    void checkboxShortcutVariants_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<bool>("checked");
        QTest::newRow("- [] ") << "- [] task" << false;
        QTest::newRow("[] at block start") << "[] task" << false;
        QTest::newRow("[ ] at block start") << "[ ] task" << false;
        QTest::newRow("- [X] ") << "- [X] task" << true;
    }

    void checkboxShortcutVariants()
    {
        QFETCH(QString, typed);
        QFETCH(bool, checked);
        QTest::keyClicks(edit.get(), typed);
        const QTextBlock block = doc()->firstBlock();
        QCOMPARE(block.text(), QString("task"));
        QVERIFY(block.textList());
        QCOMPARE(block.blockFormat().marker(),
                 checked ? QTextBlockFormat::MarkerType::Checked : QTextBlockFormat::MarkerType::Unchecked);
    }

    // Investigation: does the checkbox marker actually get drawn? Saves one image per style.
    void checkboxRenders_data()
    {
        QTest::addColumn<QString>("style");
        QTest::newRow("fusion") << "fusion";
        QTest::newRow("windows11") << "windows11";
    }

    void checkboxRenders()
    {
        QFETCH(QString, style);
        QStyle* s = QStyleFactory::create(style);
        if (!s)
            QSKIP(qPrintable("style not available: " + style));
        edit->setStyle(s);
        QTest::keyClicks(edit.get(), "- [ ] task");
        QTest::keyClick(edit.get(), Qt::Key_Return);
        QTest::keyClicks(edit.get(), "[x] done");
        QTest::keyClick(edit.get(), Qt::Key_Return);
        QTest::keyClicks(edit.get(), "plain bullet");
        QCoreApplication::processEvents();
        const QString file = QCoreApplication::applicationDirPath() + "/checkbox-" + style + ".png";
        QVERIFY(edit->grab().save(file));
        qInfo().noquote() << "saved" << file;
    }

    void boldItalicCode()
    {
        QTest::keyClicks(edit.get(), "**b** *i* `c`");
        QCOMPARE(doc()->toPlainText(), QString("b i c"));
        QCOMPARE(findFragment(doc(), "b").charFormat().fontWeight(), int(QFont::Bold));
        QVERIFY(findFragment(doc(), "i").charFormat().fontItalic());
        QVERIFY(findFragment(doc(), "c").charFormat().fontFixedPitch());
        QVERIFY(findFragment(doc(), " ").charFormat().fontWeight() != QFont::Bold);
    }

    void linkShortcut()
    {
        QTest::keyClicks(edit.get(), "[site](https://example.com)");
        QCOMPARE(doc()->toPlainText(), QString("site"));
        const QTextCharFormat f = findFragment(doc(), "site").charFormat();
        QVERIFY(f.isAnchor());
        QCOMPARE(f.anchorHref(), QString("https://example.com"));
    }

    void pasteBareUrlBecomesLink()
    {
        QMimeData mime;
        mime.setText("https://example.com/a?b=1");
        edit->insertFromMimeData(&mime);
        QCOMPARE(doc()->toPlainText(), QString("https://example.com/a?b=1"));
        QCOMPARE(findFragment(doc(), "https://example.com/a?b=1").charFormat().anchorHref(),
                 QString("https://example.com/a?b=1"));
    }

    void typedBareUrlBecomesLink()
    {
        QTest::keyClicks(edit.get(), "see https://example.com next");
        QCOMPARE(doc()->toPlainText(), QString("see https://example.com next"));
        QCOMPARE(findFragment(doc(), "https://example.com").charFormat().anchorHref(), QString("https://example.com"));
        QVERIFY(!findFragment(doc(), " next").charFormat().isAnchor());
    }

    void pasteMarkdownIsRendered()
    {
        QMimeData mime;
        mime.setText("# Title\n\n- [ ] task\n\n**bold** text");
        edit->insertFromMimeData(&mime);
        QCOMPARE(findBlock(doc(), "Title").blockFormat().headingLevel(), 1);
        QCOMPARE(findBlock(doc(), "task").blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
        QCOMPARE(findFragment(doc(), "bold").charFormat().fontWeight(), int(QFont::Bold));
    }

    void pastePlainTextUnchanged()
    {
        QMimeData mime;
        mime.setText("line one\nline two");
        edit->insertFromMimeData(&mime);
        QCOMPARE(doc()->blockCount(), 2);
        QCOMPARE(doc()->toPlainText(), QString("line one\nline two"));
    }

    void pasteBrowserHtml()
    {
        pasteHtml("<html><body><!--StartFragment--><p><b>bold</b> <a href='https://x.y'>l</a></p>"
                  "<ul><li>i</li></ul><table><tr><td>c</td></tr></table><!--EndFragment--></body></html>");
        QCOMPARE(findFragment(doc(), "bold").charFormat().fontWeight(), int(QFont::Bold));
        QCOMPARE(findFragment(doc(), "l").charFormat().anchorHref(), QString("https://x.y"));
        QVERIFY(hasList(doc()));
        QVERIFY(hasTable(doc()));
    }

    void pasteImage()
    {
        paste(testImage());
        const QStringList names = imageNames(doc());
        QCOMPARE(names.size(), 1);
        QVERIFY(names[0].startsWith("noite-att://"));
        const QImage stored = doc()->resource(QTextDocument::ImageResource, QUrl(names[0])).value<QImage>();
        QCOMPARE(stored.size(), QSize(32, 32));
    }

    void insertTable()
    {
        QTextTable* table = edit->textCursor().insertTable(2, 2);
        table->cellAt(1, 1).firstCursorPosition().insertText("x");
        QCOMPARE(table->rows(), 2);
        QCOMPARE(table->columns(), 2);
        QTextCursor cell = table->cellAt(1, 1).firstCursorPosition();
        cell.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QCOMPARE(cell.selectedText(), QString("x"));
    }

    void splitSelection()
    {
        edit->setPlainText("keep\nmove me");
        edit->moveCursor(QTextCursor::End);
        paste(testImage());

        QTextCursor sel(doc());
        sel.setPosition(doc()->findBlockByNumber(1).position());
        sel.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        edit->setTextCursor(sel);

        std::unique_ptr<QTextDocument> moved = edit->takeSelectionAsDocument();
        QVERIFY(moved);
        QCOMPARE(withoutObjects(moved->toPlainText()), QString("move me"));
        const QStringList names = imageNames(moved.get());
        QCOMPARE(names.size(), 1);
        QVERIFY(!moved->resource(QTextDocument::ImageResource, QUrl(names[0])).value<QImage>().isNull());
        QCOMPARE(doc()->toPlainText(), QString("keep"));
    }

    void htmlRoundTrip()
    {
        QTextCursor c(doc());
        QTextBlockFormat heading;
        heading.setHeadingLevel(1);
        c.setBlockFormat(heading);
        c.insertText("Head");

        c.insertBlock(QTextBlockFormat());
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        c.insertText("bold", bold);

        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        QTextCharFormat link;
        link.setAnchor(true);
        link.setAnchorHref("https://example.com");
        c.insertText("site", link);

        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        c.insertList(QTextListFormat::ListDisc);
        QTextBlockFormat task = c.blockFormat();
        task.setMarker(QTextBlockFormat::MarkerType::Unchecked);
        c.setBlockFormat(task);
        c.insertText("task");

        c.insertBlock(QTextBlockFormat(), QTextCharFormat());
        c.insertTable(1, 1)->cellAt(0, 0).firstCursorPosition().insertText("cell");

        edit->moveCursor(QTextCursor::End);
        paste(testImage());
        const QString image = imageNames(doc()).value(0);

        auto survives = [&](QTextDocument* d) {
            QStringList ok;
            if (findBlock(d, "Head").blockFormat().headingLevel() == 1) ok << "heading";
            if (findFragment(d, "bold").charFormat().fontWeight() == QFont::Bold) ok << "bold";
            if (findFragment(d, "site").charFormat().anchorHref() == "https://example.com") ok << "link";
            if (findBlock(d, "task").blockFormat().marker() == QTextBlockFormat::MarkerType::Unchecked) ok << "checkbox";
            if (hasTable(d)) ok << "table";
            if (imageNames(d).contains(image)) ok << "image";
            return ok;
        };
        const QStringList all = {"heading", "bold", "link", "checkbox", "table", "image"};

        QTextDocument fromMarkdown;
        fromMarkdown.setMarkdown(doc()->toMarkdown());
        QStringList lost = all;
        for (const QString& s : survives(&fromMarkdown)) lost.removeAll(s);
        qInfo().noquote() << "toMarkdown() loses:" << (lost.isEmpty() ? QString("nothing") : lost.join(", "));

        QTextDocument fromHtml;
        fromHtml.setHtml(doc()->toHtml());
        QCOMPARE(survives(&fromHtml), all);
    }

    void hugePasteStaysFast()
    {
        QString html;
        for (int i = 0; i < 5000; ++i)
            html += QString("<p>line %1 with <b>bold</b> and <a href='https://e.x/%1'>a link</a></p>").arg(i);
        QElapsedTimer timer;
        timer.start();
        pasteHtml(html);
        const qint64 ms = timer.elapsed();
        qInfo().noquote() << "5000-paragraph paste:" << ms << "ms";
        QCOMPARE(doc()->blockCount(), 5000);
        QVERIFY2(ms < 500, qPrintable(QString("paste took %1 ms").arg(ms)));
    }
};

QTEST_MAIN(EditorTest)
#include "tst_editor.moc"
