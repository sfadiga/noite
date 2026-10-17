#pragma once

#include <QString>
#include <QTextDocumentFragment>

struct AttachmentStore;

// Pasted HTML as a fragment fit for a note (D21, D30):
// - keeps structure and emphasis (bold, italic, underline, strike, links, lists, tables,
//   headings, code), drops colours, background colours, font families and absolute sizes,
//   so text pasted as "black on white" stays readable in a dark theme;
// - images: data: and local file: sources become attachments (noite-att:<id>); remote
//   http(s) images become a link to the image, because noite never fetches (D13);
//   anything else, or anything that can't be stored, is dropped.
QTextDocumentFragment cleanPastedHtml(const QString& html, AttachmentStore* store);
