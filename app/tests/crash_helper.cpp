// noite_crash_helper <logsDir> <kind>: crashes on purpose with the log and the crash handler
// installed, for tst_crash.cpp (kinds: av throw fatal terminate abort purecall invalid overflow
// nullcall busy). A GUI-subsystem exe, so no console window opens.
#include "log/Log.h"
#include "platform/CrashHandler.h"

#include <QCoreApplication>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace {

// Calls its pure virtual while Base is still being constructed. The volatile pointer keeps
// the optimiser from resolving the call.
struct Base {
    Base()
    {
        Base* volatile self = this;
        self->pure();
    }
    virtual ~Base() = default;
    virtual void pure() = 0;
};

struct Derived : Base {
    void pure() override {}
};

volatile bool g_stop = false;

// Never returns while g_stop is false; the frame's use after the call keeps it from becoming a loop.
int recurse(int depth)
{
    volatile char pad[4096];
    pad[0] = char(depth);
    if (g_stop)
        return pad[0];
    return recurse(depth + 1) + pad[0];
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 3)
        return 2;
    applog::install(args[1]);
    crash::install(args[1]);

    const QString kind = args[2];
    if (kind == "av") {
        volatile int* volatile target = nullptr;
        *target = 1;
    } else if (kind == "throw") {
        throw std::runtime_error("boom");
    } else if (kind == "fatal") {
        qFatal("boom");
    } else if (kind == "terminate") {
        std::terminate();
    } else if (kind == "abort") {
        std::abort();
    } else if (kind == "purecall") {
        Derived derived;
    } else if (kind == "invalid") {
        FILE* volatile none = nullptr;
        std::fclose(none);
    } else if (kind == "overflow") {
        return recurse(0);
    } else if (kind == "nullcall") {
        void (*volatile none)() = nullptr;
        none();
    } else if (kind == "busy") {
        std::atomic<int> logged{0};
        QThread* spammer = QThread::create([&logged] {
            for (;;) {
                qInfo().noquote() << QString(80, QChar('x'));
                ++logged;
            }
        });
        spammer->start();
        while (logged < 200)
            QThread::msleep(1);
        volatile int* volatile target = nullptr;
        *target = 1;
    }
    return 3;
}
