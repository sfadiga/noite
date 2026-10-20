// Performance gate for the capture (D5): hotkey -> editor painted and flushed, by phase.
// Summons through CaptureWindow::summon(), the code the hotkey runs; no input is sent. Runs
// on the real platform (a window flashes on screen and takes the foreground).
//   capture-bench [--warm N=20] [--csv PATH] [--no-keep-warm] [--selftest-timeout]
// Exits 1 if the ready p95 > 100 ms, private memory > 80 MB, or a trial times out.
// The first summon after a long idle is not simulated here: in M7b its extra ~75 ms was the
// system waking (GPU/compositor), which no in-process trick reproduces
// (docs/spikes/2026-10-m7b-idle-summon.md).
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
constexpr double kMemoryBudgetMb = 80;
constexpr int kTrialTimeoutMs = 5000;
constexpr int kAfterTrialMs = 300;

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

void printStats(const char* name, const std::vector<double>& values)
{
    std::printf("phase=%s p50=%.1f p95=%.1f max=%.1f\n", name, bench::percentile(values, 0.5),
                bench::percentile(values, 0.95), *std::max_element(values.begin(), values.end()));
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int trials = bench::intArg(args, "--warm", 20);
    const qsizetype csvAt = args.indexOf("--csv");
    const QString csvPath = csvAt >= 0 && csvAt + 1 < args.size() ? args[csvAt + 1] : QString();

    QTemporaryDir tmp;
    auto store = noite::Store::open(noite::DataDir::acquire(tmp.filePath("data")), std::make_shared<noite::SystemClock>());
    StoreAttachments attachments(*store);
    auto* edit = new NoteEdit; // owned by the window
    edit->setAttachmentStore(&attachments);
    CaptureController controller(*store, *edit, [] { return QDate::currentDate(); });
    CaptureWindow window(controller, edit);
    prewarmCapture(&window);
    std::unique_ptr<KeepWarm> warm; // as App: on unless switched off
    if (!args.contains("--no-keep-warm")) {
        warm = std::make_unique<KeepWarm>(&window);
        warm->start();
    }
    LatencyProbe probe{QString()};
    probe.setRequireFocus(false); // a CI VM may not hand the window focus
    window.setLatencyProbe(&probe);

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
        out << "trial,shown_ms,activated_ms,painted_ms,ready_ms,focused,faults\n";
    }

    std::vector<double> shown, activated, painted, ready, faults;
    int focused = 0;
    for (int n = 1; n <= trials; ++n) {
        probe.pressed();
        window.summon();
        const bool done = waitForReady(probe);
        const LatencyProbe::Phases p = probe.lastPhases();
        controller.dismiss();
        if (!done) {
            std::printf("timeout: trial %d last phase %s\n", n, lastPhase(p));
            return 1;
        }
        shown.push_back(p.shownMs);
        activated.push_back(p.activatedMs);
        painted.push_back(p.paintedMs);
        ready.push_back(p.readyMs);
        faults.push_back(double(p.faults));
        focused += p.focused ? 1 : 0;
        if (!csvPath.isEmpty())
            out << n << "," << p.shownMs << "," << p.activatedMs << "," << p.paintedMs << "," << p.readyMs << ","
                << (p.focused ? 1 : 0) << "," << p.faults << "\n";
        idle(kAfterTrialMs);
    }

    printStats("shown", shown);
    printStats("activated", activated);
    printStats("painted", painted);
    printStats("ready", ready);
    std::printf("first_ready=%.1f first_faults=%.0f\n", ready.front(), faults.front());
    std::printf("focused=%d/%d\n", focused, trials);
    const double privateMb = bench::privateMb();
    std::printf("private_mb=%.1f\n", privateMb);
    const bool pass = bench::percentile(ready, 0.95) <= kBudgetMs && privateMb <= kMemoryBudgetMb;
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
