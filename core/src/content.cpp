#include <noite/content.h>

#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <QTextDocumentFragment>

#include <algorithm>

namespace noite::content {

namespace {

const QString kIdPattern = QStringLiteral(R"([0-9a-f]{64}\.[a-z0-9]{1,8})");

QString firstGraphemes(const QString& text, int count)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    int seen = 0;
    qsizetype end = 0;
    while (seen < count) {
        const qsizetype next = finder.toNextBoundary();
        if (next < 0)
            return text;
        end = next;
        ++seen;
    }
    return text.left(end);
}

bool isTagChar(QChar c)
{
    return c.isLetterOrNumber() || c == '_' || c == '-';
}

QString finishTag(QString tag)
{
    while (tag.endsWith('-'))
        tag.chop(1);
    return tag.toLower();
}

QStringList sortedUnique(QStringList list)
{
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    return list;
}

} // namespace

QString plainText(const QString& html)
{
    QString text = QTextDocumentFragment::fromHtml(html).toPlainText();
    text.remove(QChar::ObjectReplacementCharacter);
    text.replace(QChar::ParagraphSeparator, '\n');
    text.replace(QChar::LineSeparator, '\n');
    text.replace(QChar::Nbsp, ' ');
    static const QRegularExpression trailing(R"(\s+$)");
    text.remove(trailing);
    return text;
}

QString deriveTitle(const QString& plainText)
{
    for (const QString& line : plainText.split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return firstGraphemes(trimmed, 60);
    }
    return {};
}

QStringList inlineTags(const QString& plainText)
{
    QStringList tags;
    const qsizetype n = plainText.size();
    for (qsizetype i = 0; i + 1 < n; ++i) {
        if (plainText[i] != '#')
            continue;
        if (i > 0 && !plainText[i - 1].isSpace() && plainText[i - 1] != '(')
            continue;
        if (!plainText[i + 1].isLetter())
            continue;
        qsizetype j = i + 1;
        while (j < n && isTagChar(plainText[j]))
            ++j;
        if (const QString tag = finishTag(plainText.mid(i + 1, j - i - 1)); !tag.isEmpty())
            tags << tag;
        i = j - 1;
    }
    return sortedUnique(tags);
}

std::optional<QString> normalizeTag(const QString& raw)
{
    QString tag = raw.trimmed();
    if (tag.startsWith('#'))
        tag.remove(0, 1);
    if (tag.isEmpty() || !tag[0].isLetter())
        return std::nullopt;
    if (!std::all_of(tag.begin(), tag.end(), isTagChar))
        return std::nullopt;
    return finishTag(tag);
}

QStringList attachmentRefs(const QString& html)
{
    static const QRegularExpression ref("noite-att:(" + kIdPattern + ")");
    QStringList ids;
    for (auto it = ref.globalMatch(html); it.hasNext();)
        ids << it.next().captured(1);
    return sortedUnique(ids);
}

std::optional<QString> firstImageRef(const QString& html)
{
    static const QRegularExpression img(R"(<img\b[^>]*\bsrc\s*=\s*["']noite-att:()" + kIdPattern + R"()["'])",
                                        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = img.match(html); m.hasMatch())
        return m.captured(1);
    return std::nullopt;
}

bool isEmpty(const QString& html)
{
    return plainText(html).trimmed().isEmpty() && attachmentRefs(html).isEmpty();
}

QString preview(const QString& plainText)
{
    return firstGraphemes(plainText, 200);
}

bool isValidAttachmentId(const QString& id)
{
    static const QRegularExpression valid("^" + kIdPattern + "$");
    return valid.match(id).hasMatch();
}

} // namespace noite::content
