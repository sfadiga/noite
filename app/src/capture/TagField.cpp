#include "capture/TagField.h"

#include <noite/content.h>

#include <QAbstractItemView>
#include <QCompleter>
#include <QRegularExpression>
#include <QStringListModel>

namespace {

bool isSeparator(QChar c)
{
    return c.isSpace() || c == ',';
}

} // namespace

TagField::TagField(QWidget* parent)
    : QLineEdit(parent)
    , completer_(new QCompleter(this))
    , completions_(new QStringListModel(this))
{
    setPlaceholderText(tr("#tags"));
    completer_->setModel(completions_);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setWidget(this);
    connect(completer_, qOverload<const QString&>(&QCompleter::activated), this, &TagField::insertCompletion);
    connect(this, &QLineEdit::textEdited, this, [this] {
        updateCompletion();
        emit tagsEdited();
    });
}

void TagField::setTags(const QStringList& tags)
{
    QStringList shown;
    for (const QString& tag : tags)
        shown << "#" + tag;
    setText(shown.join(' '));
}

std::optional<QStringList> TagField::parsedTags(QString* invalid) const
{
    static const QRegularExpression separators(R"([\s,]+)");
    QStringList out;
    for (const QString& raw : text().split(separators, Qt::SkipEmptyParts)) {
        if (raw == "#")
            continue;
        const std::optional<QString> tag = noite::content::normalizeTag(raw);
        if (!tag) {
            if (invalid)
                *invalid = raw.startsWith('#') ? raw.mid(1) : raw;
            return std::nullopt;
        }
        out << *tag;
    }
    out.removeDuplicates();
    out.sort();
    return out;
}

void TagField::setCompletions(const QStringList& tags)
{
    completions_->setStringList(tags);
}

QString TagField::currentWord(int* start) const
{
    const QString t = text();
    int begin = cursorPosition();
    while (begin > 0 && !isSeparator(t[begin - 1]))
        --begin;
    if (start)
        *start = begin;
    QString word = t.mid(begin, cursorPosition() - begin);
    return word.startsWith('#') ? word.mid(1) : word;
}

void TagField::updateCompletion()
{
    const QString prefix = currentWord();
    if (prefix.isEmpty()) {
        completer_->popup()->hide();
        return;
    }
    completer_->setCompletionPrefix(prefix);
    if (completer_->completionCount() > 0)
        completer_->complete();
    else
        completer_->popup()->hide();
}

void TagField::insertCompletion(const QString& tag)
{
    int start = 0;
    currentWord(&start);
    const QString t = text();
    int end = cursorPosition();
    while (end < t.size() && !isSeparator(t[end]))
        ++end;
    const QString replacement = "#" + tag + " ";
    setText(t.left(start) + replacement + t.mid(end));
    setCursorPosition(start + int(replacement.size()));
    emit tagsEdited();
}
