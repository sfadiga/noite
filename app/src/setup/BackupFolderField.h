#pragma once

#include <noite/settings.h>

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QSpinBox;

// The backup folder, how often and how many to keep: shared by the setup wizard and Settings.
class BackupFolderField : public QWidget {
    Q_OBJECT

public:
    struct Values {
        std::optional<QString> folder;
        noite::BackupFrequency frequency = noite::BackupFrequency::Daily;
        int keep = 7;
    };

    // `askFolder` shows the folder picker; nullopt means cancelled (the folder stays).
    explicit BackupFolderField(std::function<std::optional<QString>()> askFolder, QWidget* parent = nullptr);

    void setValues(const Values& values);
    Values values() const;
    // Native separators, or the unset text.
    QString folderText() const;

signals:
    void changed();

private:
    void refreshFolder();

    std::function<std::optional<QString>()> askFolder_;
    std::optional<QString> folder_;
    QLabel* folderLabel_;
    QComboBox* frequency_;
    QSpinBox* keep_;
};
