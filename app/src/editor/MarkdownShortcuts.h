#pragma once

#include <QDate>
#include <QObject>
#include <QTextCharFormat>

#include <functional>

class QTextCursor;
class QTextEdit;

// Markdown typing shortcuts for a QTextEdit, applied right after a character is typed:
// "# "/"## "/"### " headings, "- "/"* " bullets, "1. " numbered, "[ ] "/"[] "/"[x] " at
// block start or in a list item -> checkbox, **bold**, *italic*/_italic_, `code`, [label](url), bare URL + space -> link,
// "@token " (a date, see DateTokens.h) -> removed and reported as dueDateTyped.
class MarkdownShortcuts : public QObject {
    Q_OBJECT
public:
    explicit MarkdownShortcuts(QTextEdit* edit);

    // Returns true if a shortcut was applied.
    bool afterTyped(QChar typed);

    QTextCharFormat linkFormat(const QString& url) const;

    void setToday(std::function<QDate()> today) { today_ = std::move(today); }

signals:
    void dueDateTyped(QDate due);

private:
    bool applyBlockRule(const QString& before);
    bool applyInlineRule(const QString& before, QChar typed);
    bool wrap(const QString& before, const QString& marker, const QTextCharFormat& format);
    bool link(const QString& before);
    bool autolink(const QString& before);
    bool dateToken(const QString& before);
    int blockStart() const;

    QTextEdit* edit_;
    std::function<QDate()> today_ = [] { return QDate::currentDate(); };
};
