#include "NoteEdit.h"

#include "Trials.h"

#include <QKeyEvent>

NoteEdit::NoteEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setAcceptRichText(true);
}

void NoteEdit::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void NoteEdit::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);
    // The raster backing store is flushed to the screen right after this paint.
    if (trials_ && trials_->pending() && hasFocus())
        trials_->ready();
}
