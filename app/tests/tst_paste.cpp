#include "fake_attachments.h"
#include "registry.h"

#include "editor/NoteEdit.h"

#include <noite/store.h>

#include <QBuffer>
#include <QFile>
#include <QMimeData>
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
        QFile stored(store->pathFor(store->ids()[0]));
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QCOMPARE(stored.readAll(), jpeg);
    }

    void reloadedNoteShowsImage()
    {
        const QString id = store->put(encoded(Qt::green, "PNG"), "png");
        edit->setHtml("<p><img src=\"noite-att:" + id + "\"/></p>");
        const QImage img = doc()->resource(QTextDocument::ImageResource, QUrl("noite-att:" + id)).value<QImage>();
        QVERIFY(!img.isNull());
        QCOMPARE(img.pixelColor(0, 0), QColor(Qt::green));
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

    // Opening an attached program would run it: show its folder instead.
    void attachmentsOpenSafely()
    {
        const QString pdf = store->put("pdf", "pdf");
        const QString exe = store->put("exe", "exe");
        QCOMPARE(edit->openTargetFor(pdf), QUrl::fromLocalFile(store->pathFor(pdf)));
        QCOMPARE(edit->openTargetFor(exe), QUrl::fromLocalFile(QFileInfo(store->pathFor(exe)).absolutePath()));
    }
};

NOITE_TEST(PasteTest)
#include "tst_paste.moc"
