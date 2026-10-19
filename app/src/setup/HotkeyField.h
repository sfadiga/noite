#pragma once

#include "platform/HotkeyManager.h"

#include <QString>
#include <QWidget>

class QLabel;

// One hotkey in the setup wizard or Settings: a key box that records a combination and a status
// line beside it. Focusing the box starts recording (the manager is paused so the keys reach the
// field); an accepted combination, Esc or losing focus ends it and resumes the manager. The field
// only probes: committing the combination to the manager is the owner's job.
class HotkeyField : public QWidget {
    Q_OBJECT

public:
    // Recording while a combination is being pressed; otherwise what the combination shown
    // (combo()) is: accepted, or rejected as invalid, taken or the other hotkey's. Empty: none yet.
    enum class State { Empty, Accepted, Recording, Invalid, Taken, SameAsOther };

    HotkeyField(HotkeyManager& manager, int id, QWidget* parent = nullptr);
    ~HotkeyField() override;

    // Shown as accepted, no probe.
    void setCombo(const QString& combo);
    // Marks the current combination as rejected for `why` (not Ok) and shows `message` beside it.
    void showError(const QString& message, HotkeyStatus why = HotkeyStatus::Taken);
    QString combo() const { return combo_; }
    State state() const { return recording_ ? State::Recording : state_; }
    bool isAccepted() const { return state() == State::Accepted; }
    // The other hotkey's current combination, for SameAsOther.
    void setOther(const QString& combo);
    // Settings: a recorded combination equal to the other one is kept, as SameAsOther, so the two
    // can be swapped before Save. Otherwise (the wizard) it is refused and recording goes on.
    void setKeepsSameAsOther(bool keep) { keepsSameAsOther_ = keep; }
    void cancelRecording();

    bool isRecording() const { return recording_; }
    QString statusText() const;

signals:
    void changed();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool event(QEvent* event) override;

private:
    void startRecording();
    void endRecording(bool restore);
    void display(const QString& boxText, const QString& status);
    int otherId() const;

    HotkeyManager& manager_;
    int id_;
    QLabel* box_;
    QLabel* status_;
    QString combo_;
    QString other_;
    State state_ = State::Empty; // kept while recording: what Esc brings back
    bool recording_ = false;
    bool keepsSameAsOther_ = false;
    // The status line Esc brings back.
    QString savedStatus_;
};
