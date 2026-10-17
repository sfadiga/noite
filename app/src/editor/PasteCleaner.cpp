#include "editor/PasteCleaner.h"

#include "editor/AttachmentStore.h"

#include <noite/error.h>
#include <noite/store.h>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <algorithm>
#include <array>

namespace {

constexpr std::array kStyleProperties = {
    QTextFormat::ForegroundBrush, QTextFormat::BackgroundBrush, QTextFormat::FontFamily,
    QTextFormat::FontFamilies,    QTextFormat::FontStyleName,  QTextFormat::FontPointSize,
    QTextFormat::FontPixelSize,
};

struct Span {
    int position;
    int length;
};

std::optional<QString> storeBytes(AttachmentStore* store, const QByteArray& bytes, const QString& ext)
{
    if (!store || bytes.isEmpty() || bytes.size() > noite::kMaxAttachmentBytes)
        return std::nullopt;
    try {
        return store->put(bytes, ext);
    } catch (const noite::Error&) {
        return std::nullopt;
    }
}

// data:image/png;base64,....  ->  attachment
std::optional<QString> fromDataUri(AttachmentStore* store, const QString& src)
{
    static const QRegularExpression re(R"(^data:image/([a-zA-Z0-9.+-]+);base64,(.*)$)",
                                       QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(src);
    if (!m.hasMatch())
        return std::nullopt;
    QString ext = m.captured(1).toLower();
    if (ext == "jpeg")
        ext = "jpg";
    return storeBytes(store, QByteArray::fromBase64(m.captured(2).toLatin1()), ext);
}

// file:///C:/.../clip_image001.png (Word puts pasted images in a temp folder)  ->  attachment
std::optional<QString> fromLocalFile(AttachmentStore* store, const QString& src)
{
    const QUrl url(src);
    if (!url.isLocalFile())
        return std::nullopt;
    QFile file(url.toLocalFile());
    if (file.size() > noite::kMaxAttachmentBytes || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QString suffix = QFileInfo(file.fileName()).suffix().toLower();
    return storeBytes(store, file.readAll(), suffix.isEmpty() ? QString("png") : suffix);
}

void stripStyling(QTextDocument& doc)
{
    QTextCursor cursor(&doc);
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat blockFormat = block.blockFormat();
        if (blockFormat.hasProperty(QTextFormat::BackgroundBrush)) {
            blockFormat.clearProperty(QTextFormat::BackgroundBrush);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(blockFormat);
        }
        // Headings keep their relative size; elsewhere a size adjustment is <font size>.
        const bool heading = blockFormat.headingLevel() > 0;
        QList<std::pair<Span, QTextCharFormat>> changes;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat format = fragment.charFormat();
            if (format.isImageFormat())
                continue;
            bool changed = false;
            for (const int property : kStyleProperties) {
                if (format.hasProperty(property)) {
                    format.clearProperty(property);
                    changed = true;
                }
            }
            if (!heading && format.hasProperty(QTextFormat::FontSizeAdjustment)) {
                format.clearProperty(QTextFormat::FontSizeAdjustment);
                changed = true;
            }
            if (changed)
                changes.append({{fragment.position(), fragment.length()}, format});
        }
        for (const auto& [span, format] : changes) {
            cursor.setPosition(span.position);
            cursor.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
}

void resolveImages(QTextDocument& doc, AttachmentStore* store)
{
    QList<std::pair<int, QString>> images; // position, src
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (const QTextCharFormat f = it.fragment().charFormat(); f.isImageFormat())
                for (int i = 0; i < it.fragment().length(); ++i)
                    images.append({it.fragment().position() + i, f.toImageFormat().name()});

    // Back to front, so replacements don't shift the positions still to visit.
    std::reverse(images.begin(), images.end());
    QTextCursor cursor(&doc);
    for (const auto& [position, src] : images) {
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        if (src.startsWith("noite-att:"))
            continue;
        std::optional<QString> id = fromDataUri(store, src);
        if (!id)
            id = fromLocalFile(store, src);
        if (id) {
            QTextImageFormat format = cursor.charFormat().toImageFormat();
            format.setName("noite-att:" + *id);
            cursor.setCharFormat(format);
            continue;
        }
        const QUrl url(src);
        cursor.removeSelectedText();
        if (url.scheme() == "http" || url.scheme() == "https") {
            QTextCharFormat link;
            link.setAnchor(true);
            link.setAnchorHref(src);
            link.setFontUnderline(true);
            cursor.insertText(url.fileName().isEmpty() ? QString("image") : url.fileName(), link);
        }
    }
}

} // namespace

QTextDocumentFragment cleanPastedHtml(const QString& html, AttachmentStore* store)
{
    QTextDocument doc;
    doc.setHtml(html);
    stripStyling(doc);
    resolveImages(doc, store);
    return QTextDocumentFragment(&doc);
}
