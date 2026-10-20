#include "board/BoardWindow.h"

#include "board/BoardModel.h"
#include "board/ChipBar.h"
#include "board/CardDelegate.h"
#include "Theme.h"
#include "board/NoteOverlay.h"
#include "board/TabStrip.h"
#include "VersionText.h"
#include "capture/CaptureController.h"

#include <noite/store.h>
#include <noite_version.h>

#include <QCalendarWidget>
#include <QDebug>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QShortcut>
#include <QVBoxLayout>

using noite::ScratchId;
using noite::ScratchSummary;
using noite::Tab;

namespace {

// In IconMode, QListView treats an internal drag onto empty space as "move the item's
// picture there" and never asks the model; card drops must always reach the model, which
// turns them into bundle changes (M3b final review C1). The layout stays Static.
class BoardGrid : public QListView {
public:
    using QListView::QListView;

protected:
    void dragMoveEvent(QDragMoveEvent* event) override { QAbstractItemView::dragMoveEvent(event); }
    void dropEvent(QDropEvent* event) override { QAbstractItemView::dropEvent(event); }
};

} // namespace

BoardWindow::BoardWindow(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                         QWidget* parent)
    : QWidget(parent)
    , store_(store)
    , model_(new BoardModel(store, today, this))
    , delegate_(new CardDelegate(&attachments, this))
    , tabs_(new TabStrip(this))
    , chips_(new ChipBar(this))
    , search_(new QLineEdit(this))
    , version_(new QLabel(boardVersionText(NOITE_VERSION), this))
    , grid_(new BoardGrid(this))
    , empty_(new QLabel(grid_->viewport()))
    , status_(new QLabel(this))
    , overlay_(new NoteOverlay(store, attachments, std::move(today), this))
{
    setWindowTitle("noite");
    resize(1000, 700);

    QFont searchFont = theme::headerFont(font());
    searchFont.setWeight(QFont::Normal);
    search_->setFont(searchFont);
    search_->setFixedHeight(tabs_->buttonHeight());
    search_->setPlaceholderText(tr("SEARCH  /"));
    search_->setClearButtonEnabled(true);

    // Muted: the theme has no secondary-text colour, so the palette's placeholder role.
    QFont versionFont = font();
    if (versionFont.pointSizeF() > 0)
        versionFont.setPointSizeF(versionFont.pointSizeF() - 1);
    else if (versionFont.pixelSize() > 1)
        versionFont.setPixelSize(versionFont.pixelSize() - 1);
    version_->setFont(versionFont);
    version_->setForegroundRole(QPalette::PlaceholderText);
    version_->setToolTip(versionToolTip());
    version_->setFocusPolicy(Qt::NoFocus);

    grid_->setViewMode(QListView::IconMode);
    grid_->setMovement(QListView::Static);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setWrapping(true);
    grid_->setUniformItemSizes(true);
    grid_->setSelectionMode(QAbstractItemView::SingleSelection);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setModel(model_);
    grid_->setItemDelegate(delegate_);
    grid_->setDragEnabled(true);
    grid_->setDragDropMode(QAbstractItemView::DragDrop);
    grid_->setDefaultDropAction(Qt::MoveAction);
    grid_->setDropIndicatorShown(true);
    grid_->viewport()->setAcceptDrops(true);
    grid_->setDragDropOverwriteMode(true); // a drop near a card's edge is onto the card
    // Queued: the store calls reset the model, which must not happen inside Qt's dropEvent.
    connect(model_, &BoardModel::dropRequested, this, &BoardWindow::dropCard, Qt::QueuedConnection);
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(grid_, &QWidget::customContextMenuRequested, this, &BoardWindow::showContextMenu);
    connect(grid_, &QAbstractItemView::clicked, this, &BoardWindow::openSelected);
    connect(overlay_, &NoteOverlay::closed, this, [this](const std::optional<ScratchId>& id) {
        reloadSelecting(id ? id : selectedId(), grid_->currentIndex().row());
        grid_->setFocus(Qt::OtherFocusReason);
    });
    connect(&overlay_->controller(), &CaptureController::errorOccurred, this, &BoardWindow::showStatus);

    empty_->setAlignment(Qt::AlignCenter);
    empty_->setEnabled(false); // drawn in the palette's disabled (dimmed) text colour
    empty_->hide();

    auto* top = new QHBoxLayout;
    top->addWidget(tabs_);
    top->addStretch();
    top->addWidget(search_);
    search_->setMinimumWidth(260);
    top->addWidget(version_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 6);
    layout->addLayout(top);
    layout->addWidget(chips_);
    layout->addWidget(grid_, 1);
    connect(chips_, &ChipBar::filterChanged, this, [this](const noite::BoardFilter& filter) {
        model_->setFilter(filter);
        grid_->setFocus(Qt::OtherFocusReason);
    });
    layout->addWidget(status_);

    searchDelay_.setSingleShot(true);
    searchDelay_.setInterval(kSearchDebounceMs);
    connect(&searchDelay_, &QTimer::timeout, this, [this] { model_->setSearch(search_->text()); });
    connect(search_, &QLineEdit::textChanged, &searchDelay_, qOverload<>(&QTimer::start));
    connect(tabs_, &TabStrip::currentChanged, this, [this](int index) { model_->setTab(noite::Tab(index)); });
    // Ctrl+1..4: Today, All, Completed, Trash; not while a note is open over the board.
    for (const noite::Tab tab : {noite::Tab::Today, noite::Tab::All, noite::Tab::Completed, noite::Tab::Trash}) {
        auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + int(tab))), this);
        connect(shortcut, &QShortcut::activated, this, [this, tab] {
            if (!overlay_->isOpen())
                setTab(tab);
        });
    }
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
    return {model_->tab(), search_->text(), model_->filter(), model_->expanded()};
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
    model_->setView(state.tab, state.search, state.filter, state.expanded);
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
    if (!current.isValid() || model_->isStack(current.row()))
        return std::nullopt;
    return model_->summary(current.row()).id;
}

std::optional<noite::BundleId> BoardWindow::selectedStack() const
{
    const QModelIndex current = grid_->currentIndex();
    if (!current.isValid() || !model_->isStack(current.row()))
        return std::nullopt;
    return model_->row(current.row()).bundleId;
}

void BoardWindow::openSelected()
{
    const QModelIndex current = grid_->currentIndex();
    if (!current.isValid())
        return;
    if (model_->isStack(current.row())) {
        toggleSelectedStack();
        return;
    }
    const ScratchSummary s = model_->summary(current.row()); // aboutToChange may reload the model
    emit aboutToChange(s.id);
    overlay_->openNote(s.id, s.status == noite::Status::Trashed);
}

void BoardWindow::toggleSelectedStack()
{
    const std::optional<noite::BundleId> bundle = selectedStack();
    if (!bundle)
        return;
    model_->setExpanded(*bundle, !model_->isExpanded(*bundle));
    selectStack(*bundle);
}

void BoardWindow::selectStack(const noite::BundleId& bundle)
{
    if (const int row = model_->rowOfStack(bundle); row >= 0)
        grid_->setCurrentIndex(model_->index(row));
}

std::optional<noite::Bundle> BoardWindow::bundleRecord(const noite::BundleId& bundle) const
{
    for (const noite::Bundle& b : store_.listBundles())
        if (b.id == bundle)
            return b;
    return std::nullopt;
}

QString BoardWindow::bundleName(const noite::BundleId& bundle) const
{
    const std::optional<noite::Bundle> b = bundleRecord(bundle);
    return b ? b->name : QString();
}

void BoardWindow::renameSelectedBundle(const QString& name)
{
    const std::optional<noite::BundleId> bundle = selectedStack();
    if (!bundle)
        return;
    if (name.trimmed().isEmpty()) {
        showStatus(tr("A bundle needs a name"));
        return;
    }
    try {
        store_.renameBundle(*bundle, name.trimmed());
        showStatus(tr("Renamed to %1").arg(name.trimmed()));
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    model_->reload();
    selectStack(*bundle);
}

void BoardWindow::rejoin(const ScratchId& id, const std::optional<noite::Bundle>& bundle)
{
    if (!bundle) {
        store_.moveToBundle(id, std::nullopt);
        return;
    }
    store_.recreateBundle(*bundle); // no-op unless the action emptied (and so deleted) it
    store_.moveToBundle(id, bundle->id);
}

void BoardWindow::unbundleSelected()
{
    const std::optional<noite::BundleId> stack = selectedStack();
    if (!stack)
        return;
    const int fallback = grid_->currentIndex().row();
    std::optional<ScratchId> select;
    try {
        const std::optional<noite::Bundle> saved = bundleRecord(*stack);
        const QStringList members = store_.dissolveBundle(*stack);
        if (saved && !members.isEmpty()) {
            select = members.first();
            pushUndo(tr("Unbundle"), members.first(), [this, saved, members] {
                store_.recreateBundle(*saved);
                for (const ScratchId& member : members)
                    store_.moveToBundle(member, saved->id);
            });
            showStatus(tr("Unbundled %1").arg(saved->name));
        }
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    reloadSelecting(select, fallback);
}

void BoardWindow::dropCard(const ScratchId& dragged, int targetRow)
{
    const int fallback = grid_->currentIndex().row();
    try {
        const std::optional<noite::BundleId> prev = store_.get(dragged).bundleId;
        const std::optional<noite::Bundle> saved = prev ? bundleRecord(*prev) : std::nullopt;
        const QString prevName = saved ? saved->name : QString();
        auto undoPrev = [this, dragged, saved] { rejoin(dragged, saved); };

        if (targetRow < 0) { // empty space: out of its bundle
            if (!prev)
                return;
            store_.moveToBundle(dragged, std::nullopt);
            pushUndo(tr("Remove from bundle"), dragged, undoPrev);
            showStatus(tr("Removed from %1").arg(prevName));
        } else if (model_->isStack(targetRow)) {
            const noite::BundleId bundle = model_->row(targetRow).bundleId;
            if (prev == bundle)
                return;
            store_.moveToBundle(dragged, bundle);
            pushUndo(tr("Move to bundle"), dragged, undoPrev);
            showStatus(tr("Moved to %1").arg(bundleName(bundle)));
        } else {
            const ScratchSummary target = model_->row(targetRow).card;
            if (target.id == dragged || (prev && target.bundleId == prev))
                return;
            const bool targetHadBundle = target.bundleId.has_value();
            const noite::BundleId bundle = store_.bundleTogether(target.id, dragged);
            pushUndo(tr("Bundle"), dragged, [this, undoPrev, targetHadBundle, targetId = target.id] {
                undoPrev();
                if (!targetHadBundle)
                    store_.moveToBundle(targetId, std::nullopt); // the bundle the drop created goes too
            });
            showStatus(tr("Bundled into %1").arg(bundleName(bundle)));
        }
    } catch (const std::exception& error) {
        qWarning() << "board:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    reloadSelecting(dragged, fallback);
}

void BoardWindow::removeSelectedFromBundle()
{
    act([this](const ScratchSummary& s) -> std::optional<ScratchId> {
        if (!s.bundleId)
            return std::nullopt;
        const ScratchId id = s.id;
        const noite::BundleId bundle = *s.bundleId;
        const std::optional<noite::Bundle> saved = bundleRecord(bundle);
        store_.moveToBundle(id, std::nullopt);
        pushUndo(tr("Remove from bundle"), id, [this, id, saved] { rejoin(id, saved); });
        showStatus(tr("Removed from %1").arg(saved ? saved->name : QString()));
        return std::nullopt;
    });
}

void BoardWindow::openNew()
{
    overlay_->openBlank();
}

void BoardWindow::closeEvent(QCloseEvent* event)
{
    overlay_->closeAndSave();
    emit closing(viewState());
    QWidget::closeEvent(event);
}

void BoardWindow::reload()
{
    if (const std::optional<noite::BundleId> stack = selectedStack()) {
        const int row = grid_->currentIndex().row();
        model_->reload();
        if (model_->rowOfStack(*stack) >= 0)
            selectStack(*stack);
        else if (model_->rowCount() > 0)
            grid_->setCurrentIndex(model_->index(std::min(row, model_->rowCount() - 1)));
        return;
    }
    reloadSelecting(selectedId(), grid_->currentIndex().row());
}

void BoardWindow::reloadSelecting(const std::optional<ScratchId>& id, int fallbackRow)
{
    model_->reload();
    int row = id ? model_->rowOf(*id) : -1;
    if (row < 0 && id) { // a member of a collapsed bundle: select its stack
        for (int r = 0; r < model_->rowCount() && row < 0; ++r)
            if (model_->isStack(r))
                for (const ScratchSummary& m : model_->row(r).members)
                    if (m.id == *id)
                        row = r;
    }
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
    if (model_->isStack(current.row())) { // C7: nothing acts on many notes at once
        showStatus(tr("Open the bundle to act on its notes"));
        return;
    }
    const int row = current.row();
    const ScratchSummary summary = model_->summary(row); // the action reloads the model
    emit aboutToChange(summary.id);
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
    QMenu menu(this);
    if (model_->isStack(index.row())) {
        const BoardRow& stack = model_->row(index.row());
        menu.addAction(stack.expanded ? tr("Collapse") : tr("Expand"), this, &BoardWindow::toggleSelectedStack);
        menu.addAction(tr("Unbundle"), this, &BoardWindow::unbundleSelected);
        menu.addAction(tr("Rename bundle…"), this, [this, name = stack.bundleName] {
            bool ok = false;
            const QString renamed = QInputDialog::getText(this, tr("Rename bundle"), tr("Name"), QLineEdit::Normal,
                                                          name, &ok);
            if (ok)
                renameSelectedBundle(renamed);
        });
        menu.exec(grid_->viewport()->mapToGlobal(pos));
        return;
    }
    const ScratchSummary s = model_->summary(index.row());

    menu.addAction(tr("Open"), this, &BoardWindow::openSelected);
    if (s.bundleId)
        menu.addAction(tr("Remove from bundle"), this, &BoardWindow::removeSelectedFromBundle);
    menu.addSeparator();
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
    return model_->search().isEmpty() && model_->filter().isEmpty() ? tr("No notes") : tr("Nothing matches");
}

void BoardWindow::onModelReset()
{
    if (model_->rowCount() > 0 && !grid_->currentIndex().isValid())
        grid_->setCurrentIndex(model_->index(0));
    empty_->setText(emptyText());
    empty_->setVisible(model_->rowCount() == 0);
    layoutEmptyLabel();
    try {
        tabs_->setCounts(store_.tabCounts(model_->today(), model_->search(), model_->filter()));
        chips_->setFacets(store_.chipFacets(model_->tab(), model_->today(), model_->search(), model_->filter()),
                          model_->filter());
    } catch (const std::exception& error) {
        qWarning() << "board: counts:" << error.what(); // the cards still show; counts and chips stay stale
    }
}

// Esc on the grid: collapse the bundle of a selected member, else clear the chips, else close.
void BoardWindow::escapeOnGrid()
{
    const QModelIndex current = grid_->currentIndex();
    if (current.isValid() && !model_->isStack(current.row())) {
        const BoardRow& row = model_->row(current.row());
        if (row.inBundle && row.card.bundleId) {
            const noite::BundleId bundle = *row.card.bundleId;
            model_->setExpanded(bundle, false);
            selectStack(bundle);
            return;
        }
    }
    if (!model_->filter().isEmpty()) {
        model_->setFilter({});
        return;
    }
    close();
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
            escapeOnGrid();
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
            escapeOnGrid();
            return true;
        }
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && mods == Qt::NoModifier) {
            openSelected();
            return true;
        }
        if (key->key() == Qt::Key_N && mods == Qt::NoModifier) {
            openNew();
            return true;
        }
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
