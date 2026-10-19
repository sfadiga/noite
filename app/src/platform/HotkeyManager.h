#pragma once

#include "platform/Hotkey.h"

#include <QString>

// Where global hotkeys are registered: NativeHost in the app, a fake in tests.
struct HotkeyBackend {
    // False if another program (or another host) already holds the combination.
    virtual bool registerHotkey(int id, Hotkey hotkey) = 0;
    virtual void unregisterHotkey(int id) = 0;
    virtual ~HotkeyBackend() = default;
};

enum class HotkeyStatus { Ok, Taken, Invalid, SameAsOther };

struct HotkeyOutcome {
    HotkeyStatus status = HotkeyStatus::Ok;
    int failedId = 0; // the hotkey that failed; 0 when Ok
};

// Owns the two global hotkeys (new note and board). Combinations are text in parseHotkey form
// and are compared parsed and stored canonical, so "ctrl+alt+j" equals "Ctrl+Alt+J".
class HotkeyManager {
public:
    static constexpr int kCaptureId = 1;
    static constexpr int kBoardId = 2;
    static constexpr int kProbeId = 99;

    explicit HotkeyManager(HotkeyBackend& backend);

    // Sets one hotkey, independent of the other (startup, setup wizard). On failure the previous
    // combination for `id` stays registered. SameAsOther when equal to the other id's combination.
    HotkeyStatus set(int id, const QString& combo);
    // Sets both, all or nothing (Settings): on any failure both previous combinations are back.
    HotkeyOutcome apply(const QString& capture, const QString& board);
    // Unregisters one hotkey and forgets its combination (a rollback to "nothing was set").
    void clear(int id);
    // Whether `combo` could be registered right now; leaves nothing registered. A combination
    // the manager currently holds probes as Taken (the OS refuses it): callers probe while paused.
    HotkeyStatus probe(const QString& combo);

    // While paused (a hotkey field is recording) neither hotkey is registered, so the keys reach
    // the field. resume() re-registers the current combinations.
    void pause();
    void resume();
    bool isPaused() const { return paused_; }

    QString combo(int id) const { return slot(id).combo; }
    bool isRegistered(int id) const { return slot(id).registered; }

private:
    struct Slot {
        QString combo;
        bool registered = false;
    };
    Slot& slot(int id) { return id == kCaptureId ? capture_ : board_; }
    const Slot& slot(int id) const { return id == kCaptureId ? capture_ : board_; }

    void release(int id);
    bool registerSlot(int id, const QString& combo);

    HotkeyBackend& backend_;
    Slot capture_;
    Slot board_;
    bool paused_ = false;
};

// The hotkey field's status line. `otherId` is the other hotkey, named by SameAsOther.
QString hotkeyMessage(HotkeyStatus status, const QString& combo, int otherId);
