#include "capture/LatencyProbe.h"

#include <QFile>
#include <QTextStream>
#include <QTimer>

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
    timer_.start();
    ++trial_;
    pending_ = true;
    readyMs_ = -1;
}

void LatencyProbe::painted()
{
    if (!pending_ || scheduled_)
        return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        scheduled_ = false;
        if (!pending_)
            return;
        readyMs_ = timer_.nsecsElapsed() / 1e6;
        pending_ = false;
    });
}

void LatencyProbe::dismissed(const QString& text)
{
    QFile file(path_);
    const bool fresh = !file.exists();
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    if (fresh)
        out << "trial,ready_ms,text\n";
    QString quoted = text;
    quoted.replace("\"", "\"\"");
    out << trial_ << "," << (readyMs_ < 0 ? QString() : QString::number(readyMs_, 'f', 2)) << ",\"" << quoted << "\"\n";
    pending_ = false;
    readyMs_ = -1;
}
