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
    HotkeyField(HotkeyManager& manager, int id, QWidget* parent = nullptr);
    ~HotkeyField() override;

    // Shown as accepted, no probe.
    void setCombo(const QString& combo);
    // Marks the current combination as rejected and shows `message` beside it.
    void showError(const QString& message);
    QString combo() const { return combo_; }
    bool isAccepted() const { return accepted_; }
    // The other hotkey's current combination, for SameAsOther.
    void setOther(const QString& combo);

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
    bool accepted_ = false;
    bool recording_ = false;
    // What Esc brings back.
    QString savedStatus_;
    bool savedAccepted_ = false;
};
