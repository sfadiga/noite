#pragma once

#include "board/UndoLog.h"

#include <noite/model.h>

#include <QDate>
#include <QSet>
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
class NoteOverlay;
class QLabel;
class QLineEdit;
class QListView;
class TabStrip;
class ChipBar;

// The board (D17): tabs Today · All · Completed · Trash, an as-you-type search over the
// current tab (D19) and a grid of cards. Keys (D30): "/" focuses search; Esc clears a
// non-empty search, otherwise closes the board; D done/reopen; Del trash/restore;
// Ctrl+Shift+C colour; Ctrl+Z undo; Ctrl+E export. Actions apply to the selected card, and do nothing
// without one. A store error is shown in the status line and the board reloads.
class BoardWindow : public QWidget {
    Q_OBJECT
public:
    static constexpr int kSearchDebounceMs = 120;

    // What survives closing and reopening the board within a session (B4).
    struct ViewState {
        noite::Tab tab = noite::Tab::Today;
        QString search;
        noite::BoardFilter filter;           // active chips
        QSet<noite::BundleId> expanded;      // open stacks
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
    // M8a: Export… / Ctrl+E. The note open over the board (saved first) or the selected card,
    // to the path the picker returns: .pdf is a PDF, anything else HTML (".html" added when
    // there is no extension).
    void exportSelected();
    // The picker gets the suggested path and returns the chosen one ("" cancels); the
    // reporter shows a failed export. Defaults: a Save dialog and a message box.
    void setExportPathPicker(std::function<QString(const QString& suggested)> picker);
    void setExportErrorReporter(std::function<void(const QString& message)> reporter);

    // Click or Enter opens the selected card in the overlay; N opens a blank note there.
    void openSelected();
    void openNew();

    // Bundles (M3b): click/Enter on a stack toggles it; menus rename it or take a card out.
    void toggleSelectedStack();
    void renameSelectedBundle(const QString& name);
    void removeSelectedFromBundle();
    // "Unbundle" on a stack: every member leaves and the bundle goes. Undoable.
    void unbundleSelected();
    // A card dropped onto the card or stack at `targetRow`, or onto empty space (-1): bundles
    // it, moves it into that bundle, or takes it out of its own. Undoable; no-op if nothing changes.
    void dropCard(const noite::ScratchId& dragged, int targetRow);

    // Empty while a stack is selected.
    std::optional<noite::ScratchId> selectedId() const;
    std::optional<noite::BundleId> selectedStack() const;
    UndoLog& undoLog() { return undo_; }
    NoteOverlay& overlay() const { return *overlay_; }
    BoardModel& model() const { return *model_; }
    QListView* grid() const { return grid_; }
    QLineEdit* searchField() const { return search_; }
    QLabel* versionLabel() const { return version_; }
    TabStrip* tabStrip() const { return tabs_; }
    ChipBar* chipBar() const { return chips_; }
    QString statusText() const;
    // "No notes" / "Nothing matches" while the grid is empty; empty otherwise.
    QString emptyText() const;

signals:
    // The board is closing (after an open note was saved); `state` is what to restore.
    void closing(BoardWindow::ViewState state);
    // Before a card's note is opened or acted on, so a capture holding it can save and close.
    void aboutToChange(const noite::ScratchId& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

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
    void escapeOnGrid();
    void selectStack(const noite::BundleId& bundle);
    QString bundleName(const noite::BundleId& bundle) const; // null if the bundle no longer exists
    std::optional<noite::Bundle> bundleRecord(const noite::BundleId& bundle) const;
    // Undo of leaving a bundle: back into `bundle` (re-created with its id, name and time if
    // the action emptied it), or out of any bundle if it had none.
    void rejoin(const noite::ScratchId& id, const std::optional<noite::Bundle>& bundle);

    noite::Store& store_;
    UndoLog undo_;
    std::optional<noite::ScratchId> undoTarget_; // the card an undo entry asks to select
    BoardModel* model_;
    CardDelegate* delegate_;
    TabStrip* tabs_;
    ChipBar* chips_;
    QLineEdit* search_;
    QLabel* version_;
    QListView* grid_;
    QLabel* empty_;
    QLabel* status_;
    NoteOverlay* overlay_;
    QTimer searchDelay_;
    std::function<QString(const QString&)> pickExportPath_;
    std::function<void(const QString&)> reportExportError_;
};
