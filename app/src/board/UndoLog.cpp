#include "board/UndoLog.h"

void UndoLog::push(QString label, std::function<void()> undo)
{
    entries_.append({std::move(label), std::move(undo)});
}

std::optional<QString> UndoLog::undo()
{
    if (entries_.isEmpty())
        return std::nullopt;
    const Entry entry = entries_.takeLast();
    entry.undo();
    return entry.label;
}
