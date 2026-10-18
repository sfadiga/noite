#include "setup/BackupFolderField.h"

#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

BackupFolderField::BackupFolderField(std::function<std::optional<QString>()> askFolder, QWidget* parent)
    : QWidget(parent), askFolder_(std::move(askFolder)), folderLabel_(new QLabel), frequency_(new QComboBox),
      keep_(new QSpinBox)
{
    folderLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderLabel_->setWordWrap(true);
    auto* change = new QPushButton(tr("Change…"));
    frequency_->addItem(tr("day"));
    frequency_->addItem(tr("week"));
    keep_->setRange(1, 999);
    keep_->setValue(7);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(tr("Folder")), 0, 0);
    layout->addWidget(folderLabel_, 0, 1);
    layout->addWidget(change, 0, 2);
    layout->addWidget(new QLabel(tr("Back up every")), 1, 0);
    layout->addWidget(frequency_, 1, 1, 1, 2, Qt::AlignLeft);
    layout->addWidget(new QLabel(tr("Keep")), 2, 0);
    layout->addWidget(keep_, 2, 1, 1, 2, Qt::AlignLeft);
    layout->setColumnStretch(1, 1);

    connect(change, &QPushButton::clicked, this, [this] {
        if (const auto folder = askFolder_()) {
            folder_ = *folder;
            refreshFolder();
            emit changed();
        }
    });
    connect(frequency_, &QComboBox::currentIndexChanged, this, &BackupFolderField::changed);
    connect(keep_, &QSpinBox::valueChanged, this, &BackupFolderField::changed);
    refreshFolder();
}

void BackupFolderField::refreshFolder()
{
    folderLabel_->setText(folderText());
}

QString BackupFolderField::folderText() const
{
    if (!folder_)
        return tr("Not set (Back up now will ask)");
    return QDir::toNativeSeparators(*folder_);
}

void BackupFolderField::setValues(const Values& values)
{
    folder_ = values.folder;
    frequency_->setCurrentIndex(values.frequency == noite::BackupFrequency::Weekly ? 1 : 0);
    keep_->setValue(values.keep); // clamped to 1-999 by the spin box
    refreshFolder();
}

BackupFolderField::Values BackupFolderField::values() const
{
    return {folder_, frequency_->currentIndex() == 1 ? noite::BackupFrequency::Weekly : noite::BackupFrequency::Daily,
            keep_->value()};
}
