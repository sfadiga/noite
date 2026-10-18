#include "capture/CaptureController.h"

#include "editor/NoteEdit.h"

#include <noite/content.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QDebug>

using noite::Color;

CaptureController::CaptureController(noite::Store& store, NoteEdit& editor, std::function<QDate()> today,
                                     QObject* parent)
    : QObject(parent)
    , store_(store)
    , editor_(editor)
{
    editor_.setToday(std::move(today));
    autosave_.setSingleShot(true);
    autosave_.setInterval(kAutosaveMs);
    connect(&autosave_, &QTimer::timeout, this, &CaptureController::save);
    connect(editor_.document(), &QTextDocument::contentsChanged, this, &CaptureController::onContentsChanged);
    connect(&editor_, &NoteEdit::dueDateTyped, this, [this](QDate due) { setDue(due); });
}

void CaptureController::reset()
{
    flush();
    id_.reset();
    due_.reset();
    color_.reset();
    manualTags_.clear();
    inlineTags_.clear();
    resetting_ = true;
    editor_.clear();
    resetting_ = false;
    dirty_ = false;
    tagsDirty_ = false;
}

void CaptureController::begin()
{
    reset();
    emit dueChanged(due_);
    emit colorChanged(color_);
    emit tagsChanged();
}

void CaptureController::loadTags(const noite::Scratch& scratch)
{
    manualTags_ = scratch.manualTags;
    inlineTags_.clear();
    for (const QString& tag : scratch.tags)
        if (!scratch.manualTags.contains(tag))
            inlineTags_ << tag;
}

void CaptureController::setManualTags(const QStringList& tags)
{
    if (tags == manualTags_)
        return;
    manualTags_ = tags;
    tagsDirty_ = true;
    if (id_)
        autosave_.start();
}

QStringList CaptureController::knownTags() const
{
    try {
        return store_.listTags();
    } catch (const std::exception& error) {
        qWarning() << "capture: tags:" << error.what();
        return {};
    }
}

void CaptureController::open(const noite::ScratchId& id)
{
    reset();
    try {
        const noite::Scratch scratch = store_.get(id);
        id_ = scratch.id;
        due_ = scratch.due;
        color_ = scratch.color;
        loadTags(scratch);
        resetting_ = true;
        editor_.setHtml(scratch.html);
        editor_.moveCursor(QTextCursor::End);
        resetting_ = false;
    } catch (const std::exception& error) {
        report(error);
    }
    emit dueChanged(due_);
    emit colorChanged(color_);
    emit tagsChanged();
}

void CaptureController::onContentsChanged()
{
    if (resetting_)
        return;
    dirty_ = true;
    autosave_.start();
}

void CaptureController::flush()
{
    autosave_.stop();
    save();
}

void CaptureController::save()
{
    if (!dirty_ && !tagsDirty_)
        return;
    const QString html = editor_.toHtml();
    try {
        if (!id_) {
            if (noite::content::isEmpty(html)) {
                dirty_ = false; // pending tags wait for text
                return;
            }
            id_ = store_.createScratch(html).id;
            if (due_)
                store_.setDue(*id_, due_);
            if (color_)
                store_.setColor(*id_, color_);
            if (!manualTags_.isEmpty())
                store_.setManualTags(*id_, manualTags_);
        } else {
            if (dirty_)
                store_.updateContent(*id_, html);
            if (tagsDirty_)
                store_.setManualTags(*id_, manualTags_);
        }
        dirty_ = false;
        tagsDirty_ = false;
        loadTags(store_.get(*id_));
        emit tagsChanged();
        emit saved(*id_);
    } catch (const std::exception& error) {
        report(error);
    }
}

void CaptureController::dismiss()
{
    flush();
    if (id_) {
        try {
            if (store_.discardIfEmpty(*id_))
                id_.reset();
        } catch (const std::exception& error) {
            report(error);
        }
    }
    emit finished();
}

void CaptureController::markDoneAndClose()
{
    flush();
    if (id_) {
        try {
            store_.markDone(*id_);
        } catch (const std::exception& error) {
            report(error);
        }
    }
    emit finished();
}

std::optional<Color> CaptureController::nextColor(std::optional<Color> color)
{
    static constexpr Color order[] = {Color::Red, Color::Orange, Color::Yellow, Color::Green, Color::Blue, Color::Purple};
    if (!color)
        return order[0];
    const auto it = std::find(std::begin(order), std::end(order), *color);
    return (it + 1 == std::end(order)) ? std::nullopt : std::optional<Color>(*(it + 1));
}

void CaptureController::cycleColor()
{
    color_ = nextColor(color_);
    if (id_) {
        try {
            store_.setColor(*id_, color_);
        } catch (const std::exception& error) {
            report(error);
        }
    }
    emit colorChanged(color_);
}

void CaptureController::setDue(std::optional<QDate> due)
{
    due_ = due;
    if (id_) {
        try {
            store_.setDue(*id_, due_);
        } catch (const std::exception& error) {
            report(error);
        }
    }
    emit dueChanged(due_);
}

void CaptureController::report(const std::exception& error)
{
    qWarning() << "capture:" << error.what();
    emit errorOccurred(QString::fromUtf8(error.what()));
}
