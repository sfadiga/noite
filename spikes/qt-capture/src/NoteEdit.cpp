#include "NoteEdit.h"

#include "MarkdownShortcuts.h"
#include "Trials.h"

#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QGuiApplication>
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

} // namespace

void NoteEdit::insertFromMimeData(const QMimeData* source)
{
    // Spreadsheets and OneNote put a bitmap next to the HTML; the HTML wins when it has content.
    if (source->hasImage() && !htmlHasContent(source)) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            // No "//": with an authority, QUrl would parse the hash as a hostname and reject it.
            const QString name = "noite-att:"
                + QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex())
                + ".png";
            document()->addResource(QTextDocument::ImageResource, QUrl(name), image);
            QTextImageFormat format;
            format.setName(name);
            textCursor().insertImage(format);
            return;
        }
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

bool NoteEdit::isSafeLink(const QUrl& url)
{
    static const QStringList allowed = {"http", "https", "mailto"};
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

QStringList NoteEdit::takeKeyLog()
{
    return std::exchange(keyLog_, {});
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
    bool applied = false;
    if (text.size() == 1 && text[0].isPrint())
        applied = shortcuts_->afterTyped(text[0]);

    QStringList codes;
    for (QChar c : text)
        codes << QString("U+%1").arg(uint(c.unicode()), 4, 16, QChar('0')).toUpper();
    keyLog_ << QString("0x%1,%2,0x%3,%4,%5,%6")
                   .arg(event->key(), 0, 16)
                   .arg(codes.join(' '))
                   .arg(int(event->modifiers()), 0, 16)
                   .arg(event->nativeVirtualKey())
                   .arg(event->nativeScanCode())
                   .arg(applied ? QStringLiteral("shortcut") : QString());
}

void NoteEdit::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
        if (event->modifiers() & Qt::ControlModifier) {
            const QString href = anchorAt(pos);
            if (!href.isEmpty()) {
                if (isSafeLink(QUrl(href)))
                    QDesktopServices::openUrl(QUrl(href));
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

void NoteEdit::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
    // The raster backing store is flushed to the screen right after this paint.
    if (trials_ && trials_->pending() && hasFocus())
        trials_->ready();
}
