#pragma once

class QWidget;

// Readies the hidden capture window at startup (D5): creates its native window and renders
// it once offscreen, so the first summon finds its paint path loaded. Shared by App and
// capture-bench, so the bench measures what noite does.
void prewarmCapture(QWidget* window);
