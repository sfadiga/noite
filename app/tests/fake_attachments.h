#pragma once

#include "editor/AttachmentStore.h"

#include <noite/error.h>

#include <QCryptographicHash>
#include <QHash>

// In-memory AttachmentStore for editor tests: same id scheme as noite-core.
class FakeAttachments final : public AttachmentStore {
public:
    QString put(const QByteArray& bytes, const QString& ext) override
    {
        const QString id = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
            + "." + ext.toLower();
        stored.insert(id, bytes);
        return id;
    }

    QByteArray bytes(const QString& id) const override
    {
        const auto it = stored.find(id);
        if (it == stored.end())
            throw noite::Error(noite::ErrorCode::AttachmentMissing, "attachment not found: " + id);
        return *it;
    }

    QStringList ids() const { return stored.keys(); }

    QHash<QString, QByteArray> stored;
};
