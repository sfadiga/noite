#include "radar/ReviewWindow.h"

#include "Theme.h"
#include "board/CardDelegate.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"

#include <noite/error.h>
#include <noite/store.h>

#include <QCalendarWidget>
#include <QCloseEvent>
#include <QDebug>
#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>

ReviewWindow::ReviewWindow(noite::Store& store, AttachmentStore& attachments, std::function<QDate()> today,
                           QWidget* parent)
    : QWidget(parent, Qt::Window)
    , store_(store)
    , today_(today)
    , pages_(new QStackedWidget(this))
    , header_(new QLabel(this))
    , counter_(new QLabel(this))
    , editor_(new NoteEdit(this))
    , status_(new QLabel(this))
    , summary_(new QLabel(this))
    , controller_(store, *editor_, today)
{
    setWindowTitle(tr("Morning review"));
    setFocusPolicy(Qt::StrongFocus);
    resize(640, 520);
    editor_->setAttachmentStore(&attachments);
    editor_->setReadOnly(true);
    editor_->installEventFilter(this);
    header_->setFont(theme::headerFont(font()));

    auto* review = new QWidget(pages_);
    auto* top = new QHBoxLayout;
    top->addWidget(header_, 1);
    top->addWidget(counter_);
    auto* buttons = new QHBoxLayout;
    auto addButton = [&](const QString& text, auto slot) {
        auto* button = new QPushButton(text, review);
        button->setFocusPolicy(Qt::NoFocus); // the keys go to the note
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    addButton(tr("D  Done"), [this] { done(); });
    QPushButton* snoozeButton = addButton(tr("S  Snooze ▾"), [] {});
    auto* snoozeMenu = new QMenu(snoozeButton);
    snoozeMenu->addAction(tr("1  Tomorrow"), this, [this] { snooze(today_().addDays(1)); });
    snoozeMenu->addAction(tr("2  Next Monday"), this, [this] { snooze(nextMonday(today_())); });
    snoozeMenu->addAction(tr("3  Pick…"), this, &ReviewWindow::pickSnoozeDate);
    snoozeButton->setMenu(snoozeMenu);
    addButton(tr("K  Keep"), [this] { keep(); });
    addButton(tr("L  Defer"), [this] { defer(); });
    addButton(tr("Del  Trash"), [this] { trash(); });
    auto* hint = new QLabel(tr("Enter: edit  ·  ←: back  ·  Ctrl+Z: undo  ·  Esc: finish later"), review);
    hint->setEnabled(false);
    auto* reviewLayout = new QVBoxLayout(review);
    reviewLayout->addLayout(top);
    reviewLayout->addWidget(editor_, 1);
    reviewLayout->addLayout(buttons);
    reviewLayout->addWidget(hint);
    reviewLayout->addWidget(status_);

    auto* end = new QWidget(pages_);
    auto* openBoard = new QPushButton(tr("Open board"), end);
    auto* closeButton = new QPushButton(tr("Close"), end);
    openBoard->setFocusPolicy(Qt::NoFocus); // the window keeps the keys (Left, Ctrl+Z, Esc)
    closeButton->setFocusPolicy(Qt::NoFocus);
    connect(openBoard, &QPushButton::clicked, this, &ReviewWindow::requestOpenBoard);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    summary_->setAlignment(Qt::AlignCenter);
    summary_->setFont(theme::headerFont(font()));
    auto* endButtons = new QHBoxLayout;
    endButtons->addStretch();
    endButtons->addWidget(openBoard);
    endButtons->addWidget(closeButton);
    endButtons->addStretch();
    auto* endLayout = new QVBoxLayout(end);
    endLayout->addStretch();
    endLayout->addWidget(summary_);
    endLayout->addLayout(endButtons);
    endLayout->addStretch();

    pages_->addWidget(review);
    pages_->addWidget(end);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(pages_);

    connect(editor_, &NoteEdit::escapePressed, this, [this] {
        if (editing_)
            finishEditing();
        else
            close();
    });
    connect(&controller_, &CaptureController::errorOccurred, this, &ReviewWindow::showStatus);

    if (QScreen* screen = QGuiApplication::screenAt(QCursor::pos()))
        move(screen->availableGeometry().center() - rect().center());
}

void ReviewWindow::start()
{
    try {
        queue_ = store_.reviewQueue(today_(), store_.settings().staleDays);
    } catch (const std::exception& error) {
        qWarning() << "review:" << error.what();
        queue_.clear();
        showStatus(QString::fromUtf8(error.what()));
    }
    outcomes_ = QList<std::optional<Outcome>>(queue_.size());
    decided_.clear();
    pos_ = 0;
    showCurrent();
}

std::optional<noite::ScratchId> ReviewWindow::currentId() const
{
    if (isFinished())
        return std::nullopt;
    return queue_[pos_].note.id;
}

QString ReviewWindow::headerText() const
{
    if (isFinished())
        return {};
    const noite::ReviewItem& item = queue_[pos_];
    switch (item.reason) {
    case noite::ReviewReason::Overdue:
        return item.days == 1 ? tr("OVERDUE · 1 day") : tr("OVERDUE · %1 days").arg(item.days);
    case noite::ReviewReason::DueToday:
        return tr("DUE TODAY");
    case noite::ReviewReason::Stale:
        return item.days == 1 ? tr("STALE · 1 day untouched") : tr("STALE · %1 days untouched").arg(item.days);
    case noite::ReviewReason::Undated:
        return item.days == 1 ? tr("UNDATED · 1 day old") : tr("UNDATED · %1 days old").arg(item.days);
    }
    return {};
}

QString ReviewWindow::statusText() const
{
    return status_->text();
}

QString ReviewWindow::summaryText() const
{
    int counts[5] = {};
    for (const std::optional<Outcome>& outcome : outcomes_)
        if (outcome)
            ++counts[int(outcome->kind)];
    return tr("Done %1 · Snoozed %2 · Kept %3 · Deferred %4 · Trashed %5")
        .arg(counts[0])
        .arg(counts[1])
        .arg(counts[2])
        .arg(counts[3])
        .arg(counts[4]);
}

QDate ReviewWindow::nextMonday(QDate today)
{
    const int ahead = (8 - today.dayOfWeek()) % 7;
    return today.addDays(ahead == 0 ? 7 : ahead);
}

void ReviewWindow::showStatus(const QString& text)
{
    status_->setText(text);
}

void ReviewWindow::showCurrent()
{
    snoozing_ = false;
    if (isFinished()) {
        summary_->setText(queue_.isEmpty() ? tr("Nothing to review") : summaryText());
        pages_->setCurrentIndex(1);
        setFocus(Qt::OtherFocusReason); // Left, Ctrl+Z and Esc keep working here
        return;
    }
    pages_->setCurrentIndex(0);
    const noite::ReviewItem& item = queue_[pos_];
    header_->setText(headerText());
    counter_->setText(QStringLiteral("%1 / %2").arg(position()).arg(total()));
    controller_.open(item.note.id); // loads without saving
    editor_->setReadOnly(true);
    QPalette palette = editor_->palette();
    palette.setColor(QPalette::Base, CardDelegate::cardFill(item.note.color, palette));
    editor_->setPalette(palette);
    editor_->setFocus(Qt::OtherFocusReason);
}

void ReviewWindow::decide(Kind kind, const std::function<std::function<void()>(const noite::ScratchId&)>& action)
{
    const std::optional<noite::ScratchId> id = currentId();
    if (!id || editing_)
        return;
    emit aboutToChange(*id);
    try {
        const noite::Scratch note = store_.get(*id); // NotFound: gone elsewhere
        if (note.status != noite::Status::Active) {  // finished or trashed elsewhere: not a dead end
            showStatus(tr("That note is no longer on the radar"));
            ++pos_;
            showCurrent();
            return;
        }
        std::function<void()> inverse = action(*id);
        // Every decision touches modified_at; undo puts the stale clock back too.
        auto undo = [this, id = *id, before = note.modifiedAt, inverse = std::move(inverse)] {
            inverse();
            store_.setModifiedAt(id, before);
        };
        outcomes_[pos_] = Outcome{kind, std::move(undo)};
        decided_.removeAll(pos_);
        decided_.append(pos_);
        showStatus({});
    } catch (const noite::Error& error) {
        if (error.code() != noite::ErrorCode::NotFound) {
            qWarning() << "review:" << error.what();
            showStatus(QString::fromUtf8(error.what()));
            return; // the note stays current
        }
        showStatus(tr("That note is gone"));
    } catch (const std::exception& error) {
        qWarning() << "review:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
        return;
    }
    ++pos_;
    showCurrent();
}

void ReviewWindow::done()
{
    decide(Kind::Done, [this](const noite::ScratchId& id) -> std::function<void()> {
        store_.markDone(id);
        return [this, id] { store_.reopen(id); };
    });
}

void ReviewWindow::keep()
{
    decide(Kind::Kept, [this](const noite::ScratchId& id) -> std::function<void()> {
        store_.touch(id);
        return [] {}; // decide() restores modified_at
    });
}

void ReviewWindow::defer()
{
    decide(Kind::Deferred, [this](const noite::ScratchId& id) -> std::function<void()> {
        store_.defer(id);
        return [this, id] { store_.bringBack(id); };
    });
}

void ReviewWindow::trash()
{
    decide(Kind::Trashed, [this](const noite::ScratchId& id) -> std::function<void()> {
        store_.trash(id);
        return [this, id] { store_.restore(id); };
    });
}

void ReviewWindow::snooze(QDate due)
{
    decide(Kind::Snoozed, [this, due](const noite::ScratchId& id) -> std::function<void()> {
        const std::optional<QDate> before = store_.get(id).due;
        store_.setDue(id, due);
        return [this, id, before] { store_.setDue(id, before); };
    });
}

void ReviewWindow::undoAt(int position)
{
    if (position < 0 || position >= outcomes_.size() || !outcomes_[position])
        return;
    try {
        outcomes_[position]->undo();
        showStatus({});
    } catch (const std::exception& error) {
        qWarning() << "review: undo:" << error.what();
        showStatus(QString::fromUtf8(error.what()));
    }
    outcomes_[position].reset();
    decided_.removeAll(position);
}

void ReviewWindow::back()
{
    if (editing_ || pos_ == 0)
        return;
    --pos_;
    undoAt(pos_);
    showCurrent();
}

void ReviewWindow::undo()
{
    if (editing_ || decided_.isEmpty())
        return;
    const int position = decided_.last();
    undoAt(position);
    pos_ = position;
    showCurrent();
}

void ReviewWindow::editCurrent()
{
    if (isFinished())
        return;
    if (const std::optional<noite::ScratchId> id = currentId()) {
        emit aboutToChange(*id);
        controller_.open(*id); // with what a capture holding it just saved
    }
    editing_ = true;
    editor_->setReadOnly(false);
    editor_->setFocus(Qt::OtherFocusReason);
    showStatus(tr("Editing · Esc to return to the review"));
}

void ReviewWindow::finishEditing()
{
    if (!editing_)
        return;
    controller_.flush();
    editing_ = false;
    editor_->setReadOnly(true);
    showStatus({});
}

void ReviewWindow::requestOpenBoard()
{
    emit openBoardRequested();
}

void ReviewWindow::closeEvent(QCloseEvent* event)
{
    finishEditing(); // saves what the autosave hadn't yet
    QWidget::closeEvent(event);
}

// Keys that reach the window itself: on the summary page, where the note isn't focused.
void ReviewWindow::keyPressEvent(QKeyEvent* event)
{
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Escape && !editing_) {
        close();
    } else if (event->key() == Qt::Key_Left && mods == Qt::NoModifier) {
        back();
    } else if (event->key() == Qt::Key_Z && mods == Qt::ControlModifier) {
        undo();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ReviewWindow::pickSnoozeDate()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Snooze until"));
    auto* calendar = new QCalendarWidget(&dialog);
    calendar->setMinimumDate(today_().addDays(1));
    calendar->setSelectedDate(today_().addDays(1));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(calendar);
    connect(calendar, &QCalendarWidget::activated, &dialog, &QDialog::accept);
    connect(calendar, &QCalendarWidget::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() == QDialog::Accepted)
        snooze(calendar->selectedDate());
}

bool ReviewWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != editor_ || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);
    const auto* key = static_cast<QKeyEvent*>(event);
    const auto mods = key->modifiers() & ~Qt::KeypadModifier;
    const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
    if (editing_) {
        if (enter && mods == Qt::ControlModifier) {
            finishEditing();
            return true;
        }
        return QWidget::eventFilter(watched, event); // typing, and Esc via escapePressed
    }
    if (snoozing_) {
        snoozing_ = false;
        if (key->key() == Qt::Key_1)
            snooze(today_().addDays(1));
        else if (key->key() == Qt::Key_2)
            snooze(nextMonday(today_()));
        else if (key->key() == Qt::Key_3)
            pickSnoozeDate();
        else
            showStatus({});
        return true;
    }
    if (mods == Qt::NoModifier) {
        switch (key->key()) {
        case Qt::Key_D:
            done();
            return true;
        case Qt::Key_K:
            keep();
            return true;
        case Qt::Key_L:
            defer();
            return true;
        case Qt::Key_Delete:
            trash();
            return true;
        case Qt::Key_S:
            if (!isFinished()) {
                snoozing_ = true;
                showStatus(tr("Snooze: 1 Tomorrow  ·  2 Next Monday  ·  3 Pick…"));
            }
            return true;
        case Qt::Key_Left:
            back();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            editCurrent();
            return true;
        default:
            break;
        }
    }
    if (key->key() == Qt::Key_Z && mods == Qt::ControlModifier) {
        undo();
        return true;
    }
    if (key->key() == Qt::Key_Escape)
        return QWidget::eventFilter(watched, event); // escapePressed closes
    // Read-only means read-only: no stray letters, no Ctrl+Shift+V paste. Copying, selecting
    // and moving around still work.
    const bool allowed = (mods == Qt::ControlModifier && (key->key() == Qt::Key_C || key->key() == Qt::Key_A))
        || key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_PageUp
        || key->key() == Qt::Key_PageDown || key->key() == Qt::Key_Home || key->key() == Qt::Key_End;
    return !allowed;
}
