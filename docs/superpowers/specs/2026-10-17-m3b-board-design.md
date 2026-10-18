# M3b: bundles, filter chips and tags — design

**Date:** 2026-10-17 · **Status:** approved in conversation · **Spec:** D10, D12, D17, D18 (spec.md) · **Follows:** [M3a](2026-10-17-m3a-board-design.md)

## Goal

Finish the board: group notes into bundles that expand in place, narrow the board with filter chips (tags, colours, dates), and give every note a manual tag field next to its inline `#tags`.

## Decisions

| # | Decision | Why |
|---|---|---|
| C1 | Filtering runs in SQL in noite-core (`BoardFilter`), as search does | Counts and chip facets need the same filter for every tab. Filtering in the model would mean listing all four tabs on every keystroke. |
| C2 | A bundle is a **stack** card that **expands in place**: its members follow it in the grid | The spec's "stacked cards that expand in place" (D17); the user chose it over a separate bundle view. |
| C3 | Out of a bundle: the menu's "Remove from bundle", or drag the member onto empty grid space | The user's choice; dragging onto another card or stack moves it there. |
| C4 | The manual tag field sits in the editor footer (capture popup and board editor), with autocomplete | The user's choice; inline tags show beside it, read-only. |
| C5 | Tab counts follow search **and** chips | The counts always equal what each tab would show. |
| C6 | Chip visibility follows the faceted-search rule | A chip shows if the tab plus the search plus the *other* kinds' active chips has a note matching it, so OR within a kind never hides a chip you could add. Active chips always show. |
| C7 | `D`, `Del` and `Ctrl+Shift+C` do nothing on a stack and say so | Nothing changes many notes at once by accident. |

## Core API (noite-core)

```cpp
enum class DateChip { Overdue, DueToday, NoDate };

struct BoardFilter {              // empty = no filter
    QSet<Color> colors;           // OR within a kind
    QSet<DateChip> dates;         // OR
    QStringList tags;             // OR (normalized, lowercase)
    bool isEmpty() const;
};                                // AND across kinds

struct ChipFacets {               // what the chip bar shows, with counts
    QList<std::pair<Color, int>> colors;     // swatch order
    QList<std::pair<DateChip, int>> dates;   // Overdue, DueToday, NoDate
    QList<std::pair<QString, int>> tags;     // alphabetical
};

QList<ScratchSummary> list(Tab, QDate today, const QString& search = {}, const BoardFilter& = {}) const;
TabCounts tabCounts(QDate today, const QString& search = {}, const BoardFilter& = {}) const;
ChipFacets chipFacets(Tab, QDate today, const QString& search, const BoardFilter&) const;
QStringList listTags() const;     // every tag on a non-purged note, alphabetical
```

- Date chips: Overdue = `due_date < today`; DueToday = `due_date = today`; NoDate = `due_date IS NULL`.
- Tags match the `scratch_tags` table, inline or manual.
- `chipFacets` lists only chips with a count above 0, plus every active chip (count 0 if nothing matches).
- Existing callers keep compiling: `filter` defaults to empty.

## Board model

`BoardModel` rows are entries of two kinds:

- **Card:** one note (`ScratchSummary`), plus an `inBundle` flag when it is shown as a member of an expanded stack.
- **Stack:** one bundle, with `bundleId`, `name` and its matching members (in store order).

Building the rows from `list(tab, today, search, filter)`, which stays in store order:

1. A note without a bundle becomes a card.
2. The first member of a bundle seen in the list places that bundle's stack at that position. Later members join the stack.
3. When the bundle is expanded, its members follow the stack as `inBundle` cards.
4. A bundle with no matching notes produces no row.

`BoardModel` keeps the set of expanded bundle ids. `ViewState` (session-only, B4) grows `filter` and `expanded`.

## Board UI

### Stack card

- **Drawing (CardDelegate):** two offset card outlines behind the front card; the bundle name in bold; "3 notes"; the first two member titles; the oldest member's age. When expanded, a `▾` is shown, and the members get a 3 px border in the stack's accent (the Highlight colour at 60 %).
- **Interaction:** click or `Enter` toggles expand. `Esc` on a selected *member* collapses its bundle and selects the stack.
- **Menu:** Open/Collapse, "Rename bundle…" (a QInputDialog; an empty name is rejected).

### Drag and drop

`QListView` drag and drop (`DragDrop`, internal moves), with the model's mime data carrying the note id.

| Drag | Drop on | Result | Undo |
|---|---|---|---|
| card | another card | `bundleTogether(target, dragged)` | dragged goes back to its previous bundle (or none) |
| card | stack | `moveToBundle(dragged, stack)` | previous bundle |
| bundle member | empty space | `moveToBundle(dragged, nullopt)` | previous bundle |
| card without a bundle | empty space | nothing | — |
| stack | anything | not draggable | — |

Each drop reloads, selects the dragged card and sets the status line ("Bundled into Trip", "Moved to Trip", "Removed from Trip").

Menus: a card in a bundle gets "Remove from bundle".

### Chip bar

- **Placement:** a `ChipBar` widget under the tabs: a flow layout that wraps to more lines and is hidden when there are no chips.
- **Order:** colour swatch chips, then Overdue / Due today / No date, then `#tag` chips. Each shows its count; active chips are drawn in Highlight.
- **Behaviour:**
  - clicking toggles a chip → `BoardWindow` updates the filter and reloads the model, the counts and the facets;
  - `Esc` order:
    - in the search box: a non-empty search clears, otherwise the next steps apply;
    - on the grid: a selected member of an expanded bundle collapses it (selecting the stack); otherwise active chips clear; otherwise the board closes.

### Tag field (capture popup and board editor)

- **Widget:** a `TagField` in the footer (`QLineEdit` plus a `QCompleter` over `listTags()`, completing the word under the cursor). Manual tags are shown as `#a #b`; the `#` is optional when typing. The note's inline tags show beside it as a dimmed label.
- **Saving:** `CaptureController` saves manual tags on the same debounce as the text, after the note exists. If the note doesn't exist yet, it is created when the first text or tag change is saved. Discard-if-empty is unchanged: a note with tags but no text is still discarded.
- **Errors:** an invalid tag (`InvalidTag`) shows "Invalid tag: x" in the footer and saves nothing until it is fixed.
- **Keys:** `Ctrl+T` focuses the tag field from the text; `Esc` in the tag field closes like `Esc` in the text (save, hide).

## Errors

| Case | Behaviour |
|---|---|
| A store error during a drop, rename or chip reload | The status line says what failed and the board reloads (the existing `act()` path). |
| Drop target vanished (deleted elsewhere) | `NotFound` → status line plus reload. |
| Invalid tag in the field | Footer error; nothing saved. |

## Performance

`board-bench` seeds 10 % of its notes into bundles, gives them tags, and runs with one chip active. The gate is unchanged: p95 ≤ 300 ms for 2,000 notes.

## Testing (Qt Test, test-first)

Core:
- Filter algebra: OR within a kind and AND across kinds, on a fixed set of notes.
- Chips combined with search.
- `tabCounts` with a filter equals the filtered list sizes.
- The facet rule: a chip of an active kind stays visible; a zero-count chip hides; an active zero chip shows.
- `listTags` includes both inline and manual tags.

App:
- Model: stack placement, the members of an expanded stack, a stack hidden by a filter.
- Expand and collapse by click, `Enter` and `Esc`.
- Each drop case, its undo and its status text.
- The stack guard on `D`/`Del`/colour.
- Rename bundle.
- Chip toggle → filter; facets hide and show; `Esc` clear order.
- Tag field: saves, autocompletes, rejects invalid tags, `Ctrl+T` focuses it, a tag-only note is discarded.
- `ViewState` round-trips chips and expanded bundles.
- The bench passes with bundles and chips in the data.

## Roadmap edits

- M3: the M3a polish (fonts, card age, tab counts, draggable popups) and attachments in the database are done; M3b is in progress.
- M7 gains its last item: a single-exe release via a static Qt build.
- Planning note 4 (attachment grace period) now runs on the database's `touched_at`.
