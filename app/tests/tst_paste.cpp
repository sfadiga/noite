#include "fake_attachments.h"
#include "registry.h"

#include "editor/NoteEdit.h"
#include "editor/OpenFolder.h"

#include <noite/store.h>

#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

#include <memory>

namespace {

QList<QTextFragment> fragments(QTextDocument* doc)
{
    QList<QTextFragment> out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it)
            out << it.fragment();
    return out;
}

QTextFragment fragmentContaining(QTextDocument* doc, const QString& text)
{
    for (const QTextFragment& f : fragments(doc))
        if (f.text().contains(text))
            return f;
    return {};
}

QStringList imageNames(QTextDocument* doc)
{
    QStringList names;
    for (const QTextFragment& f : fragments(doc))
        if (f.charFormat().isImageFormat())
            names << f.charFormat().toImageFormat().name();
    return names;
}

// A well-formed attachment id that nothing stores.
QString fakeId()
{
    return QString(64, 'a') + ".pdf";
}

QByteArray encoded(const QColor& color, const char* format)
{
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, format);
    return bytes;
}

} // namespace

class PasteTest : public QObject {
    Q_OBJECT

public slots:
    // Stands in for the shell when a test Ctrl+clicks a chip.
    void handleUrl(const QUrl& url) { opened_ = url; }

private:
    QUrl opened_;

    std::unique_ptr<FakeAttachments> store;
    std::unique_ptr<NoteEdit> edit;
    QTextDocument* doc() { return edit->document(); }

    void paste(const QMimeData& mime) { edit->insertFromMimeData(&mime); }
    void pasteHtml(const QString& html)
    {
        QMimeData mime;
        mime.setHtml(html);
        paste(mime);
    }

private slots:
    void init()
    {
        store = std::make_unique<FakeAttachments>();
        edit = std::make_unique<NoteEdit>();
        edit->setAttachmentStore(store.get());
    }

    void cleanup()
    {
        edit.reset();
        store.reset();
    }

    void pastedImageBecomesStoredAttachment()
    {
        QMimeData mime;
        mime.setImageData(QImage::fromData(encoded(Qt::red, "PNG")));
        paste(mime);
        QCOMPARE(store->ids().size(), 1);
        QCOMPARE(imageNames(doc()), QStringList({"noite-att:" + store->ids()[0]}));
    }

    void pastedJpegBytesKeptAsIs()
    {
        const QByteArray jpeg = encoded(Qt::blue, "JPG");
        QMimeData mime;
        mime.setImageData(QImage::fromData(jpeg));
        mime.setData("image/jpeg", jpeg);
        paste(mime);
        QCOMPARE(store->ids().size(), 1);
        QVERIFY(store->ids()[0].endsWith(".jpg"));
        QCOMPARE(store->bytes(store->ids()[0]), jpeg);
    }

    void reloadedNoteShowsImage()
    {
        const QString id = store->put(encoded(Qt::green, "PNG"), "png");
        edit->setHtml("<p><img src=\"noite-att:" + id + "\"/></p>");
        const QImage img = doc()->resource(QTextDocument::ImageResource, QUrl("noite-att:" + id)).value<QImage>();
        QVERIFY(!img.isNull());
        QCOMPARE(img.pixelColor(0, 0), QColor(Qt::green));
    }

    // Review Focus 1: a reference whose bytes aren't stored shows as a broken image, no throw.
    void missingBlobGivesNullImage()
    {
        edit->setHtml("<p><img src=\"noite-att:" + fakeId() + "\"/></p>");
        const QVariant res = doc()->resource(QTextDocument::ImageResource, QUrl("noite-att:" + fakeId()));
        QVERIFY(res.value<QImage>().isNull());
    }

    void htmlPasteStripsColoursFontsSizes()
    {
        pasteHtml("<p><span style=\"color:#000000; background-color:#ffffff; font-family:'Times New Roman'; "
                  "font-size:18pt\"><b>bold</b> plain</span> <font color=\"red\" face=\"Arial\">old</font> "
                  "<a href=\"https://x.y\">l</a></p><ul><li>i</li></ul><table><tr><td>c</td></tr></table>");
        for (const QTextFragment& f : fragments(doc())) {
            const QTextCharFormat c = f.charFormat();
            QVERIFY2(!c.hasProperty(QTextFormat::ForegroundBrush), qPrintable(f.text()));
            QVERIFY2(!c.hasProperty(QTextFormat::BackgroundBrush), qPrintable(f.text()));
            QVERIFY2(!c.hasProperty(QTextFormat::FontFamilies), qPrintable(f.text()));
            QVERIFY2(!c.hasProperty(QTextFormat::FontPointSize), qPrintable(f.text()));
        }
        QCOMPARE(fragmentContaining(doc(), "bold").charFormat().fontWeight(), int(QFont::Bold));
        QTextFragment link;
        for (const QTextFragment& f : fragments(doc()))
            if (f.text() == "l")
                link = f;
        QCOMPARE(link.charFormat().anchorHref(), QString("https://x.y"));
        bool list = false;
        for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next())
            list = list || b.textList();
        QVERIFY(list);
        bool table = false;
        for (QTextFrame* fr : doc()->rootFrame()->childFrames())
            table = table || qobject_cast<QTextTable*>(fr);
        QVERIFY(table);
    }

    void htmlPasteCapturesDataUriImage()
    {
        const QByteArray png = encoded(Qt::red, "PNG");
        pasteHtml("<p>x<img src=\"data:image/png;base64," + QString::fromLatin1(png.toBase64()) + "\"/></p>");
        QCOMPARE(store->ids().size(), 1);
        QVERIFY(store->ids()[0].endsWith(".png"));
        QCOMPARE(imageNames(doc()), QStringList({"noite-att:" + store->ids()[0]}));
    }

    void htmlPasteCapturesLocalFileImage()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("x.png");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(encoded(Qt::red, "PNG"));
        file.close();
        pasteHtml("<p><img src=\"" + QUrl::fromLocalFile(path).toString() + "\"/></p>");
        QCOMPARE(store->ids().size(), 1);
        QCOMPARE(imageNames(doc()), QStringList({"noite-att:" + store->ids()[0]}));
    }

    // noite never fetches from the network (D13): a remote image becomes a link to it.
    void htmlPasteRemoteImageBecomesLink()
    {
        pasteHtml("<p>a<img src=\"https://e.x/cat.png\"/>b</p>");
        QVERIFY(imageNames(doc()).isEmpty());
        QVERIFY(store->ids().isEmpty());
        const QTextFragment link = fragmentContaining(doc(), "cat.png");
        QVERIFY(link.isValid());
        QCOMPARE(link.charFormat().anchorHref(), QString("https://e.x/cat.png"));
    }

    void copiedFileBecomesChip()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("report.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7 test");
        file.close();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        paste(mime);
        QCOMPARE(store->ids().size(), 1);
        const QTextFragment chip = fragmentContaining(doc(), "report.pdf");
        QCOMPARE(chip.charFormat().anchorHref(), "noite-att:" + store->ids()[0]);
    }

    void chipOpensUnderItsName()
    {
        QTemporaryDir tmp;
        edit->setOpenFolder(tmp.path());
        const QString pdf = store->put("pdf bytes", "pdf");
        const QFileInfo opened(edit->openTargetFor(pdf, "Report Q3.pdf").toLocalFile());
        QCOMPARE(opened.fileName(), QString("Report Q3.pdf"));
        QCOMPARE(opened.absolutePath(), tmp.path() + "/" + pdf.left(12));
        // A chip renamed to a program name opens its folder, never the file.
        QVERIFY(QFileInfo(edit->openTargetFor(pdf, "run.exe").toLocalFile()).isDir());
    }

    // Review Focus 2.
    void chipNameIsSanitised()
    {
        const QString id = fakeId();
        QCOMPARE(safeFileName("a:b?.pdf", id), QString("a_b_.pdf"));
        QCOMPARE(safeFileName("  ", id), id);
        QCOMPARE(safeFileName("..", id), id);
        QCOMPARE(safeFileName("report. ", id), QString("report"));
        for (const QString& name : {QString("..\\x.pdf"), QString("../../x.pdf"), QString("a<b>c|d*e\"f"),
                                    QString("tab\there"), QString("C:\\Windows\\x.dll")}) {
            const QString safe = safeFileName(name, id);
            QVERIFY2(!safe.isEmpty() && safe != "." && safe != "..", qPrintable(safe));
            for (QChar c : safe)
                QVERIFY2(!QString("<>:\"/\\|?*").contains(c) && c.unicode() >= 0x20, qPrintable(safe));
            QVERIFY2(!safe.endsWith('.') && !safe.endsWith(' '), qPrintable(safe));
        }
    }

    // Review Focus 5: a PDF still open in its viewer must not be rewritten.
    void openTargetReusesExistingFile()
    {
        QTemporaryDir tmp;
        edit->setOpenFolder(tmp.path());
        const QString pdf = store->put("pdf bytes", "pdf");
        const QString first = edit->openTargetFor(pdf, "r.pdf").toLocalFile();
        QFile held(first);
        QVERIFY(held.open(QIODevice::ReadOnly)); // the viewer has it open
        const QDateTime written = QFileInfo(first).lastModified();
        QCOMPARE(edit->openTargetFor(pdf, "r.pdf").toLocalFile(), first);
        QCOMPARE(QFileInfo(first).lastModified(), written);
    }

    void ctrlClickPassesChipText()
    {
        QTemporaryDir tmp;
        edit->setOpenFolder(tmp.filePath("open"));
        const QString path = tmp.filePath("notes v2.pdf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7 chip");
        file.close();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        paste(mime);

        edit->resize(400, 200);
        edit->show();
        QVERIFY(QTest::qWaitForWindowExposed(edit.get()));
        QTextCursor cursor(doc());
        cursor.setPosition(fragmentContaining(doc(), "notes v2.pdf").position() + 3);
        const QPoint at = edit->cursorRect(cursor).center();
        opened_.clear();
        QDesktopServices::setUrlHandler("file", this, "handleUrl");
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::ControlModifier, at);
        QDesktopServices::unsetUrlHandler("file");
        QCOMPARE(QFileInfo(opened_.toLocalFile()).fileName(), QString("notes v2.pdf"));
    }

    void oversizedFileNotAttached()
    {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("big.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(noite::kMaxAttachmentBytes + 1));
        file.close();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        paste(mime);
        QVERIFY(store->ids().isEmpty());
        QCOMPARE(doc()->toPlainText(), QString("big.bin (too large to attach)"));
        QVERIFY(!fragmentContaining(doc(), "big.bin").charFormat().isAnchor());
    }

    void noiteAttLinksAreSafe()
    {
        QVERIFY(NoteEdit::isSafeLink(QUrl("noite-att:" + QString(64, 'a') + ".pdf")));
        QVERIFY(!NoteEdit::isSafeLink(QUrl("file:///C:/x.exe")));
    }

    // A5: the bytes are written to the open folder to be opened there. Opening an attached
    // program would run it: show its folder instead.
    void openTargetWritesTempFile()
    {
        QTemporaryDir tmp;
        edit->setOpenFolder(tmp.path());
        const QString pdf = store->put("pdf bytes", "pdf");
        const QString exe = store->put("exe bytes", "exe");

        const QString file = edit->openTargetFor(pdf).toLocalFile();
        QVERIFY2(file.startsWith(tmp.path() + "/"), qPrintable(file));
        QFile written(file);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("pdf bytes"));

        const QString folder = edit->openTargetFor(exe).toLocalFile();
        QVERIFY(QFileInfo(folder).isDir());
        QVERIFY(folder.startsWith(tmp.path() + "/"));
        QCOMPARE(QDir(folder).entryList(QDir::Files).size(), 1);

        QVERIFY(!edit->openTargetFor(fakeId()).isValid()); // not stored: nothing to open
    }
};

NOITE_TEST(PasteTest)
#include "tst_paste.moc"
