#include "editor/OpenFolder.h"

#include <QDir>

QString defaultOpenFolder()
{
    return QDir::tempPath() + "/noite";
}

void clearOpenFolder(const QString& dir)
{
    QDir folder(dir);
    if (!folder.exists())
        return;
    for (const QFileInfo& entry : folder.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (entry.isDir())
            QDir(entry.absoluteFilePath()).removeRecursively();
        else
            QFile::remove(entry.absoluteFilePath());
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
