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
    // Loads an existing note (text, due date, colour) without saving it; typing continues at
    // the end. On failure reports and is left as after begin().
    void open(const noite::ScratchId& id);
    void flush();            // save now if there is an unsaved change
    void dismiss();          // Esc / click-away: save, discard if empty, finished()
    void markDoneAndClose(); // Ctrl+Enter
    void cycleColor();       // none -> red -> orange -> yellow -> green -> blue -> purple -> none
    void setDue(std::optional<QDate> due);
    // Normalized manual tags (D10). Saved on the autosave debounce; before the note exists,
    // applied right after its first save (a note with only tags is still discarded).
    void setManualTags(const QStringList& tags);

    static std::optional<noite::Color> nextColor(std::optional<noite::Color> color);

    std::optional<noite::ScratchId> currentId() const { return id_; }
    std::optional<QDate> due() const { return due_; }
    std::optional<noite::Color> color() const { return color_; }
    QStringList manualTags() const { return manualTags_; }
    QStringList inlineTags() const { return inlineTags_; } // from #tags in the text, as last saved
    QStringList knownTags() const;                         // every tag in use, for completion

signals:
    void finished();
    void saved(noite::ScratchId id); // after each successful create or update
    void dueChanged(std::optional<QDate> due);
    void colorChanged(std::optional<noite::Color> color);
    void tagsChanged(); // on begin/open and after each save
    void errorOccurred(QString message);

private:
    void reset(); // flush, then forget the current note and clear the editor
    void onContentsChanged();
    void save();
    void report(const std::exception& error);
    void loadTags(const noite::Scratch& scratch);

    noite::Store& store_;
    NoteEdit& editor_;
    QTimer autosave_;
    std::optional<noite::ScratchId> id_;
    std::optional<QDate> due_;
    std::optional<noite::Color> color_;
    QStringList manualTags_;
    QStringList inlineTags_;
    bool dirty_ = false;
    bool tagsDirty_ = false;
    bool resetting_ = false;
};
