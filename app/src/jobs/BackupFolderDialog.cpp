#include "jobs/BackupFolderDialog.h"

#include "platform/DriveFolder.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace noite {

BackupFolderDialog::BackupFolderDialog(QString driveFolder, std::function<QString()> pickFolder, QWidget* parent)
    : QDialog(parent), m_driveFolder(std::move(driveFolder)), m_pickFolder(std::move(pickFolder))
{
    setWindowTitle(tr("Back up noite"));

    auto* intro = new QLabel(tr("Google Drive found:"), this);
    m_path = new QLabel(shownPath(), this);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_drive = new QPushButton(tr("Back up to Drive"), this);
    m_drive->setDefault(true);
    m_other = new QPushButton(tr("Choose another folder…"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_drive);
    buttons->addWidget(m_other);
    buttons->addWidget(m_cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_path);
    layout->addLayout(buttons);

    connect(m_drive, &QPushButton::clicked, this, [this] {
        m_chosen = m_driveFolder;
        accept();
    });
    connect(m_other, &QPushButton::clicked, this, [this] {
        const QString picked = m_pickFolder();
        if (picked.isEmpty())
            return;
        m_chosen = picked;
        accept();
    });
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QString BackupFolderDialog::shownPath() const
{
    return QDir::toNativeSeparators(QDir(m_driveFolder).filePath("noite-backups"));
}

std::optional<QString> askBackupFolder(QWidget* parent)
{
    auto pick = [parent] {
        return QFileDialog::getExistingDirectory(parent, QObject::tr("Choose a backup folder"));
    };
    const auto drive = suggestedDriveFolder(mountedDriveVolumes(), [](const QString& p) { return QDir(p).exists(); });
    if (drive) {
        BackupFolderDialog dialog(*drive, pick, parent);
        if (dialog.exec() != QDialog::Accepted)
            return std::nullopt;
        return dialog.chosenFolder();
    }
    const QString folder = pick();
    if (folder.isEmpty())
        return std::nullopt;
    return folder;
}

} // namespace noite
