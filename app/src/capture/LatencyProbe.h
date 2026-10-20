#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

// Measures hotkey -> editor painted with focus, for the keystroke harness
// (spikes/hotkey-latency/run-trials.ps1) and capture-bench. Off unless NOITE_LATENCY_CSV
// names a file; then each dismissed capture appends
// "trial,ready_ms,text,shown_ms,activated_ms,painted_ms,focused" (an empty path writes
// nothing). The ready reading is taken one event-loop turn after the paint, i.e. after the
// backing store is flushed to the screen, so it is comparable with the Tauri spike's method
// (two animation frames). Phases are ms since pressed(); -1 = not reached.
class LatencyProbe : public QObject {
    Q_OBJECT
public:
    struct Phases {
        double shownMs = -1;
        double activatedMs = -1;
        double paintedMs = -1;
        double readyMs = -1;
        bool focused = false;
    };

    static std::unique_ptr<LatencyProbe> fromEnvironment();
    explicit LatencyProbe(QString csvPath);

    void pressed();
    void shown();
    void activated();
    // Every editor paint. By default only a paint with focus ends the trial.
    void painted(bool focused);
    void dismissed(const QString& text);

    // false: the first paint ends the trial, focused or not (capture-bench on a CI VM).
    void setRequireFocus(bool require) { requireFocus_ = require; }

    bool pending() const { return pending_; }
    double lastReadyMs() const { return phases_.readyMs; }
    const Phases& lastPhases() const { return phases_; }

private:
    double elapsedMs() const { return timer_.nsecsElapsed() / 1e6; }

    QString path_;
    QElapsedTimer timer_;
    int trial_ = 0;
    bool pending_ = false;
    bool scheduled_ = false;
    bool requireFocus_ = true;
    Phases phases_;
};
