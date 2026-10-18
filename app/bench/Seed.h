#pragma once

#include <QDate>

#include <functional>

namespace noite {
class Store;
}

// Fills `store` with `count` realistic notes for performance runs: 1-12 paragraphs each,
// ~10% with an image, ~30% due within [today-10, today+20], ~40% coloured, ~10% done and
// ~5% trashed, ~30% with an inline #tag, ~10% bundled with the note before, created over the
// last 120 days. `setNow` moves the store's clock before each note. Deterministic for a given
// `seed`.
void seedBoard(noite::Store& store, int count, QDate today, quint32 seed,
               const std::function<void(qint64 ms)>& setNow);
