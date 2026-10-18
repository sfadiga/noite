#pragma once

#include <QLineEdit>

#include <optional>

class QCompleter;
class QStringListModel;

// The manual tag field (D10, M3b C4): "#trip #work", the '#' optional, spaces or commas
// between tags. Completes the word under the cursor from the tags already in use.
class TagField : public QLineEdit {
    Q_OBJECT
public:
    explicit TagField(QWidget* parent = nullptr);

    void setTags(const QStringList& tags); // shows "#a #b"
    // Normalized, unique, sorted; nullopt (and `invalid` set) at the first invalid tag.
    // A lone '#' (half-typed) is ignored.
    std::optional<QStringList> parsedTags(QString* invalid = nullptr) const;
    void setCompletions(const QStringList& tags);
    QCompleter* completer() const { return completer_; }
    // Replaces the word under the cursor with "#<tag> ".
    void insertCompletion(const QString& tag);

signals:
    void tagsEdited(); // the user changed the text

private:
    // The word under the cursor without its '#', and where it starts.
    QString currentWord(int* start = nullptr) const;
    void updateCompletion();

    QCompleter* completer_;
    QStringListModel* completions_;
};
