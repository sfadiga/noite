#include "editor/AttachmentStore.h"

#include <noite/store.h>

QString StoreAttachments::put(const QByteArray& bytes, const QString& ext)
{
    return store_.putAttachment(bytes, ext);
}

QString StoreAttachments::pathFor(const QString& id) const
{
    return store_.attachmentPath(id);
}
