#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

// The board's Ctrl+Z (D30): a stack of labelled closures that reverse board actions.
class UndoLog {
public:
    void push(QString label, std::function<void()> undo);
    // Runs and pops the newest entry and returns its label; nullopt when empty.
    std::optional<QString> undo();
    int size() const { return int(entries_.size()); }
    void clear() { entries_.clear(); }

private:
    struct Entry {
        QString label;
        std::function<void()> undo;
    };
    QList<Entry> entries_;
};
