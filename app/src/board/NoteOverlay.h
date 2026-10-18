#pragma once

#include "capture/CaptureController.h"

#include <noite/model.h>

#include <QWidget>

#include <functional>
#include <optional>

namespace noite {
class Store;
}
struct AttachmentStore;
class NoteEdit;
class QFrame;
class NoteFooterBar;

// Edits a note on top of the board (B2): a dimmed backdrop over the whole board with the
// editor in a centred panel. It is the capture editor and controller, so autosave, @dates
// and discard-if-empty behave as in capture. Esc or a click on the backdrop saves and
// closes; Ctrl+Enter marks done and closes; Ctrl+Shift+C cycles the colour.
class NoteOverlay : public QWidget {
    Q_OBJECT
public:
    NoteOverlay(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today, QWidget* board);

    // Read-only for trashed notes (the store rejects their edits): only Esc/backdrop close it.
    void openNote(const noite::ScratchId& id, bool readOnly = false);
    void openBlank();
    bool isOpen() const { return open_; }
    // Saves and closes if open (the board is closing).
    void closeAndSave();

    NoteEdit* editor() const { return editor_; }
    QWidget* panel() const;
    CaptureController& controller() { return controller_; }
    QString footerText() const; // due date and colour
    NoteFooterBar* footerBar() const { return footer_; }

signals:
    // The note's id; empty if it was discarded or never created.
    void closed(std::optional<noite::ScratchId> id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void present();
    void layoutPanel();

    QFrame* panel_;
    NoteEdit* editor_;
    NoteFooterBar* footer_ = nullptr; // made after controller_, which it shows
    CaptureController controller_;
    bool open_ = false;
};
