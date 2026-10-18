#pragma once

#include <QByteArray>
#include <QString>

namespace noite {
class Store;
}

// Where the editor puts pasted images and files: content-addressed, referenced in the
// note's HTML as noite-att:<id> (D7, D21). Implemented by StoreAttachments over noite-core.
struct AttachmentStore {
    virtual ~AttachmentStore() = default;
    // Returns the attachment id (<sha256>.<ext>). May throw noite::Error.
    virtual QString put(const QByteArray& bytes, const QString& ext) = 0;
    // The stored bytes. Throws noite::Error (e.g. AttachmentMissing).
    virtual QByteArray bytes(const QString& id) const = 0;
};

class StoreAttachments final : public AttachmentStore {
public:
    explicit StoreAttachments(noite::Store& store)
        : store_(store)
    {
    }
    QString put(const QByteArray& bytes, const QString& ext) override;
    QByteArray bytes(const QString& id) const override;

private:
    noite::Store& store_;
};
