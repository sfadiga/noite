#include "platform/Autostart.h"

#include <QDir>
#include <QSettings>

namespace {

constexpr const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const char* kValue = "noite";

QString quoted(const QString& path)
{
    return "\"" + QDir::toNativeSeparators(QDir::cleanPath(path)) + "\"";
}

} // namespace

std::optional<QString> RunKeyAutostart::current() const
{
    const QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    const QVariant value = run.value(QString::fromLatin1(kValue));
    if (!value.isValid())
        return std::nullopt;
    return value.toString();
}

bool RunKeyAutostart::set(const QString& command)
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    run.setValue(QString::fromLatin1(kValue), command);
    run.sync();
    return run.status() == QSettings::NoError;
}

bool RunKeyAutostart::remove()
{
    QSettings run(QString::fromLatin1(kRunKey), QSettings::NativeFormat);
    run.remove(QString::fromLatin1(kValue));
    run.sync();
    return run.status() == QSettings::NoError;
}

QString autostartCommand(const QString& exePath, const QString& dataDir, const QString& defaultDataDir)
{
    QString command = quoted(exePath);
    if (QDir::cleanPath(dataDir).compare(QDir::cleanPath(defaultDataDir), Qt::CaseInsensitive) != 0)
        command += " --data " + quoted(dataDir);
    return command;
}

bool applyAutostart(AutostartRegistry& registry, bool enabled, const QString& command)
{
    if (!enabled)
        return !registry.current() || registry.remove();
    if (registry.current() == command)
        return true;
    return registry.set(command);
}
