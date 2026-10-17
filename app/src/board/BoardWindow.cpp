#include "board/BoardWindow.h"

#include "board/BoardModel.h"
#include "board/CardDelegate.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QTabBar>
#include <QVBoxLayout>

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
    }
    return QWidget::eventFilter(watched, event);
}
