#pragma once

#include "editor/AttachmentStore.h"
#include "editor/OpenFolder.h"

#include <QDate>
#include <QTextEdit>
#include <QUrl>

#include <functional>
#include <memory>

class MarkdownShortcuts;

// The capture editor: markdown typing shortcuts, rich/image paste, checkbox toggling,
// Ctrl+click links, and moving a selection into its own document (split, req. 22).
class NoteEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit NoteEdit(QWidget* parent = nullptr);

    // Pasted images and files go here; without a store they are not accepted.
    void setAttachmentStore(AttachmentStore* store) { attachments_ = store; }

    // What Ctrl+click on a noite-att link opens. The bytes are written to
    // <open folder>/<first 12 hex of the id>/<safeFileName(name, id)> (a file of the same
    // size already there is reused, as it may be open in a viewer) and that file is returned,
    // or for programs and scripts (which would run) the folder that holds it. Empty if the
    // bytes can't be read or written.
    QUrl openTargetFor(const QString& attachmentId, const QString& name = {}) const;
    // Where openTargetFor writes; defaults to %TEMP%\noite (A5).
    void setOpenFolder(const QString& dir) { openFolder_ = dir; }

    // "Today" for @date tokens; injectable for tests. Defaults to QDate::currentDate().
    void setToday(std::function<QDate()> today);

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
    // An "@token " typed in the text named a date (the token itself is removed).
    void dueDateTyped(QDate due);

protected:
    // Images referenced as noite-att:<id> load from the attachment store.
    QVariant loadResource(int type, const QUrl& name) override;
    void keyPressEvent(QKeyEvent* event) override;
    // Dictation, IMEs and the emoji panel commit text here instead of through key presses.
    void inputMethodEvent(QInputMethodEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    bool toggleCheckboxAt(const QPoint& pos);
    // The text of the link `href` under `pos` (a file chip's name).
    QString anchorTextAt(const QPoint& pos, const QString& href) const;
    // Copied files become links ("chips") to their attachment; too-large ones become text.
    void insertFileChips(const QStringList& files);

    MarkdownShortcuts* shortcuts_;
    AttachmentStore* attachments_ = nullptr;
    QString openFolder_ = defaultOpenFolder();
};
