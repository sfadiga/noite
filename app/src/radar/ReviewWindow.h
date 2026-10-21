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
class QLabel;
class QStackedWidget;

// The morning review (D32, M4): one note at a time from Store::reviewQueue, and a decision
// for each: D Done, S Snooze (1 Tomorrow, 2 Next Monday, 3 Pick…), K Keep, Del Trash.
// Enter edits the note in place (Esc/Ctrl+Enter back to the review); Left goes back and
// undoes the previous decision; Ctrl+Z undoes the last one. Esc closes; notes not reached
// stay as they are. A store refusal leaves the note current; a note deleted elsewhere is
// skipped.
class ReviewWindow : public QWidget {
    Q_OBJECT
public:
    ReviewWindow(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                 QWidget* parent = nullptr);

    void start();

    int position() const { return pos_ + 1; } // 1-based
    int total() const { return int(queue_.size()); }
    std::optional<noite::ScratchId> currentId() const;
    QString headerText() const;
    QString statusText() const;
    bool isFinished() const { return pos_ >= queue_.size(); }
    QString summaryText() const;
    bool isEditing() const { return editing_; }
    NoteEdit* editor() const { return editor_; }

    void done();
    void keep();
    void defer(); // L (M8c)
    void trash();
    void snooze(QDate due);
    void back();
    void undo();
    void editCurrent();
    void finishEditing();
    void requestOpenBoard();

    static QDate nextMonday(QDate today);

signals:
    void openBoardRequested();
    // Before the current note is decided on or edited, so a capture holding it can save and close.
    void aboutToChange(const noite::ScratchId& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Kind { Done, Snoozed, Kept, Deferred, Trashed }; // the summary's order
    struct Outcome {
        Kind kind;
        std::function<void()> undo;
    };

    // Runs `action` on the current note and records its outcome, then moves on. A note gone
    // elsewhere is skipped; any other refusal keeps it current.
    void decide(Kind kind, const std::function<std::function<void()>(const noite::ScratchId&)>& action);
    void undoAt(int position);
    void showCurrent();
    void showStatus(const QString& text);
    void pickSnoozeDate();

    noite::Store& store_;
    std::function<QDate()> today_;
    QList<noite::ReviewItem> queue_;
    QList<std::optional<Outcome>> outcomes_;
    QList<int> decided_; // positions in the order they were decided (for Ctrl+Z)
    int pos_ = 0;
    bool editing_ = false;
    bool snoozing_ = false; // after S: waiting for 1, 2 or 3

    QStackedWidget* pages_;
    QLabel* header_;
    QLabel* counter_;
    NoteEdit* editor_;
    QLabel* status_;
    QLabel* summary_;
    CaptureController controller_;
};
