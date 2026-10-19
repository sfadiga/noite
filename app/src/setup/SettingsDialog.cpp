#include "setup/SettingsDialog.h"

#include "setup/BackupFolderField.h"
#include "setup/HotkeyField.h"
#include "setup/SettingsApplier.h"
#include "setup/SetupTexts.h"

#include <noite/store.h>

#include <QCheckBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimeEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <tuple>

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
    for (HotkeyField* field : {captureField_, boardField_})
        field->setKeepsSameAsOther(true);
    captureField_->setCombo(initial.captureHotkey);
    boardField_->setCombo(initial.boardHotkey);
    captureField_->setOther(initial.boardHotkey);
    boardField_->setOther(initial.captureHotkey);
    // A hotkey startup could not register (another program holds it) says so rather than ✓.
    for (auto [field, id, otherId] : {std::tuple{captureField_, HotkeyManager::kCaptureId, HotkeyManager::kBoardId},
                                      std::tuple{boardField_, HotkeyManager::kBoardId, HotkeyManager::kCaptureId}}) {
        if (!hotkeys_.isPaused() && !hotkeys_.isRegistered(id) && !field->combo().isEmpty())
            field->showError(hotkeyMessage(HotkeyStatus::Taken, field->combo(), otherId), HotkeyStatus::Taken);
    }
    // `edited` changed: `other` learns its combination. If `other` only clashed with it and no
    // longer does, it is accepted again without a probe (the manager may hold it, as in a swap);
    // Save does the real check.
    const auto sync = [](HotkeyField* edited, HotkeyField* other) {
        other->setOther(edited->combo());
        if (other->state() == HotkeyField::State::SameAsOther
            && parseHotkey(other->combo()) != parseHotkey(edited->combo())) {
            other->setCombo(other->combo());
        }
    };
    connect(captureField_, &HotkeyField::changed, this, [this, sync] { sync(captureField_, boardField_); });
    connect(boardField_, &HotkeyField::changed, this, [this, sync] { sync(boardField_, captureField_); });
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
        errorLabel_->clear();
        errorLabel_->hide();
        QDir().mkpath(context_.logsDir); // Explorer has nothing to open before the first log
        const QUrl url = QUrl::fromLocalFile(context_.logsDir);
        const bool opened = context_.openUrl ? context_.openUrl(url) : QDesktopServices::openUrl(url);
        if (!opened)
            showMessage(tr("The logs folder could not be opened: %1").arg(QDir::toNativeSeparators(context_.logsDir)));
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
    noite::JobState job;
    try {
        job = store_.jobState();
    } catch (const std::exception& error) {
        qWarning() << "settings: last backup:" << error.what(); // shown as never
    }
    lastBackupLabel_->setText(tr("Last backup: %1").arg(lastBackupText(job, today)));
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

void SettingsDialog::showMessage(const QString& message)
{
    errorLabel_->setText(message);
    errorLabel_->show();
}

void SettingsDialog::save()
{
    captureField_->cancelRecording(); // a recording field resumes the hotkeys it paused
    boardField_->cancelRecording();

    errorLabel_->clear();
    errorLabel_->hide();

    // The field holding the other's combination already says so; nothing is applied until they differ.
    for (const HotkeyField* field : {captureField_, boardField_}) {
        if (field->state() == HotkeyField::State::SameAsOther)
            return;
    }

    std::optional<ApplyError> error;
    try {
        error = applier_.apply(edited());
    } catch (const std::exception& failure) {
        error = ApplyError{QString::fromUtf8(failure.what())};
    }
    if (error) {
        if (error->hotkeyId == HotkeyManager::kCaptureId)
            captureField_->showError(error->message, error->status);
        else if (error->hotkeyId == HotkeyManager::kBoardId)
            boardField_->showError(error->message, error->status);
        else
            showMessage(error->message);
        return;
    }
    accept();
}

void SettingsDialog::reject()
{
    captureField_->cancelRecording(); // a recording field resumes the hotkeys it paused
    boardField_->cancelRecording();
    QDialog::reject();
}
