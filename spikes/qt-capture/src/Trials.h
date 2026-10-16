#pragma once

#include <QElapsedTimer>
#include <QString>

// Spike instrumentation: hotkey press -> focused, painted editor, one CSV row per
// dismissed capture. Same format as the Tauri spike: trial,ready_ms,text.
class Trials {
public:
    void pressed();
    void ready();
    void dismissed(const QString& text);
    bool pending() const { return pending_; }

    static QString resultsDir();
    static void logStartup(qint64 coldStartMs);

private:
    QElapsedTimer timer_;
    int trial_ = 0;
    bool pending_ = false;
    double readyMs_ = -1;
};
