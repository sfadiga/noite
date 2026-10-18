#pragma once

#include <noite/backup.h>
#include <noite/error.h>

#include <QMetaType>
#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

Q_DECLARE_METATYPE(noite::backup::Result)
Q_DECLARE_METATYPE(noite::ErrorCode)

// Runs a backup off the UI thread (B4). The scheduler talks to this interface so tests can fake it.
class BackupRunner : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Ignored while a run is in flight.
    virtual void start(const noite::backup::Job& job) = 0;
    virtual bool busy() const = 0;
    // Cancels the run in flight (if any) and blocks until it has stopped; start() works again after.
    virtual void cancelAndWait() = 0;

signals:
    // Both are delivered on the thread the runner lives on.
    void finished(noite::backup::Result result);
    void failed(noite::ErrorCode code, QString message);
};

// One QThread named "backup", started on demand and kept for later runs.
class ThreadBackupRunner final : public BackupRunner {
    Q_OBJECT
public:
    explicit ThreadBackupRunner(QObject* parent = nullptr);
    ~ThreadBackupRunner() override;

    void start(const noite::backup::Job& job) override;
    bool busy() const override { return m_busy.load(); }
    void cancelAndWait() override;

private:
    void run(const noite::backup::Job& job);

    QThread m_thread;
    std::unique_ptr<QObject> m_worker; // lives on m_thread
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_busy{false};
};
