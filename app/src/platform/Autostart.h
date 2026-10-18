#pragma once

#include <QString>

#include <optional>

// Where "Start with Windows" lives (D4, M4 R5): the per-user Run key. Behind an interface so
// tests never touch the real registry.
struct AutostartRegistry {
    virtual ~AutostartRegistry() = default;
    virtual std::optional<QString> current() const = 0;
    virtual bool set(const QString& command) = 0;
    virtual bool remove() = 0;
};

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value "noite".
class RunKeyAutostart final : public AutostartRegistry {
public:
    std::optional<QString> current() const override;
    bool set(const QString& command) override;
    bool remove() override;
};

// The Run command: the quoted exe path, plus --data "<dir>" when the data folder isn't the
// default one next to the exe (compared after cleaning, case-insensitively).
QString autostartCommand(const QString& exePath, const QString& dataDir, const QString& defaultDataDir);

// Writes the command when enabled (only if it changed), removes it when disabled. Rewritten
// on every launch so a moved portable folder still starts. False if the registry refused.
bool applyAutostart(AutostartRegistry& registry, bool enabled, const QString& command);
