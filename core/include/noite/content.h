#pragma once

// Pure helpers that derive everything noite stores alongside a note from its Qt rich-text
// HTML: plain text (search, title, tags) and the attachments it references (D7, D10).

#include <QString>
#include <QStringList>

#include <optional>

namespace noite::content {

// Text of the note, one line per paragraph/line break, images removed, trailing space trimmed.
QString plainText(const QString& html);

// First non-empty line, trimmed, cut to 60 grapheme clusters; "" if there is none.
QString deriveTitle(const QString& plainText);

// "#tag" words: '#' at the start or after whitespace or '(', then a letter, then letters,
// digits, '_' or '-'; trailing '-' dropped; lowercased. Sorted, unique.
QStringList inlineTags(const QString& plainText);

// The same rule for a tag typed into the tag field; the leading '#' is optional.
std::optional<QString> normalizeTag(const QString& raw);

// Attachment ids referenced as noite-att:<id> anywhere in the HTML. Sorted, unique.
QStringList attachmentRefs(const QString& html);

// The first <img> whose source is an attachment, in document order.
std::optional<QString> firstImageRef(const QString& html);

// No text and no attachments: a capture closed like this is discarded (D6).
bool isEmpty(const QString& html);

// The first 200 grapheme clusters, for board cards.
QString preview(const QString& plainText);

// <64 lowercase hex SHA-256>.<1-8 lowercase alphanumerics>
bool isValidAttachmentId(const QString& id);

// A title as a Windows file name, no extension (M8a X9): <>:"/\|?* and control characters
// become '_', trailing dots and spaces go, a device name (CON, NUL, COM1...) gets '_',
// at most 100 characters, "note" when nothing is left.
QString exportFileName(const QString& title);

} // namespace noite::content
