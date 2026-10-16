#pragma once

#include <QWidget>

class NoteEdit;
class Trials;

// Frameless, always-on-top capture window. Created at startup and kept hidden;
// summon() positions it on the monitor of the foreground window and focuses it.
class CaptureWindow : public QWidget {
    Q_OBJECT
public:
    explicit CaptureWindow(Trials* trials);
    NoteEdit* editor() const { return editor_; }
    void summon();

signals:
    void dismissed(const QString& text);

private:
    void dismiss();

    NoteEdit* editor_;
};
