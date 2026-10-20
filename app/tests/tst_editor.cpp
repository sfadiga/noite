// Editor behaviour tests, ported from the Qt spike (spikes/qt-capture) plus M2 hardening.
#include "fake_attachments.h"
#include "registry.h"

#include "editor/NoteEdit.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QApplication>
#include <QInputMethodEvent>
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

QTextFragment fragmentContaining(QTextDocument* doc, const QString& text)
{
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().text().contains(text))
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

    std::unique_ptr<FakeAttachments> attachments;
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
        attachments = std::make_unique<FakeAttachments>();
        edit = std::make_unique<NoteEdit>();
        edit->setAttachmentStore(attachments.get());
        edit->resize(480, 360);
        edit->show();
        QVERIFY(QTest::qWaitForWindowExposed(edit.get()));
        edit->setFocus();
    }

    void cleanup()
    {
        edit.reset();
        attachments.reset();
    }

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

    // ---- M2 hardening (Qt spike review's deferred list) ----

    // Dictation, IME and the emoji panel commit text through inputMethodEvent, not keyPressEvent.
    void imeCommitTriggersShortcuts()
    {
        QTest::keyClicks(edit.get(), "#");
        QInputMethodEvent commit;
        commit.setCommitString(" ");
        QApplication::sendEvent(edit.get(), &commit);
        QTest::keyClicks(edit.get(), "Title");
        QCOMPARE(doc()->firstBlock().blockFormat().headingLevel(), 1);
        QCOMPARE(doc()->firstBlock().text(), QString("Title"));
    }

    // Absolute sizes break when the theme or default font changes (D30).
    void headingSizeIsRelative_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<int>("adjustment");
        QTest::newRow("h1") << "# T" << 2;
        QTest::newRow("h2") << "## T" << 1;
        QTest::newRow("h3") << "### T" << 0;
    }

    void headingSizeIsRelative()
    {
        QFETCH(QString, typed);
        QFETCH(int, adjustment);
        QTest::keyClicks(edit.get(), typed);
        const QTextCharFormat f = findFragment(doc(), "T").charFormat();
        QCOMPARE(f.property(QTextFormat::FontSizeAdjustment).toInt(), adjustment);
        QVERIFY(!f.hasProperty(QTextFormat::FontPointSize));
        QCOMPARE(f.fontWeight(), int(QFont::Bold));
    }

    // A stored link colour would be wrong in the other theme (D30).
    void linksStoreNoColour()
    {
        QTest::keyClicks(edit.get(), "[a](https://x.y) see https://typed.example ");
        QMimeData mime;
        mime.setText("https://pasted.example");
        edit->insertFromMimeData(&mime);
        for (const char* text : {"a", "https://typed.example", "https://pasted.example"}) {
            const QTextCharFormat f = findFragment(doc(), text).charFormat();
            QVERIFY2(f.isAnchor(), text);
            QVERIFY2(!f.hasProperty(QTextFormat::ForegroundBrush), text);
        }
    }

    void autolinkExcludesTrailingPunctuation()
    {
        QTest::keyClicks(edit.get(), "(see https://x.com). ");
        QCOMPARE(fragmentContaining(doc(), "https://x.com").charFormat().anchorHref(), QString("https://x.com"));
        edit->clear();
        QTest::keyClicks(edit.get(), "https://y.com, next");
        QCOMPARE(fragmentContaining(doc(), "https://y.com").charFormat().anchorHref(), QString("https://y.com"));
        QVERIFY(!fragmentContaining(doc(), "next").charFormat().isAnchor());
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

    // Placeholders such as <wrong-base> are text, not HTML tags: an unclosed tag swallowed
    // every later line of a pasted markdown note.
    void pasteMarkdownKeepsAngleBracketText()
    {
        QMimeData mime;
        mime.setText("git stash\ngit rebase --onto develop <wrong-base> <my-feature>\ngit stash pop\n\n"
                     "1. first\n2. second");
        edit->insertFromMimeData(&mime);
        const QString text = doc()->toPlainText();
        QVERIFY2(text.contains("git rebase --onto develop <wrong-base> <my-feature>"), qPrintable(text));
        QVERIFY2(text.contains("git stash pop"), qPrintable(text));
        QVERIFY2(text.contains("second"), qPrintable(text));
        QVERIFY(findBlock(doc(), "second").textList());
    }

    // Review C1: each pasted image must get its own valid resource key.
    void twoPastedImagesStayDistinct()
    {
        QImage red(8, 8, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QImage blue(8, 8, QImage::Format_ARGB32);
        blue.fill(Qt::blue);
        paste(red);
        paste(blue);
        const QStringList names = imageNames(doc());
        QCOMPARE(names.size(), 2);
        QVERIFY(names[0] != names[1]);
        for (const QString& n : names)
            QVERIFY2(QUrl(n).isValid(), qPrintable(n));
        QCOMPARE(doc()->resource(QTextDocument::ImageResource, QUrl(names[0])).value<QImage>().pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(doc()->resource(QTextDocument::ImageResource, QUrl(names[1])).value<QImage>().pixelColor(0, 0), QColor(Qt::blue));

        QTextDocument back;
        back.setHtml(doc()->toHtml());
        QCOMPARE(imageNames(&back), names);
    }

    // Review I2: emphasis markers inside words, numbers and URLs are left alone.
    void intrawordMarkersUntouched_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::newRow("snake_case") << "my_var_name ";
        QTest::newRow("multiply") << "2*3*4";
        QTest::newRow("url underscores") << "see https://x.com/a_b_c ";
        QTest::newRow("url leading underscore") << "https://x.com/_a_ ";
    }

    void intrawordMarkersUntouched()
    {
        QFETCH(QString, typed);
        QTest::keyClicks(edit.get(), typed);
        QCOMPARE(doc()->toPlainText(), typed);
        for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                QVERIFY(!it.fragment().charFormat().fontItalic());
    }

    void urlWithUnderscoresKeepsHref()
    {
        QTest::keyClicks(edit.get(), "see https://x.com/a_b_c ");
        QCOMPARE(findFragment(doc(), "https://x.com/a_b_c").charFormat().anchorHref(), QString("https://x.com/a_b_c"));
    }

    // Review I3: a single markdown-looking line doesn't reflow ordinary text.
    void pasteTextWithOneMarkdownLineStaysPlain()
    {
        QMimeData mime;
        mime.setText("Hi team,\nplease see the list:\n- item one\nthanks\nBob");
        edit->insertFromMimeData(&mime);
        QCOMPARE(doc()->toPlainText(), QString("Hi team,\nplease see the list:\n- item one\nthanks\nBob"));
    }

    void pasteMarkdownKeepsLineBreaks()
    {
        QMimeData mime;
        mime.setText("# Notes\nfirst line\nsecond line\n- a\n- b");
        edit->insertFromMimeData(&mime);
        QCOMPARE(findBlock(doc(), "Notes").blockFormat().headingLevel(), 1);
        QVERIFY(findBlock(doc(), "first line").isValid());
        QVERIFY(findBlock(doc(), "second line").isValid());
        QVERIFY(findBlock(doc(), "a").textList());
        QVERIFY(findBlock(doc(), "b").textList());
    }

    void ctrlShiftVPastesPlainText()
    {
        QGuiApplication::clipboard()->setText("**bold** text");
        QTest::keyClick(edit.get(), Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(doc()->toPlainText(), QString("**bold** text"));
        QVERIFY(findFragment(doc(), "**bold** text").charFormat().fontWeight() != QFont::Bold);
    }

    // Review I4: spreadsheet-style clipboards carry a bitmap too; the table wins.
    void htmlTableBeatsImage()
    {
        QMimeData mime;
        mime.setImageData(testImage());
        mime.setHtml("<table><tr><td>a</td><td>b</td></tr></table>");
        edit->insertFromMimeData(&mime);
        QVERIFY(hasTable(doc()));
        QVERIFY(imageNames(doc()).isEmpty());
    }

    void imageOnlyHtmlStillPastesImage()
    {
        QMimeData mime;
        mime.setImageData(testImage());
        mime.setHtml("<html><body><img src='file:///C:/tmp/x.png'></body></html>");
        edit->insertFromMimeData(&mime);
        const QStringList names = imageNames(doc());
        QCOMPARE(names.size(), 1);
        QVERIFY(names[0].startsWith("noite-att:"));
    }

    // Review I5: Ctrl+click only opens web and mail links.
    void onlySafeLinkSchemesOpen()
    {
        QVERIFY(NoteEdit::isSafeLink(QUrl("https://example.com")));
        QVERIFY(NoteEdit::isSafeLink(QUrl("http://example.com")));
        QVERIFY(NoteEdit::isSafeLink(QUrl("mailto:a@b.c")));
        QVERIFY(!NoteEdit::isSafeLink(QUrl("file:///C:/Windows/System32/calc.exe")));
        QVERIFY(!NoteEdit::isSafeLink(QUrl("javascript:alert(1)")));
        QVERIFY(!NoteEdit::isSafeLink(QUrl("ms-settings:")));
        QVERIFY(!NoteEdit::isSafeLink(QUrl("")));
    }

    // Review I8: typing after a pasted URL is not part of the link.
    void pastedUrlFormatDoesNotStick()
    {
        QMimeData mime;
        mime.setText("https://example.com");
        edit->insertFromMimeData(&mime);
        QTest::keyClicks(edit.get(), " tail");
        const QTextFragment tail = fragmentContaining(doc(), "tail");
        QVERIFY(tail.isValid());
        QVERIFY2(!tail.charFormat().isAnchor(), qPrintable("anchored fragment: " + tail.text()));
        QCOMPARE(findFragment(doc(), "https://example.com").charFormat().anchorHref(), QString("https://example.com"));
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
        QVERIFY(names[0].startsWith("noite-att:"));
        QVERIFY(QUrl(names[0]).isValid());
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
#ifdef QT_DEBUG
        QSKIP("timing is judged on Release builds only (Debug time logged above)");
#endif
        QVERIFY2(ms < 500, qPrintable(QString("paste took %1 ms").arg(ms)));
    }
};

NOITE_TEST(EditorTest)
#include "tst_editor.moc"
