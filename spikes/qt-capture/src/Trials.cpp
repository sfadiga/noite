#include "Trials.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

void appendLine(const QString& file, const QString& header, const QString& line)
{
    QDir().mkpath(Trials::resultsDir());
    QFile f(Trials::resultsDir() + "/" + file);
    const bool fresh = !f.exists();
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    if (fresh)
        out << header << "\n";
    out << line << "\n";
}

QString csvQuote(QString s)
{
    return "\"" + s.replace("\"", "\"\"") + "\"";
}

} // namespace

void Trials::pressed()
{
    timer_.start();
    ++trial_;
    pending_ = true;
    readyMs_ = -1;
}

void Trials::ready()
{
    if (!pending_)
        return;
    readyMs_ = timer_.nsecsElapsed() / 1e6;
    pending_ = false;
}

void Trials::dismissed(const QString& text)
{
    pending_ = false;
    const QString ready = readyMs_ < 0 ? QString() : QString::number(readyMs_, 'f', 2);
    appendLine("trials.csv", "trial,ready_ms,text",
               QString::number(trial_) + "," + ready + "," + csvQuote(text));
    readyMs_ = -1;
}

QString Trials::resultsDir()
{
    return QStringLiteral(SPIKE_DIR "/results");
}

void Trials::writeDiagnostics(const QString& html, const QStringList& keys)
{
    QDir().mkpath(resultsDir());
    QFile h(resultsDir() + "/last-note.html");
    if (h.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        h.write(html.toUtf8());
    QFile k(resultsDir() + "/last-keys.csv");
    if (k.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        k.write(("key,text,modifiers,native_vk,scan_code,result\n" + keys.join('\n') + '\n').toUtf8());
}

void Trials::logStartup(qint64 coldStartMs)
{
    appendLine("startup.csv", "cold_start_ms", QString::number(coldStartMs));
}
