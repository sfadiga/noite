// Export of one note to HTML or PDF (M8a, X1-X14).

#include <noite/content.h>
#include <noite/error.h>
#include <noite/store.h>

#include "db.h"
#include "store_internal.h"

#include <QDir>
#include <QFont>
#include <QImage>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QUrl>

namespace noite {

using namespace detail;

namespace {

// A4 at the 96 dpi QTextDocument lays out in; QTextDocument::print scales it to the page.
constexpr qreal kPxPerMm = 96.0 / 25.4;
const QSizeF kA4Px(210 * kPxPerMm, 297 * kPxPerMm);
constexpr qreal kMarginPx = 20 * kPxPerMm;

// X5, X7, X8. Qt's own head styles <li class="checked|unchecked"> markers as ☐ / ☒; a
// checked item reads better as ☑.
constexpr const char* kExportCss = R"(
body { background: #ffffff; color: #1a1a1a; max-width: 46em; margin: 2em auto; padding: 0 1em; }
img { max-width: 100%; height: auto; }
li.unchecked::marker { content: "\2610\00a0"; }
li.checked::marker { content: "\2611\00a0"; }
)";

QString mimeFor(const QString& attachmentId)
{
    const QString ext = attachmentId.section('.', -1);
    if (ext == "jpg" || ext == "jpeg")
        return QStringLiteral("image/jpeg");
    if (ext == "png" || ext == "gif" || ext == "bmp" || ext == "webp")
        return "image/" + ext;
    return QStringLiteral("application/octet-stream");
}

QString attachmentUrl(const QString& id)
{
    return "noite-att:" + id;
}

[[noreturn]] void throwIo(const QString& path, const QString& reason)
{
    throw Error(ErrorCode::Io, QString("couldn't write %1: %2").arg(QDir::toNativeSeparators(path), reason));
}

} // namespace

std::unique_ptr<QTextDocument> detail::exportDocument(const Store& store, const QString& html,
                                                      qreal textWidth)
{
    QString text = html;
    QHash<QString, QImage> images;
    for (const QString& id : content::attachmentRefs(html)) {
        QImage image;
        try {
            image = QImage::fromData(store.attachmentBytes(id));
        } catch (const Error&) {
        }
        if (image.isNull())
            text.replace(QRegularExpression("<img[^>]*" + QRegularExpression::escape(attachmentUrl(id)) + "[^>]*>"),
                         QStringLiteral("[missing image]"));
        else
            images.insert(id, image);
    }

    auto doc = std::make_unique<QTextDocument>();
    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(10.5);
    doc->setDefaultFont(font);
    doc->setHtml(text);
    for (auto it = images.cbegin(); it != images.cend(); ++it)
        doc->addResource(QTextDocument::ImageResource, QUrl(attachmentUrl(it.key())), it.value());
    if (textWidth <= 0)
        return doc;

    // Collected first: changing a format while walking the fragments would move them.
    QList<std::pair<int, QTextImageFormat>> resized;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.charFormat().isImageFormat())
                continue;
            QTextImageFormat format = fragment.charFormat().toImageFormat();
            const QImage image = images.value(format.name().mid(attachmentUrl({}).size()));
            const qreal width = format.width() > 0 ? format.width() : image.width();
            const qreal height = format.height() > 0 ? format.height() : image.height();
            if (width <= textWidth || width <= 0)
                continue;
            format.setWidth(textWidth);
            format.setHeight(height * textWidth / width);
            resized.append({fragment.position(), format});
        }
    }
    for (const auto& [position, format] : resized) {
        QTextCursor cursor(doc.get());
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        cursor.setCharFormat(format);
    }
    return doc;
}

void Store::exportScratch(const ScratchId& id, const QString& path, ExportFormat format) const
{
    const Scratch scratch = get(id);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throwIo(path, file.errorString());

    if (format == ExportFormat::Html) {
        const auto doc = exportDocument(*this, scratch.html, 0);
        QString html = doc->toHtml();
        for (const QString& ref : content::attachmentRefs(html)) {
            try {
                html.replace(attachmentUrl(ref), "data:" + mimeFor(ref) + ";base64,"
                                                     + QString::fromLatin1(attachmentBytes(ref).toBase64()));
            } catch (const Error&) {
                // only a link can still point at a missing attachment (images were replaced)
            }
        }
        if (!html.contains(QLatin1String("charset")))
            html.replace(QLatin1String("<head>"), QLatin1String("<head><meta charset=\"utf-8\" />"));
        // Our own <title>: Qt writes DocumentTitle unescaped.
        html.replace(QLatin1String("</head>"), QString("<title>%1</title><style type=\"text/css\">%2</style></head>")
                                                   .arg(scratch.title.toHtmlEscaped(), kExportCss));
        file.write(html.toUtf8());
    } else {
        // The document is paginated here (page size and margins in its own units), so print()
        // adds neither its 2 cm margins nor page numbers.
        const auto doc = exportDocument(*this, scratch.html, kA4Px.width() - 2 * kMarginPx);
        doc->setPageSize(kA4Px);
        QTextFrameFormat frame = doc->rootFrame()->frameFormat();
        frame.setMargin(kMarginPx);
        doc->rootFrame()->setFrameFormat(frame);

        QPdfWriter pdf(&file);
        pdf.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF()));
        pdf.setResolution(300);
        pdf.setTitle(scratch.title);
        pdf.setCreator(QStringLiteral("noite"));
        doc->print(&pdf);
    }
    if (!file.commit())
        throwIo(path, file.errorString());
}

std::optional<QString> Store::lastExportDir() const
{
    Stmt s = db().prepare("SELECT value FROM settings WHERE key = 'last_export_dir'");
    if (!s.step())
        return std::nullopt;
    return s.text(0);
}

void Store::setLastExportDir(const QString& dir)
{
    Stmt s = db().prepare("INSERT OR REPLACE INTO settings(key, value) VALUES ('last_export_dir', ?)");
    s.bind(1, dir);
    s.step();
}

} // namespace noite
