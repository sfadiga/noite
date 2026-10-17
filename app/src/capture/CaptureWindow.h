#pragma once

#include <QWidget>

class CaptureController;
class NoteEdit;
class QLabel;

// The frameless, always-on-top capture window (D6). Created at startup and kept hidden;
// summon() opens a blank note centred on the monitor of the foreground window, or just
// refocuses it if it is already open. Keys: Esc saves and hides, Ctrl+Enter marks done
// and closes, Ctrl+Shift+C cycles the colour; clicking away saves and hides (D30).
class CaptureWindow : public QWidget {
    Q_OBJECT
public:
    // Takes ownership of `editor`.
    CaptureWindow(CaptureController& controller, NoteEdit* editor);

    void summon();
    NoteEdit* editor() const { return editor_; }
    QString footerText() const;

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateFooter();

    CaptureController& controller_;
    NoteEdit* editor_;
    QLabel* footer_;
};
