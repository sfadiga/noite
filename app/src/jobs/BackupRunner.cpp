#include "jobs/BackupRunner.h"

#include "log/Log.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <exception>

using namespace noite;

ThreadBackupRunner::ThreadBackupRunner(QObject* parent)
    : BackupRunner(parent)
    , m_worker(std::make_unique<QObject>())
{
    qRegisterMetaType<backup::Result>();
    qRegisterMetaType<ErrorCode>();
    m_thread.setObjectName("backup");
    m_worker->moveToThread(&m_thread);
    // Every (re)start of the thread is a new OS thread, so it is named each time.
    connect(&m_thread, &QThread::started, m_worker.get(), [] { applog::setThreadName("backup"); },
            Qt::DirectConnection);
}

ThreadBackupRunner::~ThreadBackupRunner()
{
    cancelAndWait();
}

namespace {

struct Outcome {
    bool ok = false;
    backup::Result result;
    ErrorCode code = ErrorCode::Io;
    QString message;
};

// Runs on the backup thread.
Outcome runJob(const backup::Job& job, const std::atomic<bool>* cancel)
{
    Outcome out;
    try {
        out.result = backup::run(job, cancel);
        out.ok = true;
    } catch (const Error& e) {
        out.code = e.code();
        out.message = QString::fromUtf8(e.what());
    } catch (const std::exception& e) {
        out.message = QString::fromUtf8(e.what());
    } catch (...) {
        out.message = QStringLiteral("unknown error");
    }
    return out;
}

} // namespace

void ThreadBackupRunner::start(const backup::Job& job)
{
    if (m_busy) {
        return;
    }
    m_busy = true;
    m_cancel.store(false);
    if (!m_thread.isRunning()) {
        m_thread.start();
        if (!m_thread.isRunning()) {
            m_busy = false;
            emit failed(ErrorCode::Io, QStringLiteral("could not start the backup thread"));
            return;
        }
    }
    const quint64 gen = m_generation;
    QMetaObject::invokeMethod(
        m_worker.get(),
        [this, gen, job] {
            Outcome out = runJob(job, &m_cancel);
            // Back to the runner's thread: a cancelled-and-restarted runner drops the old run's outcome.
            QMetaObject::invokeMethod(
                this,
                [this, gen, out = std::move(out)] {
                    if (gen != m_generation) {
                        return;
                    }
                    m_busy = false;
                    if (out.ok) {
                        emit finished(out.result);
                    } else {
                        emit failed(out.code, out.message);
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void ThreadBackupRunner::cancelAndWait()
{
    ++m_generation;
    m_cancel.store(true);
    m_thread.quit();
    m_thread.wait();
    // A run that was queued but never started must not fire on the next start().
    QCoreApplication::removePostedEvents(m_worker.get());
    m_busy = false;
}
