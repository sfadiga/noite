#include "setup/SetupWizard.h"

#include "setup/BackupFolderField.h"
#include "setup/HotkeyField.h"
#include "setup/SetupTexts.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

class HotkeyPage : public QWizardPage {
public:
    HotkeyPage(const QString& title, HotkeyManager& hotkeys, int id, HotkeyField* field,
               std::function<QString()> otherCombo, QWidget* parent = nullptr)
        : QWizardPage(parent)
        , hotkeys_(hotkeys)
        , id_(id)
        , field_(field)
        , otherCombo_(std::move(otherCombo))
    {
        setTitle(title);
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(field_);
        layout->addStretch(1);
        connect(field_, &HotkeyField::changed, this, &QWizardPage::completeChanged);
    }

    void initializePage() override
    {
        const QString other = otherCombo_();
        field_->setOther(other);
        const QString current = field_->combo();

        const auto parsed = parseHotkey(current);
        const auto parsedOther = parseHotkey(other);
        HotkeyStatus status = HotkeyStatus::Ok;
        if (!parsed)
            status = HotkeyStatus::Invalid;
        else if (parsedOther && *parsed == *parsedOther)
            status = HotkeyStatus::SameAsOther;
        else if (!(hotkeys_.isRegistered(id_) && parseHotkey(hotkeys_.combo(id_)) == parsed))
            status = hotkeys_.probe(current);

        if (status == HotkeyStatus::Ok)
            field_->setCombo(current);
        else
            field_->showError(hotkeyMessage(status, current, otherId()), status);
        emit completeChanged();
    }

    void cleanupPage() override
    {
        field_->cancelRecording();
        // The result can be ignored: this page's next validatePage() (or the finish) sets it again.
        if (field_->isAccepted())
            hotkeys_.set(id_, field_->combo());
    }

    bool isComplete() const override { return field_->isAccepted(); }

    bool validatePage() override
    {
        field_->cancelRecording();
        const HotkeyStatus status = hotkeys_.set(id_, field_->combo());
        if (status == HotkeyStatus::Ok)
            return true;
        field_->showError(hotkeyMessage(status, field_->combo(), otherId()), status);
        emit completeChanged();
        return false;
    }

private:
    int otherId() const
    {
        return id_ == HotkeyManager::kCaptureId ? HotkeyManager::kBoardId : HotkeyManager::kCaptureId;
    }

    HotkeyManager& hotkeys_;
    int id_;
    HotkeyField* field_;
    std::function<QString()> otherCombo_;
};

class TryItPage : public QWizardPage {
public:
    TryItPage(QLabel* label, std::function<QString()> captureCombo, QWidget* parent = nullptr)
        : QWizardPage(parent)
        , label_(label)
        , captureCombo_(std::move(captureCombo))
    {
        setTitle(tr("Try it"));
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(label_);
        layout->addStretch(1);
    }

    void initializePage() override { label_->setText(tr("Press %1 now.").arg(captureCombo_())); }

private:
    QLabel* label_;
    std::function<QString()> captureCombo_;
};

} // namespace

SetupWizard::SetupWizard(noite::Settings initial, HotkeyManager& hotkeys, WizardContext context, QWidget* parent)
    : QWizard(parent)
    , initial_(std::move(initial))
    , hotkeys_(hotkeys)
    , context_(std::move(context))
{
    setWindowTitle(tr("Set up noite"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoCancelButton, true);
    setButtonText(QWizard::CustomButton1, tr("Skip setup"));
    setOption(QWizard::HaveCustomButton1, true);

    // Page 1: Welcome
    auto* welcomePage = new QWizardPage;
    welcomePage->setTitle(tr("Welcome"));
    auto* welcomeLayout = new QVBoxLayout(welcomePage);
    auto* welcomeLabel =
        new QLabel(tr("Your notes live in this folder: %1. Nothing leaves your PC.")
                       .arg(QDir::toNativeSeparators(context_.dataDir)));
    welcomeLabel->setWordWrap(true);
    welcomeLayout->addWidget(welcomeLabel);
    if (context_.sync) {
        auto* warningLabel = new QLabel(syncWarningText(*context_.sync));
        warningLabel->setWordWrap(true);
        welcomeLayout->addWidget(warningLabel);
    }
    welcomeLayout->addStretch(1);
    setPage(kWelcomePage, welcomePage);

    // Page 2 & Page 3: Capture and Board hotkeys
    captureField_ = new HotkeyField(hotkeys_, HotkeyManager::kCaptureId);
    boardField_ = new HotkeyField(hotkeys_, HotkeyManager::kBoardId);
    captureField_->setOther(initial_.boardHotkey);
    captureField_->setCombo(initial_.captureHotkey);
    boardField_->setOther(initial_.captureHotkey);
    boardField_->setCombo(initial_.boardHotkey);

    setPage(kCapturePage,
            new HotkeyPage(tr("Capture hotkey"), hotkeys_, HotkeyManager::kCaptureId, captureField_,
                           [this] { return boardField_->combo(); }));
    setPage(kBoardPage,
            new HotkeyPage(tr("Board hotkey"), hotkeys_, HotkeyManager::kBoardId, boardField_,
                           [this] { return captureField_->combo(); }));

    // Page 4: Trash
    auto* trashPage = new QWizardPage;
    trashPage->setTitle(tr("Trash"));
    auto* trashLayout = new QVBoxLayout(trashPage);
    auto* trashRow = new QHBoxLayout;
    trashDays_ = new QSpinBox;
    trashDays_->setRange(1, 365);
    trashDays_->setValue(initial_.trashRetentionDays);
    trashRow->addWidget(new QLabel(tr("Keep trash for")));
    trashRow->addWidget(trashDays_);
    trashRow->addWidget(new QLabel(tr("days")));
    trashRow->addStretch(1);
    trashLayout->addLayout(trashRow);
    trashLayout->addStretch(1);
    setPage(kTrashPage, trashPage);

    // Page 5: Mornings
    auto* morningsPage = new QWizardPage;
    morningsPage->setTitle(tr("Mornings"));
    auto* morningsLayout = new QVBoxLayout(morningsPage);
    auto* dayRow = new QHBoxLayout;
    dayStart_ = new QTimeEdit;
    dayStart_->setDisplayFormat(QStringLiteral("HH:mm"));
    dayStart_->setTime(initial_.dayStart);
    dayRow->addWidget(new QLabel(tr("Day starts at")));
    dayRow->addWidget(dayStart_);
    dayRow->addStretch(1);
    morningsLayout->addLayout(dayRow);

    auto* staleRow = new QHBoxLayout;
    staleDays_ = new QSpinBox;
    staleDays_->setRange(1, 90);
    staleDays_->setValue(initial_.staleDays);
    staleRow->addWidget(new QLabel(tr("Bring back untouched notes after")));
    staleRow->addWidget(staleDays_);
    staleRow->addWidget(new QLabel(tr("days")));
    staleRow->addStretch(1);
    morningsLayout->addLayout(staleRow);
    morningsLayout->addStretch(1);
    setPage(kMorningsPage, morningsPage);

    // Page 6: Backup
    auto* backupPage = new QWizardPage;
    backupPage->setTitle(tr("Backup"));
    auto* backupLayout = new QVBoxLayout(backupPage);
    backupField_ = new BackupFolderField(context_.askFolder);
    backupField_->setValues({initial_.backupDir, initial_.backupFrequency, initial_.backupKeep});
    backupLayout->addWidget(backupField_);
    backupLayout->addStretch(1);
    setPage(kBackupPage, backupPage);

    // Page 7: Start with Windows
    auto* autostartPage = new QWizardPage;
    autostartPage->setTitle(tr("Start with Windows"));
    auto* autostartLayout = new QVBoxLayout(autostartPage);
    startWithWindows_ = new QCheckBox(tr("Start noite with Windows"));
    startWithWindows_->setChecked(initial_.startWithWindows);
    startWithWindows_->setEnabled(context_.autostartManaged);
    autostartLayout->addWidget(startWithWindows_);
    autostartLayout->addStretch(1);
    setPage(kAutostartPage, autostartPage);

    // Page 8: Try it
    tryItLabel_ = new QLabel(tr("Press %1 now.").arg(initial_.captureHotkey));
    setPage(kTryItPage, new TryItPage(tryItLabel_, [this] { return captureField_->combo(); }));

    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        setOption(QWizard::HaveCustomButton1, id != kCapturePage);
        if (id == kCapturePage)
            captureField_->setOther(boardField_->combo());
        else if (id == kBoardPage)
            boardField_->setOther(captureField_->combo());
    });
    connect(this, &QWizard::customButtonClicked, this, [this](int which) {
        if (which == QWizard::CustomButton1)
            skip();
    });
}

noite::Settings SetupWizard::collected() const
{
    noite::Settings s = initial_;
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

void SetupWizard::captureHotkeyPressed()
{
    if (currentId() != kTryItPage)
        return;
    finish(true);
}

void SetupWizard::skip()
{
    if (currentId() == kCapturePage)
        return;
    captureField_->cancelRecording();
    boardField_->cancelRecording();
    if (!hotkeys_.isRegistered(HotkeyManager::kCaptureId)
        || parseHotkey(hotkeys_.combo(HotkeyManager::kCaptureId)) != parseHotkey(captureField_->combo())) {
        const HotkeyStatus status = hotkeys_.set(HotkeyManager::kCaptureId, captureField_->combo());
        if (status != HotkeyStatus::Ok) {
            captureField_->showError(hotkeyMessage(status, captureField_->combo(), HotkeyManager::kBoardId), status);
            while (currentId() < kCapturePage)
                next();
            while (currentId() > kCapturePage)
                back();
            return;
        }
    }
    finish(false);
}

void SetupWizard::accept()
{
    finish(false);
}

void SetupWizard::reject()
{
    if (closed_)
        return;
    closed_ = true;
    captureField_->cancelRecording(); // a recording field resumes the hotkeys it paused
    boardField_->cancelRecording();
    const noite::Settings partial = collected();
    QWizard::reject();
    emit abandoned(partial);
}

void SetupWizard::showEvent(QShowEvent* event)
{
    closed_ = false;
    QWizard::showEvent(event);
}

void SetupWizard::finish(bool viaHotkey)
{
    if (closed_)
        return;
    closed_ = true;
    captureField_->cancelRecording(); // a recording field resumes the hotkeys it paused
    boardField_->cancelRecording();
    const noite::Settings result = collected();
    QWizard::done(QDialog::Accepted);
    emit finished(result, viaHotkey);
}
