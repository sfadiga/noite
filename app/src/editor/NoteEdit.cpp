#include "editor/NoteEdit.h"

#include "editor/MarkdownShortcuts.h"
#include "editor/PasteCleaner.h"

#include <noite/error.h>
#include <noite/store.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocumentFragment>

NoteEdit::NoteEdit(QWidget* parent)
    : QTextEdit(parent)
    , shortcuts_(new MarkdownShortcuts(this))
{
    setAcceptRichText(true);
    connect(shortcuts_, &MarkdownShortcuts::dueDateTyped, this, &NoteEdit::dueDateTyped);
}

void NoteEdit::setToday(std::function<QDate()> today)
{
    shortcuts_->setToday(std::move(today));
}

bool NoteEdit::canInsertFromMimeData(const QMimeData* source) const
{
    return source->hasImage() || QTextEdit::canInsertFromMimeData(source);
}

namespace {

// HTML that carries real content (text, tables), not just a wrapper around one image.
bool htmlHasContent(const QMimeData* source)
{
    if (!source->hasHtml())
        return false;
    QString text = QTextDocumentFragment::fromHtml(source->html()).toPlainText();
    return !text.remove(QChar::ObjectReplacementCharacter).trimmed().isEmpty();
}

// Rendered as markdown only with at least two markdown constructs; one "- " line in an
// email or one "# comment" in a shell snippet stays plain text.
bool looksLikeMarkdown(const QString& text)
{
    static const QRegularExpression construct(
        R"((?:^|\n)[ \t]*(?:#{1,6} |[-*+] |\d+\. |> )|\*\*\S[^*\n]*\*\*|\[[^\]\n]+\]\([^)\s]+\)|`[^`\n]+`)");
    int count = 0;
    for (auto it = construct.globalMatch(text); it.hasNext() && count < 2; it.next())
        ++count;
    return count >= 2;
}

// Markdown joins consecutive lines into one paragraph; pasted notes expect each line to
// stay a line. Separate them with a blank line, except inside fences and table rows.
QString keepLineBreaks(const QString& markdown)
{
    const QStringList lines = markdown.split('\n');
    QStringList out;
    bool fenced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i];
        out << line;
        if (line.trimmed().startsWith("```"))
            fenced = !fenced;
        const bool next = i + 1 < lines.size() && !lines[i + 1].trimmed().isEmpty();
        const bool table = line.trimmed().startsWith('|') && next && lines[i + 1].trimmed().startsWith('|');
        if (!fenced && !table && next && !line.trimmed().isEmpty())
            out << QString();
    }
    return out.join('\n');
}

// Explorer copies: every URL is a local file.
QStringList localFiles(const QMimeData* source)
{
    QStringList files;
    if (!source->hasUrls())
        return files;
    for (const QUrl& url : source->urls()) {
        if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile())
            return {};
        files << url.toLocalFile();
    }
    return files;
}

// The clipboard's own encoded image if there is one (no re-encoding), else a PNG of it.
std::pair<QByteArray, QString> imageBytes(const QMimeData* source)
{
    for (const auto& [mime, ext] : {std::pair{"image/png", "png"}, {"image/jpeg", "jpg"}, {"image/gif", "gif"}})
        if (source->hasFormat(mime))
            return {source->data(mime), ext};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    qvariant_cast<QImage>(source->imageData()).save(&buffer, "PNG");
    return {png, "png"};
}

} // namespace

void NoteEdit::insertFileChips(const QStringList& files)
{
    QTextCursor cursor = textCursor();
    const QTextCharFormat outside = currentCharFormat();
    cursor.beginEditBlock();
    for (int i = 0; i < files.size(); ++i) {
        if (i > 0)
            cursor.insertText(" ", outside);
        const QFileInfo info(files[i]);
        QFile file(files[i]);
        std::optional<QString> id;
        if (info.size() <= noite::kMaxAttachmentBytes && file.open(QIODevice::ReadOnly)) {
            try {
                id = attachments_->put(file.readAll(), info.suffix().isEmpty() ? QString("bin") : info.suffix());
            } catch (const noite::Error&) {
            }
        }
        if (!id) {
            cursor.insertText(info.fileName() + " (too large to attach)", outside);
            continue;
        }
        QTextCharFormat chip = outside;
        chip.merge(shortcuts_->linkFormat("noite-att:" + *id));
        cursor.insertText(info.fileName(), chip);
    }
    cursor.endEditBlock();
    setCurrentCharFormat(outside);
}

void NoteEdit::insertFromMimeData(const QMimeData* source)
{
    if (const QStringList files = localFiles(source); !files.isEmpty()) {
        if (attachments_)
            insertFileChips(files);
        return;
    }
    // Spreadsheets and OneNote put a bitmap next to the HTML; the HTML wins when it has content.
    if (source->hasImage() && !htmlHasContent(source)) {
        if (!attachments_)
            return;
        const auto [bytes, ext] = imageBytes(source);
        try {
            if (bytes.size() <= noite::kMaxAttachmentBytes) {
                QTextImageFormat format;
                format.setName("noite-att:" + attachments_->put(bytes, ext));
                textCursor().insertImage(format);
            }
        } catch (const noite::Error&) {
        }
        return;
    }
    if (source->hasHtml()) {
        textCursor().insertFragment(cleanPastedHtml(source->html(), attachments_));
        return;
    }
    if (!source->hasHtml() && source->hasText()) {
        const QString text = source->text();
        static const QRegularExpression url(R"(^\s*((?:https?://|www\.)\S+)\s*$)");
        if (const auto m = url.match(text); m.hasMatch()) {
            const QTextCharFormat outside = currentCharFormat();
            QTextCharFormat anchor = outside;
            anchor.merge(shortcuts_->linkFormat(m.captured(1)));
            textCursor().insertText(m.captured(1), anchor);
            setCurrentCharFormat(outside);
            return;
        }
        if (looksLikeMarkdown(text)) {
            textCursor().insertFragment(QTextDocumentFragment::fromMarkdown(keepLineBreaks(text)));
            return;
        }
    }
    QTextEdit::insertFromMimeData(source);
}

QUrl NoteEdit::openTargetFor(const QString& attachmentId, const QString& name) const
{
    if (!attachments_)
        return {};
    QByteArray bytes;
    try {
        bytes = attachments_->bytes(attachmentId);
    } catch (const noite::Error& error) {
        qWarning() << "open attachment:" << error.what();
        return {};
    }
    const QString dir = openFolder_ + "/" + attachmentId.left(12);
    const QString path = dir + "/" + safeFileName(name, attachmentId);
    if (QFileInfo(path).size() != bytes.size() || !QFileInfo::exists(path)) {
        QSaveFile file(path);
        if (!QDir().mkpath(dir) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
            || !file.commit()) {
            qWarning() << "open attachment: cannot write" << path << file.errorString();
            return {};
        }
    }
    // Both the stored id's extension and the written name's count: the chip's text is just
    // text, so "report.pdf" bytes shown as "run.exe" must not be launched either.
    static const QStringList runnable = {"exe", "bat", "cmd", "com", "msi", "ps1", "vbs", "vbe", "js", "jse", "wsf",
                                         "scr", "lnk", "url", "hta", "jar", "reg", "cpl", "msc", "pif", "appref-ms"};
    if (runnable.contains(QFileInfo(attachmentId).suffix().toLower())
        || runnable.contains(QFileInfo(path).suffix().toLower()))
        return QUrl::fromLocalFile(dir);
    return QUrl::fromLocalFile(path);
}

QString NoteEdit::anchorTextAt(const QPoint& pos, const QString& href) const
{
    const QTextCursor cursor = cursorForPosition(pos);
    for (auto it = cursor.block().begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (fragment.isValid() && fragment.charFormat().anchorHref() == href
            && fragment.contains(cursor.position()))
            return fragment.text();
    }
    // The click can land just past the chip's last character.
    for (auto it = cursor.block().begin(); !it.atEnd(); ++it) {
        if (it.fragment().isValid() && it.fragment().charFormat().anchorHref() == href)
            return it.fragment().text();
    }
    return {};
}

QVariant NoteEdit::loadResource(int type, const QUrl& name)
{
    if (type == QTextDocument::ImageResource && name.scheme() == "noite-att" && attachments_) {
        try {
            return QImage::fromData(attachments_->bytes(name.path()));
        } catch (const noite::Error& error) {
            qWarning() << "image:" << error.what();
            return {};
        }
    }
    return QTextEdit::loadResource(type, name);
}

bool NoteEdit::isSafeLink(const QUrl& url)
{
    static const QStringList allowed = {"http", "https", "mailto", "noite-att"};
    return url.isValid() && allowed.contains(url.scheme().toLower());
}

void NoteEdit::pastePlainText()
{
    insertPlainText(QGuiApplication::clipboard()->text());
}

std::unique_ptr<QTextDocument> NoteEdit::takeSelectionAsDocument()
{
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection())
        return nullptr;

    auto moved = std::make_unique<QTextDocument>();
    moved->setDefaultFont(document()->defaultFont());
    QTextCursor(moved.get()).insertFragment(cursor.selection());
    for (QTextBlock b = moved->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (!f.isImageFormat())
                continue;
            const QUrl url(f.toImageFormat().name());
            moved->addResource(QTextDocument::ImageResource, url,
                               document()->resource(QTextDocument::ImageResource, url));
        }
    }

    cursor.beginEditBlock();
    cursor.removeSelectedText();
    if (cursor.block().text().isEmpty() && cursor.block() != document()->firstBlock())
        cursor.deletePreviousChar();
    cursor.endEditBlock();
    setTextCursor(cursor);
    return moved;
}

void NoteEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    if (event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        pastePlainText();
        return;
    }
    QTextEdit::keyPressEvent(event);
    const QString text = event->text();
    if (text.size() == 1 && text[0].isPrint())
        shortcuts_->afterTyped(text[0]);
}

void NoteEdit::inputMethodEvent(QInputMethodEvent* event)
{
    QTextEdit::inputMethodEvent(event);
    // Shortcuts look at the text before the cursor, so the committed text's last
    // character is the "typed" one (a dictated phrase ending in a space still autolinks).
    const QString commit = event->commitString();
    if (!commit.isEmpty() && commit.back().isPrint())
        shortcuts_->afterTyped(commit.back());
}

void NoteEdit::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ControlModifier) {
            const QString href = anchorAt(pos);
            if (!href.isEmpty()) {
                const QUrl url(href);
                if (url.scheme() == "noite-att") {
                    if (const QUrl target = openTargetFor(url.path(), anchorTextAt(pos, href)); target.isValid())
                        QDesktopServices::openUrl(target);
                } else if (isSafeLink(url)) {
                    QDesktopServices::openUrl(url);
                }
                return;
            }
        }
        if (toggleCheckboxAt(pos))
            return;
    }
    QTextEdit::mousePressEvent(event);
}

// A click in the indent to the left of a checkbox item's first line toggles it.
bool NoteEdit::toggleCheckboxAt(const QPoint& pos)
{
    const QTextBlock block = cursorForPosition(pos).block();
    QTextBlockFormat format = block.blockFormat();
    if (format.marker() == QTextBlockFormat::MarkerType::NoMarker)
        return false;
    QTextCursor start(block);
    const QRect line = cursorRect(start);
    if (pos.x() >= line.left() || pos.y() < line.top() || pos.y() > line.bottom())
        return false;
    format.setMarker(format.marker() == QTextBlockFormat::MarkerType::Checked
                         ? QTextBlockFormat::MarkerType::Unchecked
                         : QTextBlockFormat::MarkerType::Checked);
    start.setBlockFormat(format);
    return true;
}
