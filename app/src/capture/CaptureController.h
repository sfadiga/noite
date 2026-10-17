#pragma once

#include <noite/model.h>

#include <QDate>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

namespace noite {
class Store;
}
class NoteEdit;

// Turns editing in the capture window into store calls (D6):
// - no scratch exists until the first save with content, then updates;
// - saves are debounced (kAutosaveMs after the last change) and forced by flush();
// - dismiss() saves and discards the scratch if it ended up empty;
// - due date (from @tokens) and colour set before the first save are applied right after it.
// Store failures are reported through errorOccurred; the editor text is never cleared by one.
class CaptureController : public QObject {
    Q_OBJECT
public:
    static constexpr int kAutosaveMs = 400;

    CaptureController(noite::Store& store, NoteEdit& editor, std::function<QDate()> today, QObject* parent = nullptr);

    void begin();            // a fresh, blank capture
    void flush();            // save now if there is an unsaved change
    void dismiss();          // Esc / click-away: save, discard if empty, finished()
    void markDoneAndClose(); // Ctrl+Enter
    void cycleColor();       // none -> red -> orange -> yellow -> green -> blue -> purple -> none
    void setDue(std::optional<QDate> due);

    std::optional<noite::ScratchId> currentId() const { return id_; }
    std::optional<QDate> due() const { return due_; }
    std::optional<noite::Color> color() const { return color_; }

signals:
    void finished();
    void dueChanged(std::optional<QDate> due);
    void colorChanged(std::optional<noite::Color> color);
    void errorOccurred(QString message);

private:
    void onContentsChanged();
    void save();
    void report(const std::exception& error);

    noite::Store& store_;
    NoteEdit& editor_;
    QTimer autosave_;
    std::optional<noite::ScratchId> id_;
    std::optional<QDate> due_;
    std::optional<noite::Color> color_;
    bool dirty_ = false;
    bool resetting_ = false;
};
