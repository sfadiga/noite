#pragma once

#include <noite/model.h>

#include <QDate>
#include <QTimer>
#include <QWidget>

#include <functional>

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
// current tab (D19) and a grid of cards. Keys: "/" focuses search; Esc clears a non-empty
// search, otherwise closes the board.
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
    void onModelReset();
    void layoutEmptyLabel();

    noite::Store& store_;
    BoardModel* model_;
    CardDelegate* delegate_;
    QTabBar* tabs_;
    QLineEdit* search_;
    QListView* grid_;
    QLabel* empty_;
    QLabel* status_;
    QTimer searchDelay_;
};
