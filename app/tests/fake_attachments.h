#pragma once

#include "editor/AttachmentStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

// In-memory-ish AttachmentStore for editor tests: same id scheme as noite-core.
class FakeAttachments final : public AttachmentStore {
public:
    QString put(const QByteArray& bytes, const QString& ext) override
    {
        const QString id = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
            + "." + ext.toLower();
        QFile file(dir.filePath(id));
        if (file.open(QIODevice::WriteOnly))
            file.write(bytes);
        return id;
    }

    QString pathFor(const QString& id) const override { return dir.filePath(id); }

    QStringList ids() const { return QDir(dir.path()).entryList(QDir::Files); }

    QTemporaryDir dir;
};
