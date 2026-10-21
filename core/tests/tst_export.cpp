#include "helpers.h"
#include "registry.h"

#include "store_internal.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

using namespace noite;
using namespace noite::test;

namespace {

QByteArray png(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Every element the editor makes: heading, bold, link, two checkboxes, a table, an image.
QString richNote(const QString& imageId)
{
    return "<h1>Head</h1>"
           "<p><b>bold</b> <a href=\"https://example.com\">site</a></p>"
           "<ul><li class=\"unchecked\">todo</li><li class=\"checked\">did</li></ul>"
           "<table><tr><td>cell</td></tr></table>"
           "<p><img src=\"noite-att:"
        + imageId + "\"/></p>";
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QTextBlock findBlock(const QTextDocument& doc, const QString& text)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        if (b.text() == text)
            return b;
    return {};
}

QTextCharFormat fragmentFormat(const QTextDocument& doc, const QString& text)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().text() == text)
                return it.fragment().charFormat();
    return {};
}

QList<QTextImageFormat> images(const QTextDocument& doc)
{
    QList<QTextImageFormat> out;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat())
                out << it.fragment().charFormat().toImageFormat();
    return out;
}

int pageCount(const QByteArray& pdf)
{
    static const QRegularExpression page("/Type\\s*/Page(?![s\\w])");
    int n = 0;
    for (auto it = page.globalMatch(QString::fromLatin1(pdf)); it.hasNext(); it.next())
        ++n;
    return n;
}

} // namespace

class ExportTest : public QObject {
    Q_OBJECT

private slots:
    void htmlRoundTripKeepsEverything()
    {
        auto f = fixture();
        const QByteArray bytes = png(40, 20);
        const QString image = f->store->putAttachment(bytes, "png");
        const QString id = f->store->createScratch(richNote(image)).id;
        const QString path = f->tmp.filePath("note.html");
        f->store->exportScratch(id, path, ExportFormat::Html);

        const QString html = QString::fromUtf8(readAll(path));
        QVERIFY(!html.contains("noite-att:"));
        QTextDocument back;
        back.setHtml(html);
        QCOMPARE(findBlock(back, "Head").blockFormat().headingLevel(), 1);
        QCOMPARE(fragmentFormat(back, "bold").fontWeight(), int(QFont::Bold));
        QCOMPARE(fragmentFormat(back, "site").anchorHref(), QString("https://example.com"));
        QCOMPARE(findBlock(back, "todo").blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
        QCOMPARE(findBlock(back, "did").blockFormat().marker(), QTextBlockFormat::MarkerType::Checked);
        bool table = false;
        for (QTextFrame* frame : back.rootFrame()->childFrames())
            table = table || qobject_cast<QTextTable*>(frame);
        QVERIFY(table);
        QCOMPARE(images(back).size(), 1);
        QVERIFY(images(back)[0].name().startsWith("data:image/png;base64,"));
    }

    // X7: the stored bytes, not a re-encode.
    void htmlEmbedsOriginalImageBytes()
    {
        auto f = fixture();
        const QByteArray bytes = png(40, 20);
        const QString id = f->store->createScratch(htmlWithImage("pic", f->store->putAttachment(bytes, "png"))).id;
        const QString path = f->tmp.filePath("pic.html");
        f->store->exportScratch(id, path, ExportFormat::Html);
        QVERIFY(readAll(path).contains("data:image/png;base64," + bytes.toBase64()));
    }

    void htmlHasTitleAndCheckboxCss()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"Groceries & more", "milk"})).id;
        const QString path = f->tmp.filePath("g.html");
        f->store->exportScratch(id, path, ExportFormat::Html);
        const QString out = QString::fromUtf8(readAll(path));
        QVERIFY2(out.contains("<title>Groceries &amp; more</title>"), qPrintable(out.left(600)));
        QVERIFY(!out.contains("<title>Groceries & more"));
        QVERIFY(out.contains("li.checked"));
        QVERIFY(out.contains("li.unchecked"));
        QVERIFY(out.contains("max-width: 100%"));
        QVERIFY(!readAll(path).startsWith("\xEF\xBB\xBF")); // no BOM
    }

    void pdfIsAPdf()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"one page"})).id;
        const QString path = f->tmp.filePath("n.pdf");
        f->store->exportScratch(id, path, ExportFormat::Pdf);
        const QByteArray pdf = readAll(path);
        QVERIFY(pdf.startsWith("%PDF-"));
        QCOMPARE(pageCount(pdf), 1);
    }

    void longNoteGivesSeveralPages()
    {
        auto f = fixture();
        QStringList lines;
        for (int i = 0; i < 200; ++i)
            lines << QString("line %1").arg(i);
        const QString id = f->store->createScratch(html(lines)).id;
        const QString path = f->tmp.filePath("long.pdf");
        f->store->exportScratch(id, path, ExportFormat::Pdf);
        QVERIFY(pageCount(readAll(path)) > 1);
    }

    void pdfKeepsLinks()
    {
        auto f = fixture();
        const QString id = f->store->createScratch("<p><a href=\"https://example.com/x\">site</a></p>").id;
        const QString path = f->tmp.filePath("link.pdf");
        f->store->exportScratch(id, path, ExportFormat::Pdf);
        const QByteArray pdf = readAll(path);
        QVERIFY(pdf.contains("/URI"));
        QVERIFY(pdf.contains("https://example.com/x"));
    }

    // X7: wider than the text, scaled down with its aspect ratio; narrower ones untouched.
    void wideImageFitsTextWidth()
    {
        auto f = fixture();
        const QString wide = f->store->putAttachment(png(2000, 100), "png");
        const QString narrow = f->store->putAttachment(png(100, 50), "png");
        const QString note = htmlWithImage("w", wide) + htmlWithImage("n", narrow);
        const auto doc = detail::exportDocument(*f->store, note, 500);
        const QList<QTextImageFormat> found = images(*doc);
        QCOMPARE(found.size(), 2);
        QCOMPARE(found[0].width(), 500.0);
        QCOMPARE(found[0].height(), 25.0);
        QCOMPARE(found[1].width(), 0.0); // intrinsic size
    }

    // X10: in both formats.
    void missingImageBecomesText()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(htmlWithImage("before", fakeAttachmentId())).id;
        const QString path = f->tmp.filePath("m.html");
        f->store->exportScratch(id, path, ExportFormat::Html);
        const QString out = QString::fromUtf8(readAll(path));
        QVERIFY(out.contains("[missing image]"));
        QVERIFY(!out.contains("noite-att:"));
        const auto doc = detail::exportDocument(*f->store, f->store->get(id).html, 500);
        QVERIFY(doc->toPlainText().contains("[missing image]"));
        QVERIFY(images(*doc).isEmpty());
        const QString pdf = f->tmp.filePath("m.pdf");
        f->store->exportScratch(id, pdf, ExportFormat::Pdf);
        QVERIFY(readAll(pdf).startsWith("%PDF-"));
    }

    void unwritablePathThrowsIoAndLeavesNothing()
    {
        auto f = fixture();
        const QString id = f->store->createScratch(html({"x"})).id;
        for (const ExportFormat format : {ExportFormat::Html, ExportFormat::Pdf}) {
            const QString path = f->tmp.filePath("no-such-folder/x.out");
            NOITE_EXPECT_ERROR(ErrorCode::Io, f->store->exportScratch(id, path, format));
            QVERIFY(!QFileInfo::exists(path));
        }
        const QString readOnly = f->tmp.filePath("ro.html");
        {
            QFile file(readOnly);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("keep");
        }
        QVERIFY(QFile::setPermissions(readOnly, QFileDevice::ReadOwner));
        NOITE_EXPECT_ERROR(ErrorCode::Io, f->store->exportScratch(id, readOnly, ExportFormat::Html));
        QCOMPARE(readAll(readOnly), QByteArray("keep"));
        QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    void unknownIdThrowsNotFound()
    {
        auto f = fixture();
        NOITE_EXPECT_ERROR(ErrorCode::NotFound,
                           f->store->exportScratch("missing", f->tmp.filePath("x.html"), ExportFormat::Html));
    }

    void lastExportDirRoundTripsAndSurvivesSettings()
    {
        auto f = fixture();
        QCOMPARE(f->store->lastExportDir(), std::optional<QString>());
        f->store->setLastExportDir("C:/Users/me/Desktop");
        f->store->saveSettings(f->store->settings());
        QCOMPARE(f->store->lastExportDir(), std::optional<QString>("C:/Users/me/Desktop"));
    }
};

NOITE_TEST(ExportTest)
#include "tst_export.moc"
