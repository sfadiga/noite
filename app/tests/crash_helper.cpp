// noite_crash_helper <logsDir> av|throw|fatal|terminate|abort|purecall|invalid: crashes on
// purpose with the log and the crash handler installed, for tst_crash.cpp. A GUI-subsystem exe,
// so no console window opens.
#include "log/Log.h"
#include "platform/CrashHandler.h"

#include <QCoreApplication>
#include <QStringList>

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
    }
    return 3;
}
