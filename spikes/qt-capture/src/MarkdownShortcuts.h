#pragma once

#include <QObject>
#include <QTextCharFormat>

class QTextCursor;
class QTextEdit;

// Markdown typing shortcuts for a QTextEdit, applied right after a character is typed:
// "# "/"## "/"### " headings, "- "/"* " bullets, "1. " numbered, "[ ] "/"[x] " in a list
// item -> checkbox, **bold**, *italic*/_italic_, `code`, [label](url).
class MarkdownShortcuts : public QObject {
    Q_OBJECT
public:
    explicit MarkdownShortcuts(QTextEdit* edit);

    // Returns true if a shortcut was applied.
    bool afterTyped(QChar typed);

private:
    bool applyBlockRule(const QString& before);
    bool applyInlineRule(const QString& before, QChar typed);
    bool wrap(const QString& before, const QString& marker, const QTextCharFormat& format);
    bool link(const QString& before);
    int blockStart() const;

    QTextEdit* edit_;
};
