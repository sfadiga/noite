#include "MarkdownShortcuts.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>

namespace {

// Removes [from, from + length) and returns the format of the first removed character,
// which is the format the text "outside" the markers had.
QTextCharFormat removeRange(QTextCursor& c, int from, int length)
{
    c.setPosition(from + 1);
    const QTextCharFormat outside = c.charFormat();
    c.setPosition(from);
    c.setPosition(from + length, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    return outside;
}

} // namespace

MarkdownShortcuts::MarkdownShortcuts(QTextEdit* edit)
    : QObject(edit)
    , edit_(edit)
{
}

int MarkdownShortcuts::blockStart() const
{
    return edit_->textCursor().block().position();
}

bool MarkdownShortcuts::afterTyped(QChar typed)
{
    const QTextCursor cursor = edit_->textCursor();
    if (cursor.hasSelection())
        return false;
    const QString before = cursor.block().text().left(cursor.positionInBlock());
    if (typed == ' ' && applyBlockRule(before))
        return true;
    return applyInlineRule(before, typed);
}

bool MarkdownShortcuts::applyBlockRule(const QString& before)
{
    QTextCursor c = edit_->textCursor();
    const int start = blockStart();
    QTextList* list = c.currentList();

    // "[ ] ", "[] " (Notion-style) or "[x] ": in a list item, or at block start (making the list).
    static const QRegularExpression checkbox(R"(^\[( ?|[xX])\] $)");
    if (const auto m = checkbox.match(before); m.hasMatch()) {
        c.beginEditBlock();
        removeRange(c, start, before.size());
        if (!list)
            c.createList(QTextListFormat::ListDisc);
        QTextBlockFormat f = c.blockFormat();
        f.setMarker(m.captured(1).trimmed().isEmpty() ? QTextBlockFormat::MarkerType::Unchecked
                                                      : QTextBlockFormat::MarkerType::Checked);
        c.setBlockFormat(f);
        c.endEditBlock();
        return true;
    }

    static const QRegularExpression heading("^(#{1,3}) $");
    if (const auto m = heading.match(before); m.hasMatch() && !list) {
        const int level = m.captured(1).size();
        static const qreal scale[] = {2.0, 1.5, 1.17};
        QTextCharFormat chars;
        chars.setFontWeight(QFont::Bold);
        chars.setFontPointSize(edit_->document()->defaultFont().pointSizeF() * scale[level - 1]);
        c.beginEditBlock();
        removeRange(c, start, before.size());
        QTextBlockFormat f = c.blockFormat();
        f.setHeadingLevel(level);
        c.setBlockFormat(f);
        c.setBlockCharFormat(chars);
        c.endEditBlock();
        edit_->mergeCurrentCharFormat(chars);
        return true;
    }

    if (!list && (before == "- " || before == "* " || before == "1. ")) {
        c.beginEditBlock();
        removeRange(c, start, before.size());
        c.createList(before == "1. " ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
        c.endEditBlock();
        return true;
    }
    return false;
}

bool MarkdownShortcuts::applyInlineRule(const QString& before, QChar typed)
{
    if (typed == ' ')
        return autolink(before);
    if (typed == ')')
        return link(before);
    if (typed == '*') {
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        if (wrap(before, "**", bold))
            return true;
        QTextCharFormat italic;
        italic.setFontItalic(true);
        return wrap(before, "*", italic);
    }
    if (typed == '_') {
        QTextCharFormat italic;
        italic.setFontItalic(true);
        return wrap(before, "_", italic);
    }
    if (typed == '`') {
        QTextCharFormat code;
        code.setFontFixedPitch(true);
        code.setFontFamilies({"Consolas", "Courier New", "monospace"});
        return wrap(before, "`", code);
    }
    return false;
}

// "<marker>content<marker>" ending at the cursor becomes "content" in `format`.
bool MarkdownShortcuts::wrap(const QString& before, const QString& marker, const QTextCharFormat& format)
{
    const int n = marker.size();
    if (!before.endsWith(marker) || before.size() < 2 * n + 1)
        return false;
    const int close = before.size() - n;
    const QChar m = marker[0];
    // A single-char marker must not be half of a doubled one ("**b*" is bold in progress).
    if (n == 1 && before[close - 1] == m)
        return false;
    const int open = before.lastIndexOf(marker, close - 1);
    if (open < 0 || close - open - n < 1)
        return false;
    if (n == 1 && open > 0 && before[open - 1] == m)
        return false;
    const QString content = before.mid(open + n, close - open - n);
    if (content.front().isSpace() || content.back().isSpace() || content.startsWith(m))
        return false;
    // Not inside words or numbers ("my_var_name", "2*3*4"), URLs, or code.
    if (open > 0 && (before[open - 1].isLetterOrNumber() || before[open - 1] == '_'))
        return false;
    static const QRegularExpression space(R"(\s)");
    const QString token = before.mid(before.lastIndexOf(space, open) + 1);
    if (token.contains("://") || token.startsWith("www."))
        return false;

    const int start = blockStart();
    QTextCursor c = edit_->textCursor();
    c.setPosition(start + open + 1);
    if (c.charFormat().fontFixedPitch())
        return false;
    c.beginEditBlock();
    removeRange(c, start + close, n);
    const QTextCharFormat outside = removeRange(c, start + open, n);
    c.setPosition(start + open);
    c.setPosition(start + open + content.size(), QTextCursor::KeepAnchor);
    c.mergeCharFormat(format);
    c.endEditBlock();
    edit_->setCurrentCharFormat(outside);
    return true;
}

// A bare URL followed by a typed space becomes a link to itself.
bool MarkdownShortcuts::autolink(const QString& before)
{
    static const QRegularExpression re(R"((?:^|\s)((?:https?://|www\.)\S+) $)");
    const auto m = re.match(before);
    if (!m.hasMatch())
        return false;
    const int start = blockStart();
    QTextCursor c = edit_->textCursor();
    c.setPosition(start + m.capturedStart(1));
    c.setPosition(start + m.capturedEnd(1), QTextCursor::KeepAnchor);
    if (c.charFormat().isAnchor())
        return false;
    const QTextCharFormat outside = edit_->currentCharFormat();
    c.beginEditBlock();
    c.mergeCharFormat(linkFormat(m.captured(1)));
    c.endEditBlock();
    edit_->setCurrentCharFormat(outside);
    return true;
}

QTextCharFormat MarkdownShortcuts::linkFormat(const QString& url) const
{
    QTextCharFormat anchor;
    anchor.setAnchor(true);
    anchor.setAnchorHref(url.startsWith("www.") ? "https://" + url : url);
    anchor.setFontUnderline(true);
    anchor.setForeground(edit_->palette().link());
    return anchor;
}

bool MarkdownShortcuts::link(const QString& before)
{
    static const QRegularExpression re(R"(\[([^\[\]]+)\]\(([^()\s]+)\)$)");
    const auto m = re.match(before);
    if (!m.hasMatch())
        return false;

    const int start = blockStart();
    QTextCursor c = edit_->textCursor();
    c.beginEditBlock();
    const QTextCharFormat outside = removeRange(c, start + m.capturedStart(), m.capturedLength());
    QTextCharFormat anchor = outside;
    anchor.merge(linkFormat(m.captured(2)));
    c.insertText(m.captured(1), anchor);
    c.endEditBlock();
    edit_->setCurrentCharFormat(outside);
    return true;
}
