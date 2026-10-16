#pragma once

#include <QTextEdit>

#include <memory>

class MarkdownShortcuts;
class Trials;

// The capture editor: markdown typing shortcuts, rich/image paste, checkbox toggling,
// Ctrl+click links, and moving a selection into its own document (split, req. 22).
// Reports "ready" to Trials on the first paint after it has focus.
class NoteEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit NoteEdit(QWidget* parent = nullptr);
    void setTrials(Trials* trials) { trials_ = trials; }

    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

    // Moves the selection (text, formats and image resources) into a new document.
    // Returns null if nothing is selected.
    std::unique_ptr<QTextDocument> takeSelectionAsDocument();

    // Diagnostic: key events typed into this editor since the last call.
    QStringList takeKeyLog();

signals:
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool toggleCheckboxAt(const QPoint& pos);

    MarkdownShortcuts* shortcuts_;
    Trials* trials_ = nullptr;
    QStringList keyLog_;
};
