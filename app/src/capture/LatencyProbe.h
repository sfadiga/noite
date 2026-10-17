#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

// Measures hotkey -> editor painted with focus, for the keystroke harness
// (spikes/hotkey-latency/run-trials.ps1). Off unless NOITE_LATENCY_CSV names a file; then
// each dismissed capture appends "trial,ready_ms,text". The reading is taken one event-loop
// turn after the paint, i.e. after the backing store is flushed to the screen, so it is
// comparable with the Tauri spike's method (two animation frames).
class LatencyProbe : public QObject {
    Q_OBJECT
public:
    static std::unique_ptr<LatencyProbe> fromEnvironment();
    explicit LatencyProbe(QString csvPath);

    void pressed();
    void painted();
    void dismissed(const QString& text);

    bool pending() const { return pending_; }
    double lastReadyMs() const { return readyMs_; }

private:
    QString path_;
    QElapsedTimer timer_;
    int trial_ = 0;
    bool pending_ = false;
    bool scheduled_ = false;
    double readyMs_ = -1;
};
