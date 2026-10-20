#include "capture/LatencyProbe.h"

#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <windows.h>
#include <psapi.h>

namespace {

QString ms(double value)
{
    return value < 0 ? QString() : QString::number(value, 'f', 2);
}

qint64 pageFaults()
{
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters);
    return counters.PageFaultCount;
}

} // namespace

std::unique_ptr<LatencyProbe> LatencyProbe::fromEnvironment()
{
    const QString path = qEnvironmentVariable("NOITE_LATENCY_CSV");
    if (path.isEmpty())
        return nullptr;
    return std::make_unique<LatencyProbe>(path);
}

LatencyProbe::LatencyProbe(QString csvPath)
    : path_(std::move(csvPath))
{
}

void LatencyProbe::pressed()
{
    faultsAtPress_ = pageFaults();
    timer_.start();
    ++trial_;
    pending_ = true;
    phases_ = {};
}

void LatencyProbe::shown()
{
    if (pending_ && phases_.shownMs < 0)
        phases_.shownMs = elapsedMs();
}

void LatencyProbe::activated()
{
    if (pending_ && phases_.activatedMs < 0)
        phases_.activatedMs = elapsedMs();
}

void LatencyProbe::painted(bool focused)
{
    if (!pending_ || scheduled_ || (requireFocus_ && !focused))
        return;
    phases_.paintedMs = elapsedMs();
    phases_.focused = focused;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        scheduled_ = false;
        if (!pending_)
            return;
        phases_.readyMs = elapsedMs();
        phases_.faults = pageFaults() - faultsAtPress_;
        pending_ = false;
    });
}

void LatencyProbe::dismissed(const QString& text)
{
    if (!path_.isEmpty()) {
        QFile file(path_);
        const bool fresh = !file.exists();
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out.setEncoding(QStringConverter::Utf8);
            if (fresh)
                out << "trial,ready_ms,text,shown_ms,activated_ms,painted_ms,focused,faults\n";
            QString quoted = text;
            quoted.replace("\"", "\"\"");
            out << trial_ << "," << ms(phases_.readyMs) << ",\"" << quoted << "\"," << ms(phases_.shownMs) << ","
                << ms(phases_.activatedMs) << "," << ms(phases_.paintedMs) << "," << (phases_.focused ? 1 : 0) << ","
                << (phases_.faults < 0 ? QString() : QString::number(phases_.faults)) << "\n";
        }
    }
    pending_ = false;
    phases_.readyMs = -1;
}
