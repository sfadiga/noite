#include "editor/OpenFolder.h"

#include <QDir>
#include <QRegularExpression>

QString defaultOpenFolder()
{
    return QDir::tempPath() + "/noite-open";
}

void clearOpenFolder(const QString& dir)
{
    // Only the per-attachment folders noite writes (the id's first 12 hex digits): the open
    // folder isn't guaranteed to be a private temp folder.
    static const QRegularExpression chipFolder("^[0-9a-f]{12}$");
    const QDir folder(dir);
    for (const QFileInfo& entry : folder.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (chipFolder.match(entry.fileName()).hasMatch() && !entry.isSymLink())
            QDir(entry.absoluteFilePath()).removeRecursively();
    }
}

QString safeFileName(const QString& name, const QString& fallback)
{
    static const QString rejected = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(name.size());
    for (QChar c : name)
        out += (c.unicode() < 0x20 || rejected.contains(c)) ? QChar('_') : c;
    out = out.trimmed();
    while (out.endsWith('.') || out.endsWith(' '))
        out.chop(1);
    if (out.isEmpty() || out == "." || out == "..")
        return fallback;
    return out;
}
