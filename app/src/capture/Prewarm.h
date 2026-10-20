#pragma once

#include <QString>

#include <optional>

class QWidget;

struct PrewarmOptions {
    bool floor = false; // pin the warmed pages with a working-set floor (M7b)
};

// Readies the hidden capture window at startup (D5): creates its native window and renders
// it once offscreen, so the first summon finds its paint path loaded. Shared by App and
// capture-bench, so the bench measures what noite does. An error message, never a throw.
std::optional<QString> prewarmCapture(QWidget* window, PrewarmOptions options);
