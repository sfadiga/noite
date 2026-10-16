#pragma once

#include <QTextEdit>

class Trials;

// The capture editor. Reports "ready" on the first paint after it has focus.
class NoteEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit NoteEdit(QWidget* parent = nullptr);
    void setTrials(Trials* trials) { trials_ = trials; }

signals:
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    Trials* trials_ = nullptr;
};
