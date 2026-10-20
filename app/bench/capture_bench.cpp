// Performance gate for the capture (D5): hotkey -> editor painted and flushed, by phase, in
// warm trials and in trimmed ones (the working set emptied first, as Windows does to an idle
// process). Summons through CaptureWindow::summon(), the code the hotkey runs; no input is
// sent. Runs on the real platform (a window flashes on screen and takes the foreground).
//   capture-bench [--warm N=20] [--trimmed N=10] [--csv PATH] [--no-keep-warm] [--floor]
//                 [--warm-wide] [--selftest-timeout]
// Exits 1 if warm or trimmed ready p95 > 100 ms, private memory > 80 MB, a trial times out,
// or the trim fails; a trimmed p95 over the 50 ms target only prints a warning.
#include "BenchUtil.h"

#include "capture/CaptureController.h"
#include "capture/CaptureWindow.h"
#include "capture/KeepWarm.h"
#include "capture/LatencyProbe.h"
#include "capture/Prewarm.h"
#include "editor/AttachmentStore.h"
#include "editor/NoteEdit.h"

#include <noite/clock.h>
#include <noite/data_dir.h>
#include <noite/store.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <vector>

namespace {

constexpr double kBudgetMs = 100;
constexpr double kTargetMs = 50;
constexpr double kMemoryBudgetMb = 80;
constexpr int kTrialTimeoutMs = 5000;
constexpr int kAfterTrialMs = 300;
constexpr int kAfterTrimMs = 500;

void idle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Runs the event loop until the probe's trial ends or the timeout passes; false on timeout.
bool waitForReady(const LatencyProbe& probe)
{
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    poll.setTimerType(Qt::PreciseTimer);
    poll.setInterval(1);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!probe.pending() || elapsed.elapsed() > kTrialTimeoutMs)
            loop.quit();
    });
    poll.start();
    loop.exec();
    return !probe.pending();
}

const char* lastPhase(const LatencyProbe::Phases& p)
{
    if (p.paintedMs >= 0)
        return "painted";
    if (p.activatedMs >= 0)
        return "activated";
    if (p.shownMs >= 0)
        return "shown";
    return "pressed";
}

struct Series {
    std::vector<double> shown, activated, painted, ready, faults;
    int focused = 0;

    void add(const LatencyProbe::Phases& p)
    {
        shown.push_back(p.shownMs);
        activated.push_back(p.activatedMs);
        painted.push_back(p.paintedMs);
        ready.push_back(p.readyMs);
        faults.push_back(double(p.faults));
        focused += p.focused ? 1 : 0;
    }
};

void printSeries(const char* mode, const Series& s)
{
    const std::pair<const char*, const std::vector<double>*> phases[] = {
        {"shown", &s.shown}, {"activated", &s.activated}, {"painted", &s.painted}, {"ready", &s.ready}};
    for (const auto& [name, values] : phases) {
        if (values->empty())
            continue;
        std::printf("mode=%s phase=%s p50=%.1f p95=%.1f max=%.1f\n", mode, name, bench::percentile(*values, 0.5),
                    bench::percentile(*values, 0.95), *std::max_element(values->begin(), values->end()));
    }
    if (!s.faults.empty())
        std::printf("mode=%s faults p50=%.0f p95=%.0f max=%.0f\n", mode, bench::percentile(s.faults, 0.5),
                    bench::percentile(s.faults, 0.95), *std::max_element(s.faults.begin(), s.faults.end()));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int warmTrials = bench::intArg(args, "--warm", 20);
    const int trimmedTrials = bench::intArg(args, "--trimmed", 10);
    const qsizetype csvAt = args.indexOf("--csv");
    const QString csvPath = csvAt >= 0 && csvAt + 1 < args.size() ? args[csvAt + 1] : QString();
    const bool keepWarm = !args.contains("--no-keep-warm");
    const bool floor = args.contains("--floor");

    QTemporaryDir tmp;
    auto store = noite::Store::open(noite::DataDir::acquire(tmp.filePath("data")), std::make_shared<noite::SystemClock>());
    StoreAttachments attachments(*store);
    auto* edit = new NoteEdit; // owned by the window
    edit->setAttachmentStore(&attachments);
    CaptureController controller(*store, *edit, [] { return QDate::currentDate(); });
    CaptureWindow window(controller, edit);
    if (const std::optional<QString> error = prewarmCapture(&window, {floor}))
        std::printf("prewarm: %s\n", qPrintable(*error));
    std::unique_ptr<KeepWarm> warm;
    if (keepWarm)
        warm = std::make_unique<KeepWarm>(&window); // not started: trimmed trials tick it themselves
    LatencyProbe probe{QString()};
    probe.setRequireFocus(false); // a CI VM may not hand the window focus
    window.setLatencyProbe(&probe);
    std::printf("keep_warm=%s floor=%s\n", keepWarm ? "on" : "off", floor ? "on" : "off");

    if (args.contains("--selftest-timeout")) { // a trial that never paints: must time out
        probe.pressed();
        if (!waitForReady(probe)) {
            std::printf("timeout: trial 1 last phase %s\n", lastPhase(probe.lastPhases()));
            return 1;
        }
        return 0;
    }

    QFile csv(csvPath);
    QTextStream out(&csv);
    if (!csvPath.isEmpty()) {
        if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("cannot write %s\n", qPrintable(csvPath));
            return 1;
        }
        out << "mode,trial,shown_ms,activated_ms,painted_ms,ready_ms,focused,faults\n";
    }

    Series warmSeries;
    Series trimmedSeries;
    const auto trial = [&](const char* mode, int n, Series& series) -> bool {
        probe.pressed();
        window.summon();
        const bool ready = waitForReady(probe);
        const LatencyProbe::Phases phases = probe.lastPhases();
        controller.dismiss();
        if (!ready) {
            std::printf("timeout: %s trial %d last phase %s\n", mode, n, lastPhase(phases));
            return false;
        }
        series.add(phases);
        if (!csvPath.isEmpty())
            out << mode << "," << n << "," << phases.shownMs << "," << phases.activatedMs << "," << phases.paintedMs
                << "," << phases.readyMs << "," << (phases.focused ? 1 : 0) << "," << phases.faults << "\n";
        idle(kAfterTrialMs);
        return true;
    };

    for (int n = 1; n <= warmTrials; ++n)
        if (!trial("warm", n, warmSeries))
            return 1;
    for (int n = 1; n <= trimmedTrials; ++n) {
        if (warm)
            warm->tick(); // trimmed since the last keep-warm tick
        if (!EmptyWorkingSet(GetCurrentProcess())) {
            std::printf("EmptyWorkingSet failed: %lu\n", GetLastError());
            return 1;
        }
        idle(kAfterTrimMs);
        if (!trial("trimmed", n, trimmedSeries))
            return 1;
    }

    printSeries("warm", warmSeries);
    printSeries("trimmed", trimmedSeries);
    const int total = int(warmSeries.ready.size() + trimmedSeries.ready.size());
    std::printf("focused=%d/%d\n", warmSeries.focused + trimmedSeries.focused, total);
    const double privateMb = bench::privateMb();
    std::printf("private_mb=%.1f\n", privateMb);
    std::printf("working_set_mb=%.1f\n", bench::workingSetMb());

    bool pass = privateMb <= kMemoryBudgetMb;
    if (!warmSeries.ready.empty())
        pass = pass && bench::percentile(warmSeries.ready, 0.95) <= kBudgetMs;
    if (!trimmedSeries.ready.empty()) {
        const double trimmedP95 = bench::percentile(trimmedSeries.ready, 0.95);
        pass = pass && trimmedP95 <= kBudgetMs;
        if (trimmedP95 > kTargetMs)
            std::printf("warning: trimmed ready p95 %.1f ms is over the %.0f ms target\n", trimmedP95, kTargetMs);
    }
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
