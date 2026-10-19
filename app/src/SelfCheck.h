#pragma once

#include <QString>
#include <QStringList>

#include <optional>

// `noite.exe --self-check`: no window, no tray, no data folder. Verifies the version, the
// statically linked plugins and the JPEG/GIF decoders, and writes noite-self-check.txt
// next to the exe.

// The five plugin class names a static build must link; empty unless NOITE_STATIC is defined.
QStringList requiredStaticPlugins();

// className() of every statically linked plugin (QPluginLoader::staticInstances()).
QStringList presentStaticPluginClasses();

// The first failing item's text, or nullopt when everything passes.
std::optional<QString> runSelfCheck(const QStringList& required, const QStringList& present);

// Entry point for --self-check: returns the process exit code (0 ok, 1 failed).
int selfCheckMain(int argc, char** argv);
