#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <chrono>

// Idle first-summon mitigation (D5): while the capture window is hidden, render it offscreen
// every `interval`, so its paint path, fonts and glyph caches stay resident instead of being
// paged out. The Qt spike measured 97.5 ms for the first summon after 15 min idle (7 ms warm).
// NOITE_KEEP_WARM=0 turns it off (for measuring with and without).
class KeepWarm : public QObject {
    Q_OBJECT
public:
    explicit KeepWarm(QWidget* window, std::chrono::milliseconds interval = std::chrono::minutes(4),
                      QObject* parent = nullptr);

    static bool enabledByEnvironment();

    void start() { timer_.start(); }
    void stop() { timer_.stop(); }
    int ticks() const { return ticks_; }
    // One render now; the timer calls it. capture-bench calls it before each trim.
    void tick();

private:
    QPointer<QWidget> window_;
    QTimer timer_;
    int ticks_ = 0;
};
