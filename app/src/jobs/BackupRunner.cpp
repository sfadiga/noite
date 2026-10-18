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

void ThreadBackupRunner::start(const backup::Job& job)
{
    if (m_busy.exchange(true)) {
        return;
    }
    m_cancel.store(false);
    if (!m_thread.isRunning()) {
        m_thread.start();
    }
    QMetaObject::invokeMethod(m_worker.get(), [this, job] { run(job); }, Qt::QueuedConnection);
}

void ThreadBackupRunner::run(const backup::Job& job)
{
    // Runs on the backup thread. Busy clears before the signal so a slot may start the next run.
    try {
        const backup::Result result = backup::run(job, &m_cancel);
        m_busy.store(false);
        emit finished(result);
    } catch (const Error& e) {
        m_busy.store(false);
        emit failed(e.code(), QString::fromUtf8(e.what()));
    } catch (const std::exception& e) {
        m_busy.store(false);
        emit failed(ErrorCode::Io, QString::fromUtf8(e.what()));
    } catch (...) {
        m_busy.store(false);
        emit failed(ErrorCode::Io, QStringLiteral("unknown error"));
    }
}

void ThreadBackupRunner::cancelAndWait()
{
    m_cancel.store(true);
    m_thread.quit();
    m_thread.wait();
    // A run that was queued but never started must not fire on the next start().
    QCoreApplication::removePostedEvents(m_worker.get());
    m_busy.store(false);
}
