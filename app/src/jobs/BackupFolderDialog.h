#pragma once

#include <QDialog>
#include <QString>

#include <functional>
#include <optional>

class QLabel;
class QPushButton;

namespace noite {

// "Google Drive found: <folder>\noite-backups" with a one-click choice, or a folder picker.
class BackupFolderDialog : public QDialog {
    Q_OBJECT
public:
    // `pickFolder` returns the folder the user picks, or an empty string for none.
    BackupFolderDialog(QString driveFolder, std::function<QString()> pickFolder, QWidget* parent = nullptr);

    std::optional<QString> chosenFolder() const { return m_chosen; }
    QPushButton* driveButton() const { return m_drive; }
    QPushButton* otherButton() const { return m_other; }
    QPushButton* cancelButton() const { return m_cancel; }
    QString shownPath() const;

private:
    QString m_driveFolder;
    std::function<QString()> m_pickFolder;
    std::optional<QString> m_chosen;
    QPushButton* m_drive;
    QPushButton* m_other;
    QPushButton* m_cancel;
    QLabel* m_path;
};

// The Drive dialog when Drive is detected, else the plain folder picker. Nothing if dismissed.
std::optional<QString> askBackupFolder(QWidget* parent = nullptr);

} // namespace noite
