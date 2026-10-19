#include "setup/SettingsDialog.h"

#include "setup/BackupFolderField.h"
#include "setup/HotkeyField.h"
#include "setup/SettingsApplier.h"
#include "setup/SetupTexts.h"

#include <noite/store.h>

#include <QCheckBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(noite::Store& store, HotkeyManager& hotkeys, SettingsApplier& applier,
                               SettingsContext context, QWidget* parent)
    : QDialog(parent)
    , store_(store)
    , hotkeys_(hotkeys)
    , applier_(applier)
    , context_(std::move(context))
{
    setWindowTitle(tr("noite settings"));

    const noite::Settings initial = store_.settings();
    auto* mainLayout = new QVBoxLayout(this);

    if (context_.sync) {
        auto* syncLabel = new QLabel(syncWarningText(*context_.sync));
        syncLabel->setWordWrap(true);
        mainLayout->addWidget(syncLabel);
    }

    // Group 1: Hotkeys
    auto* hotkeysGroup = new QGroupBox(tr("Hotkeys"));
    auto* hotkeysLayout = new QFormLayout(hotkeysGroup);
    captureField_ = new HotkeyField(hotkeys_, HotkeyManager::kCaptureId);
    boardField_ = new HotkeyField(hotkeys_, HotkeyManager::kBoardId);
    captureField_->setOther(initial.boardHotkey);
    captureField_->setCombo(initial.captureHotkey);
    boardField_->setOther(initial.captureHotkey);
    boardField_->setCombo(initial.boardHotkey);
    connect(captureField_, &HotkeyField::changed, this, [this] { boardField_->setOther(captureField_->combo()); });
    connect(boardField_, &HotkeyField::changed, this, [this] { captureField_->setOther(boardField_->combo()); });
    hotkeysLayout->addRow(tr("New note"), captureField_);
    hotkeysLayout->addRow(tr("Board"), boardField_);
    mainLayout->addWidget(hotkeysGroup);

    // Group 2: Notes
    auto* notesGroup = new QGroupBox(tr("Notes"));
    auto* notesLayout = new QFormLayout(notesGroup);
    trashDays_ = new QSpinBox;
    trashDays_->setRange(1, 365);
    trashDays_->setSuffix(tr(" days"));
    trashDays_->setValue(initial.trashRetentionDays);

    dayStart_ = new QTimeEdit;
    dayStart_->setDisplayFormat(QStringLiteral("HH:mm"));
    dayStart_->setTime(initial.dayStart);

    staleDays_ = new QSpinBox;
    staleDays_->setRange(1, 90);
    staleDays_->setSuffix(tr(" days"));
    staleDays_->setValue(initial.staleDays);

    notesLayout->addRow(tr("Keep trash for"), trashDays_);
    notesLayout->addRow(tr("Day starts at"), dayStart_);
    notesLayout->addRow(tr("Resurface untouched"), staleDays_);
    mainLayout->addWidget(notesGroup);

    // Group 3: Backup
    auto* backupGroup = new QGroupBox(tr("Backup"));
    auto* backupLayout = new QVBoxLayout(backupGroup);
    backupField_ = new BackupFolderField(context_.askFolder);
    backupField_->setValues({initial.backupDir, initial.backupFrequency, initial.backupKeep});
    backupLayout->addWidget(backupField_);

    auto* backupStatusRow = new QHBoxLayout;
    lastBackupLabel_ = new QLabel;
    refreshLastBackup();
    auto* backUpNowBtn = new QPushButton(tr("Back up now"));
    connect(backUpNowBtn, &QPushButton::clicked, this, &SettingsDialog::backUpNowRequested);
    backupStatusRow->addWidget(lastBackupLabel_, 1);
    backupStatusRow->addWidget(backUpNowBtn);
    backupLayout->addLayout(backupStatusRow);
    mainLayout->addWidget(backupGroup);

    // Group 4: System
    auto* systemGroup = new QGroupBox(tr("System"));
    auto* systemLayout = new QVBoxLayout(systemGroup);
    startWithWindows_ = new QCheckBox(tr("Start with Windows"));
    startWithWindows_->setChecked(initial.startWithWindows);
    startWithWindows_->setEnabled(context_.autostartManaged);
    systemLayout->addWidget(startWithWindows_);

    auto* systemButtonsRow = new QHBoxLayout;
    auto* openLogsBtn = new QPushButton(tr("Open logs folder"));
    auto* runSetupBtn = new QPushButton(tr("Run setup again"));
    connect(openLogsBtn, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(context_.logsDir));
    });
    connect(runSetupBtn, &QPushButton::clicked, this, [this] {
        reject();
        emit runSetupRequested();
    });
    systemButtonsRow->addWidget(openLogsBtn);
    systemButtonsRow->addWidget(runSetupBtn);
    systemButtonsRow->addStretch(1);
    systemLayout->addLayout(systemButtonsRow);
    mainLayout->addWidget(systemGroup);

    errorLabel_ = new QLabel;
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    mainLayout->addWidget(errorLabel_);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->addStretch(1);
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    auto* saveBtn = new QPushButton(tr("Save"));
    saveBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::save);
    bottomRow->addWidget(cancelBtn);
    bottomRow->addWidget(saveBtn);
    mainLayout->addLayout(bottomRow);
}

void SettingsDialog::refreshLastBackup()
{
    const QDate today = context_.today ? context_.today() : QDate::currentDate();
    lastBackupLabel_->setText(tr("Last backup: %1").arg(lastBackupText(store_.jobState(), today)));
}

noite::Settings SettingsDialog::edited() const
{
    noite::Settings s = store_.settings();
    s.captureHotkey = captureField_->combo();
    s.boardHotkey = boardField_->combo();
    s.trashRetentionDays = trashDays_->value();
    s.dayStart = dayStart_->time();
    s.staleDays = staleDays_->value();
    const BackupFolderField::Values backup = backupField_->values();
    s.backupDir = backup.folder;
    s.backupFrequency = backup.frequency;
    s.backupKeep = backup.keep;
    s.startWithWindows = startWithWindows_->isChecked();
    return s;
}

QString SettingsDialog::errorText() const
{
    return errorLabel_->text();
}

void SettingsDialog::save()
{
    captureField_->cancelRecording();
    boardField_->cancelRecording();
    if (hotkeys_.isPaused())
        hotkeys_.resume();

    errorLabel_->clear();
    errorLabel_->hide();

    const std::optional<ApplyError> error = applier_.apply(edited());
    if (error) {
        if (error->hotkeyId == HotkeyManager::kCaptureId) {
            captureField_->showError(error->message);
        } else if (error->hotkeyId == HotkeyManager::kBoardId) {
            boardField_->showError(error->message);
        } else {
            errorLabel_->setText(error->message);
            errorLabel_->show();
        }
        return;
    }
    accept();
}

void SettingsDialog::reject()
{
    captureField_->cancelRecording();
    boardField_->cancelRecording();
    if (hotkeys_.isPaused())
        hotkeys_.resume();
    QDialog::reject();
}
