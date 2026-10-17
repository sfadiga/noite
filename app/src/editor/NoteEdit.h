#pragma once

#include <QDate>
#include <QTextEdit>

#include <functional>
#include <memory>

class MarkdownShortcuts;

// The capture editor: markdown typing shortcuts, rich/image paste, checkbox toggling,
// Ctrl+click links, and moving a selection into its own document (split, req. 22).
class NoteEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit NoteEdit(QWidget* parent = nullptr);

    // "Today" for @date tokens; injectable for tests. Defaults to QDate::currentDate().
    void setToday(std::function<QDate()> today) { today_ = std::move(today); }
    QDate today() const { return today_(); }

    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

    // Moves the selection (text, formats and image resources) into a new document.
    // Returns null if nothing is selected.
    std::unique_ptr<QTextDocument> takeSelectionAsDocument();

    // Ctrl+click opens only web and mail links (pasted HTML can carry file: or other schemes).
    static bool isSafeLink(const QUrl& url);

    // Ctrl+Shift+V: paste the clipboard as plain text, no rendering or link detection.
    void pastePlainText();

signals:
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    // Dictation, IMEs and the emoji panel commit text here instead of through key presses.
    void inputMethodEvent(QInputMethodEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool toggleCheckboxAt(const QPoint& pos);

    MarkdownShortcuts* shortcuts_;
    std::function<QDate()> today_ = [] { return QDate::currentDate(); };
};
