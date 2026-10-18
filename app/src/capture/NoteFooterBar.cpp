#include "capture/NoteFooterBar.h"

#include "capture/CaptureController.h"
#include "capture/NoteFooter.h"
#include "capture/TagField.h"

#include <QHBoxLayout>
#include <QLabel>

NoteFooterBar::NoteFooterBar(CaptureController& controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
    , tags_(new TagField(this))
    , inline_(new QLabel(this))
    , error_(new QLabel(this))
    , info_(new QLabel(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->addWidget(tags_, 1);
    layout->addWidget(inline_);
    layout->addWidget(error_);
    layout->addWidget(info_);
    inline_->setEnabled(false); // drawn dimmed: derived from the text, edited there
    error_->hide();

    connect(tags_, &TagField::tagsEdited, this, &NoteFooterBar::onTagsEdited);
    connect(&controller_, &CaptureController::tagsChanged, this, &NoteFooterBar::refreshTags);
    connect(&controller_, &CaptureController::dueChanged, this, &NoteFooterBar::refreshInfo);
    connect(&controller_, &CaptureController::colorChanged, this, &NoteFooterBar::refreshInfo);
    refreshTags();
    refreshInfo();
}

void NoteFooterBar::focusTags()
{
    tags_->setFocus(Qt::ShortcutFocusReason);
    tags_->end(false);
}

QString NoteFooterBar::infoText() const
{
    return info_->text();
}

QString NoteFooterBar::inlineText() const
{
    return inline_->text();
}

QString NoteFooterBar::errorText() const
{
    return error_->text();
}

void NoteFooterBar::setReadOnly(bool readOnly)
{
    tags_->setReadOnly(readOnly);
}

void NoteFooterBar::onTagsEdited()
{
    QString invalid;
    const std::optional<QStringList> tags = tags_->parsedTags(&invalid);
    if (!tags) {
        error_->setText(tr("Invalid tag: %1").arg(invalid));
        error_->show();
        return;
    }
    error_->clear();
    error_->hide();
    controller_.setManualTags(*tags);
}

void NoteFooterBar::refreshTags()
{
    if (!tags_->hasFocus()) { // never rewrite what the user is typing
        tags_->setTags(controller_.manualTags());
        error_->clear();
        error_->hide();
    }
    QStringList shown;
    for (const QString& tag : controller_.inlineTags())
        shown << "#" + tag;
    inline_->setText(shown.join(' '));
    inline_->setVisible(!shown.isEmpty());
    tags_->setCompletions(controller_.knownTags());
}

void NoteFooterBar::refreshInfo()
{
    info_->setText(noteFooterText(controller_.due(), controller_.color()));
    info_->setVisible(!info_->text().isEmpty());
}
