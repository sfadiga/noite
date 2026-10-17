#include "board/BoardWindow.h"

#include "board/BoardModel.h"
#include "board/CardDelegate.h"
#include "capture/CaptureController.h"

#include <noite/store.h>

#include <QCalendarWidget>
#include <QDebug>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QTabBar>
#include <QVBoxLayout>

using noite::ScratchId;
using noite::ScratchSummary;
using noite::Tab;

BoardWindow::BoardWindow(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                         QWidget* parent)
    : QWidget(parent)
    , store_(store)
    , model_(new BoardModel(store, std::move(today), this))
    , delegate_(new CardDelegate(&attachments, this))
    , tabs_(new QTabBar(this))
    , search_(new QLineEdit(this))
    , grid_(new QListView(this))
    , empty_(new QLabel(grid_->viewport()))
    , status_(new QLabel(this))
{
    setWindowTitle("noite");
    resize(1000, 700);

    for (const QString& name : {tr("Today"), tr("All"), tr("Completed"), tr("Trash")})
        tabs_->addTab(name);
    tabs_->setDrawBase(false);
    search_->setPlaceholderText(tr("Search  /"));
    search_->setClearButtonEnabled(true);

    grid_->setViewMode(QListView::IconMode);
    grid_->setMovement(QListView::Static);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setWrapping(true);
    grid_->setUniformItemSizes(true);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setModel(model_);
    grid_->setItemDelegate(delegate_);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid_, &QWidget::customContextMenuRequested, this, &BoardWindow::showContextMenu);

    empty_->setAlignment(Qt::AlignCenter);
    empty_->setEnabled(false); // drawn in the palette's disabled (dimmed) text colour
    empty_->hide();

    auto* top = new QHBoxLayout;
    top->addWidget(tabs_);
    top->addStretch();
    top->addWidget(search_);
    search_->setMinimumWidth(260);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 6);
    layout->addLayout(top);
    layout->addWidget(grid_, 1);
    layout->addWidget(status_);

    searchDelay_.setSingleShot(true);
    searchDelay_.setInterval(kSearchDebounceMs);
    connect(&searchDelay_, &QTimer::timeout, this, [this] { model_->setSearch(search_->text()); });
    connect(search_, &QLineEdit::textChanged, &searchDelay_, qOverload<>(&QTimer::start));
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) { model_->setTab(noite::Tab(index)); });
    connect(model_, &QAbstractItemModel::modelReset, this, &BoardWindow::onModelReset);
    connect(model_, &BoardModel::errorOccurred, this, &BoardWindow::showStatus);

    grid_->installEventFilter(this);
    grid_->viewport()->installEventFilter(this);
    search_->installEventFilter(this);

    model_->reload();
}

BoardWindow::~BoardWindow() = default;

BoardWindow::ViewState BoardWindow::viewState() const
{
    return {model_->tab(), search_->text()};
}

void BoardWindow::restoreViewState(const ViewState& state)
{
    {
        const QSignalBlocker blockTabs(tabs_);
        const QSignalBlocker blockSearch(search_);
        tabs_->setCurrentIndex(int(state.tab));
        search_->setText(state.search);
    }
    searchDelay_.stop();
    model_->setTab(state.tab);
    model_->setSearch(state.search);
}

void BoardWindow::setTab(noite::Tab tab)
{
    if (tabs_->currentIndex() == int(tab))
        model_->setTab(tab);
    else
        tabs_->setCurrentIndex(int(tab)); // currentChanged sets the model's tab
}

void BoardWindow::showStatus(const QString& text)
{
    status_->setText(text);
}

QString BoardWindow::statusText() const
{
    return status_->text();
}

std::optional<ScratchId> BoardWindow::selectedId() const
{
    const QModelIndex current = grid_->currentIndex();
    if (!current.isValid())
        return std::nullopt;
    return model_->summary(current.row()).id;
}

void BoardWindow::reload()
{
    reloadSelecting(selectedId(), grid_->currentIndex().row());
}

void BoardWindow::reloadSelecting(const std::optional<ScratchId>& id, int fallbackRow)
{
    model_->reload();
    int row = id ? model_->rowOf(*id) : -1;
    if (row < 0)
        row = std::min(fallbackRow, model_->rowCount() - 1);
    if (row >= 0)
        grid_->setCurrentIndex(model_->index(row));
}

void BoardWindow::act(const std::function<std::optional<ScratchId>(const ScratchSummary&)>& action)
{
    const QModelIndex current = grid_->currentIndex();
    if (!current.isValid())
        return;
    const int row = current.row();
    const ScratchSummary summary = model_->summary(row); // the action reloads the model
    std::optional<ScratchId> select;
    try {
        select = action(summary);
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    reloadSelecting(select ? select : summary.id, row);
}

void BoardWindow::pushUndo(const QString& label, const ScratchId& id, std::function<void()> storeCall)
{
    undo_.push(label, [this, id, storeCall = std::move(storeCall)] {
        storeCall();
        undoTarget_ = id;
    });
}

void BoardWindow::markDoneOrReopen()
{
    if (model_->tab() == Tab::Trash)
        return; // trashed notes are restored, not finished
    act([this](const ScratchSummary& s) -> std::optional<ScratchId> {
        const ScratchId id = s.id;
        if (s.status == noite::Status::Done) {
            store_.reopen(id);
            pushUndo(tr("Reopen"), id, [this, id] { store_.markDone(id); });
            showStatus(tr("Reopened"));
        } else {
            store_.markDone(id);
            pushUndo(tr("Done"), id, [this, id] { store_.reopen(id); });
            showStatus(tr("Marked done · Ctrl+Z to undo"));
        }
        return std::nullopt;
    });
}

void BoardWindow::trashOrRestore()
{
    act([this](const ScratchSummary& s) -> std::optional<ScratchId> {
        const ScratchId id = s.id;
        if (s.status == noite::Status::Trashed) {
            store_.restore(id);
            pushUndo(tr("Restore"), id, [this, id] { store_.trash(id); });
            showStatus(tr("Restored"));
        } else {
            store_.trash(id);
            pushUndo(tr("Trash"), id, [this, id] { store_.restore(id); });
            showStatus(tr("Moved to trash · Ctrl+Z to undo"));
        }
        return std::nullopt;
    });
}

void BoardWindow::cycleColor()
{
    if (const QModelIndex current = grid_->currentIndex(); current.isValid())
        setColor(CaptureController::nextColor(model_->summary(current.row()).color));
}

void BoardWindow::setColor(std::optional<noite::Color> color)
{
    act([this, color](const ScratchSummary& s) -> std::optional<ScratchId> {
        const ScratchId id = s.id;
        const std::optional<noite::Color> previous = s.color;
        store_.setColor(id, color);
        pushUndo(tr("Colour"), id, [this, id, previous] { store_.setColor(id, previous); });
        showStatus(tr("Colour changed"));
        return std::nullopt;
    });
}

void BoardWindow::setDue(std::optional<QDate> due)
{
    act([this, due](const ScratchSummary& s) -> std::optional<ScratchId> {
        const ScratchId id = s.id;
        const std::optional<QDate> previous = s.due;
        store_.setDue(id, due);
        pushUndo(tr("Due date"), id, [this, id, previous] { store_.setDue(id, previous); });
        showStatus(due ? tr("Due date set") : tr("Due date cleared"));
        return std::nullopt;
    });
}

void BoardWindow::cloneSelected()
{
    act([this](const ScratchSummary& s) -> std::optional<ScratchId> {
        const ScratchId clone = store_.cloneScratch(s.id).id;
        pushUndo(tr("Clone"), s.id, [this, clone] { store_.trash(clone); });
        showStatus(tr("Cloned"));
        return clone;
    });
}

void BoardWindow::undo()
{
    if (undo_.size() == 0) {
        showStatus(tr("Nothing to undo"));
        return;
    }
    const std::optional<ScratchId> selected = selectedId();
    const int row = grid_->currentIndex().row();
    undoTarget_.reset();
    try {
        showStatus(tr("Undone: %1").arg(*undo_.undo()));
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    reloadSelecting(undoTarget_ ? undoTarget_ : selected, row);
}

void BoardWindow::showContextMenu(const QPoint& pos)
{
    const QModelIndex index = grid_->indexAt(pos);
    if (!index.isValid())
        return;
    grid_->setCurrentIndex(index);
    const ScratchSummary s = model_->summary(index.row());

    QMenu menu(this);
    if (s.status != noite::Status::Trashed)
        menu.addAction(s.status == noite::Status::Done ? tr("Reopen") : tr("Done"), this, &BoardWindow::markDoneOrReopen);
    menu.addAction(s.status == noite::Status::Trashed ? tr("Restore") : tr("Trash"), this, &BoardWindow::trashOrRestore);
    menu.addSeparator();

    QMenu* colours = menu.addMenu(tr("Colour"));
    colours->addAction(tr("None"), this, [this] { setColor(std::nullopt); });
    for (noite::Color c : {noite::Color::Red, noite::Color::Orange, noite::Color::Yellow, noite::Color::Green,
                           noite::Color::Blue, noite::Color::Purple}) {
        QString name = noite::colorName(c);
        name[0] = name[0].toUpper();
        colours->addAction(name, this, [this, c] { setColor(c); });
    }
    QMenu* due = menu.addMenu(tr("Due date"));
    due->addAction(tr("Today"), this, [this] { setDue(model_->today()); });
    due->addAction(tr("Tomorrow"), this, [this] { setDue(model_->today().addDays(1)); });
    due->addAction(tr("Pick…"), this, &BoardWindow::pickDue);
    due->addAction(tr("Clear"), this, [this] { setDue(std::nullopt); });
    menu.addSeparator();
    menu.addAction(tr("Clone"), this, &BoardWindow::cloneSelected);
    menu.exec(grid_->viewport()->mapToGlobal(pos));
}

void BoardWindow::pickDue()
{
    const QModelIndex current = grid_->currentIndex();
    if (!current.isValid())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Due date"));
    auto* calendar = new QCalendarWidget(&dialog);
    calendar->setSelectedDate(model_->summary(current.row()).due.value_or(model_->today()));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(calendar);
    connect(calendar, &QCalendarWidget::activated, &dialog, &QDialog::accept);
    connect(calendar, &QCalendarWidget::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() == QDialog::Accepted)
        setDue(calendar->selectedDate());
}

QString BoardWindow::emptyText() const
{
    if (model_->rowCount() > 0)
        return {};
    return model_->search().isEmpty() ? tr("No notes") : tr("Nothing matches");
}

void BoardWindow::onModelReset()
{
    if (model_->rowCount() > 0 && !grid_->currentIndex().isValid())
        grid_->setCurrentIndex(model_->index(0));
    empty_->setText(emptyText());
    empty_->setVisible(model_->rowCount() == 0);
    layoutEmptyLabel();
}

void BoardWindow::layoutEmptyLabel()
{
    empty_->setGeometry(grid_->viewport()->rect());
}

void BoardWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    grid_->setFocus(Qt::OtherFocusReason);
}

bool BoardWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == grid_->viewport() && event->type() == QEvent::Resize)
        layoutEmptyLabel();
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    const auto* key = static_cast<QKeyEvent*>(event);
    if (watched == search_ && key->key() == Qt::Key_Escape) {
        if (!search_->text().isEmpty()) {
            search_->clear();
            searchDelay_.stop();
            model_->setSearch({});
        } else {
            close();
        }
        return true;
    }
    if (watched == grid_) {
        if (key->key() == Qt::Key_Slash) {
            search_->setFocus(Qt::ShortcutFocusReason);
            search_->selectAll();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        }
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        if (key->key() == Qt::Key_D && mods == Qt::NoModifier) {
            markDoneOrReopen();
            return true;
        }
        if (key->key() == Qt::Key_Delete && mods == Qt::NoModifier) {
            trashOrRestore();
            return true;
        }
        if (key->key() == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            cycleColor();
            return true;
        }
        if (key->key() == Qt::Key_Z && mods == Qt::ControlModifier) {
            undo();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
