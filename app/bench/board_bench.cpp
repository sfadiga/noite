// Performance gate for the board (D5): cold open with N notes, painted and flushed, plus
// private memory after closing it. Runs on the real platform (a window flashes on screen).
//   board-bench [--notes N=2000] [--runs R=20] [--keep-hidden]
// Exits 1 if p95 > 300 ms or private memory > 80 MB.
#include "BenchUtil.h"
#include "Seed.h"

#include "board/BoardWindow.h"
#include "editor/AttachmentStore.h"

#include <noite/clock.h>
#include <noite/data_dir.h>
#include <noite/store.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QListView>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

using bench::intArg;
using bench::percentile;
using bench::privateMb;

constexpr double kBudgetMs = 300;
constexpr double kMemoryBudgetMb = 80;

// Quits `loop` one event-loop turn after the first paint of `target` (after the flush).
class FirstPaint : public QObject {
public:
    FirstPaint(QObject* target, QEventLoop& loop)
        : loop_(loop)
    {
        target->installEventFilter(this);
    }
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint && !seen_) {
            seen_ = true;
            QTimer::singleShot(0, &loop_, &QEventLoop::quit);
        }
        return false;
    }

private:
    QEventLoop& loop_;
    bool seen_ = false;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int notes = intArg(args, "--notes", 2000);
    const int runs = intArg(args, "--runs", 20);
    const bool keepHidden = args.contains("--keep-hidden");

    QTemporaryDir tmp;
    auto clock = std::make_shared<noite::FixedClock>(0);
    auto store = noite::Store::open(noite::DataDir::acquire(tmp.filePath("data")), clock);
    const QDate today = QDate::currentDate();
    seedBoard(*store, notes, today, 2026, [&](qint64 ms) { clock->set(ms); });
    clock->set(QDateTime::currentMSecsSinceEpoch());
    StoreAttachments attachments(*store);
    std::printf("seeded %d notes\n", notes);

    std::vector<double> times;
    std::unique_ptr<BoardWindow> kept;
    for (int run = 0; run < runs; ++run) {
        QEventLoop loop;
        QElapsedTimer timer;
        timer.start();
        std::unique_ptr<BoardWindow> board;
        BoardWindow* window = nullptr;
        if (keepHidden && kept) {
            window = kept.get();
        } else {
            board = std::make_unique<BoardWindow>(*store, attachments, [today] { return today; });
            window = board.get();
        }
        if (run % 2 == 1) { // alternate runs open with a tag chip active (M3b)
            noite::BoardFilter work;
            work.tags = {"work"};
            window->restoreViewState({noite::Tab::Today, {}, work, {}});
        } else {
            window->restoreViewState({});
        }
        FirstPaint watch(window->grid()->viewport(), loop);
        window->show();
        loop.exec();
        times.push_back(double(timer.nsecsElapsed()) / 1e6);
        window->hide();
        if (keepHidden && !kept)
            kept = std::move(board);
        app.processEvents();
    }
    kept.reset();
    app.processEvents();

    const double p50 = percentile(times, 0.5);
    const double p95 = percentile(times, 0.95);
    const double max = *std::max_element(times.begin(), times.end());
    const double memory = privateMb();
    std::printf("runs=%d p50=%.1f p95=%.1f max=%.1f first=%.1f\n", runs, p50, p95, max, times.front());
    std::printf("private_mb=%.1f\n", memory);
    const bool pass = p95 <= kBudgetMs && memory <= kMemoryBudgetMb;
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
