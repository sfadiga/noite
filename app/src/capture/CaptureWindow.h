#pragma once

#include <noite/model.h>

#include <QWidget>

class CaptureController;
class LatencyProbe;
class NoteEdit;
class NoteFooterBar;

// The frameless, always-on-top capture window (D6). Created at startup and kept hidden;
// summon() opens a blank note centred on the monitor of the foreground window, or just
// refocuses it if it is already open. Keys: Esc saves and hides, Ctrl+Enter marks done
// and closes, Ctrl+Shift+C cycles the colour (D30). Clicking away leaves it open on top (D6).
class CaptureWindow : public QWidget {
    Q_OBJECT
public:
    // Takes ownership of `editor`.
    CaptureWindow(CaptureController& controller, NoteEdit* editor);

    void summon();
    // Like summon(), but opens the existing note `id` instead of a blank one.
    void summonNote(const noite::ScratchId& id);
    NoteEdit* editor() const { return editor_; }
    QString footerText() const; // due date and colour
    NoteFooterBar* footerBar() const { return footer_; }

    // Optional (harness runs only): told when the editor first paints with focus and when
    // the capture closes.
    void setLatencyProbe(LatencyProbe* probe) { probe_ = probe; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showAndFocus(); // centre on the foreground monitor (unless open), raise, focus

    CaptureController& controller_;
    NoteEdit* editor_;
    NoteFooterBar* footer_;
    LatencyProbe* probe_ = nullptr;
};
