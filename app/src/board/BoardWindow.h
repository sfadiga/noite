#pragma once

#include "board/UndoLog.h"

#include <noite/model.h>

#include <QDate>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <optional>

namespace noite {
class Store;
}
struct AttachmentStore;
class BoardModel;
class CardDelegate;
class QLabel;
class QLineEdit;
class QListView;
class QTabBar;

// The board (D17): tabs Today · All · Completed · Trash, an as-you-type search over the
// current tab (D19) and a grid of cards. Keys (D30): "/" focuses search; Esc clears a
// non-empty search, otherwise closes the board; D done/reopen; Del trash/restore;
// Ctrl+Shift+C colour; Ctrl+Z undo. Actions apply to the selected card, and do nothing
// without one. A store error is shown in the status line and the board reloads.
class BoardWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr int kSearchDebounceMs = 120;

    // What survives closing and reopening the board within a session (B4).
    struct ViewState {
        noite::Tab tab = noite::Tab::Today;
        QString search;
    };

    BoardWindow(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                QWidget* parent = nullptr);
    ~BoardWindow() override;

    ViewState viewState() const;
    void restoreViewState(const ViewState& state);
    void setTab(noite::Tab tab);
    void showStatus(const QString& text);
    // Re-queries the store, keeping the selected card selected (or the same row if it left).
    void reload();

    // Board actions on the selected card.
    void markDoneOrReopen(); // Reopen in Completed
    void trashOrRestore();   // Restore in Trash
    void cycleColor();
    void setColor(std::optional<noite::Color> color);
    void setDue(std::optional<QDate> due);
    void cloneSelected();
    void undo();

    std::optional<noite::ScratchId> selectedId() const;
    UndoLog& undoLog() { return undo_; }
    BoardModel& model() const { return *model_; }
    QListView* grid() const { return grid_; }
    QLineEdit* searchField() const { return search_; }
    QTabBar* tabBar() const { return tabs_; }
    QString statusText() const;
    // "No notes" / "Nothing matches" while the grid is empty; empty otherwise.
    QString emptyText() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // Runs `action` on the selected card's summary; it returns the card to select afterwards
    // (empty: keep the row). Store errors are reported and the board reloads.
    void act(const std::function<std::optional<noite::ScratchId>(const noite::ScratchSummary&)>& action);
    void reloadSelecting(const std::optional<noite::ScratchId>& id, int fallbackRow);
    // Pushes an undo entry whose closure runs a store call, then reloads selecting `id`.
    void pushUndo(const QString& label, const noite::ScratchId& id, std::function<void()> storeCall);
    void showContextMenu(const QPoint& pos);
    void pickDue();
    void onModelReset();
    void layoutEmptyLabel();

    noite::Store& store_;
    UndoLog undo_;
    std::optional<noite::ScratchId> undoTarget_; // the card an undo entry asks to select
    BoardModel* model_;
    CardDelegate* delegate_;
    QTabBar* tabs_;
    QLineEdit* search_;
    QListView* grid_;
    QLabel* empty_;
    QLabel* status_;
    QTimer searchDelay_;
};
