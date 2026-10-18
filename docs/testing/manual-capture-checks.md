# Manual checks

Run on the **deployed** build (`tools\deploy.ps1 -Config Release`, then `dist\noite\noite.exe`) before each release, and whenever the capture window, editor or paste handling changes. Use a throwaway data folder (`noite.exe --data C:\temp\noite-check`) so real notes stay untouched.

Record pass/fail and a one-line note for any failure. Every failure becomes an automated test plus a fix.

| # | Check | How | Expected |
|---|---|---|---|
| 1 | Hotkey from an elevated app | Focus Task Manager, press `Ctrl+Alt+J`, type | Capture opens focused and takes the typing |
| 2 | Hotkey while open | With the capture open, press `Ctrl+Alt+J` again | Stays open and focused, text kept, no flicker |
| 3 | Typed markdown | `# Title`, `- item`, `1. item`, `[] task`, `**b** *i* `` `c` ``, `[site](https://example.com)` | Each becomes its formatting as you type |
| 4 | Checkbox click | Click just left of a `[]` item | It toggles |
| 5 | Due dates | `pay rent @tomorrow`, `@sex`, `@20/10` | Token disappears, footer shows the date |
| 6 | Paste from a browser | Copy part of a web page with an image, paste | Formatting and links kept; local/inline images become attachments; remote images become links |
| 7 | Paste from Word | Copy text with formatting and a picture, paste | Formatting kept, picture kept, no Word fonts or colours |
| 8 | Dark theme | Switch Windows to dark mode, paste black text from Word | Text readable (follows the theme) |
| 9 | Copied file | Copy a PDF in Explorer, paste | A file chip with its name; `Ctrl+click` opens it |
| 10 | Copied program | Copy an `.exe`, paste, `Ctrl+click` the chip | Opens the folder, doesn't run the program |
| 11 | Click away | Type, then click another window | Capture hides; the note is saved |
| 12 | `Ctrl+Enter` | Type, press `Ctrl+Enter` | Capture closes; the note is marked done |
| 13 | Colour | `Ctrl+Shift+C` a few times | Footer cycles red → … → purple → none |
| 14 | Empty capture | Open, press `Esc` without typing | Nothing is saved |
| 15 | Tray | Right-click the tray icon → New note | Opens a capture (left-click opens the board: B1) |
| 16 | Second launch | Start `noite.exe` again with the same data folder | The running copy opens a capture; no second tray icon |
| 17 | Second monitor | Focus a window on the other monitor, press the hotkey | Capture centres on that monitor, crisp |
| 18 | Display scale | Check at the display's scaling | Text crisp, nothing clipped |
| 19 | Hotkey taken | Start with another app holding `Ctrl+Alt+J` | Tray warning; tray still opens a capture |

## Board

| # | Check | How | Expected |
|---|---|---|---|
| B1 | Tray opens the board | Left-click the tray icon; right-click → Board | The board opens in front, Today tab, first card selected |
| B2 | Board hotkey | Press `Ctrl+Alt+B` from another app, then again with the board behind other windows | Opens; the second press brings it to the front |
| B3 | Keyboard only | `/`, type part of a note, `Esc`, arrows, `Enter`, edit, `Esc`, `D`, `Ctrl+Z` | Search narrows and clears; the note opens and saves; done then undone |
| B4 | Context menu | Right-click a card; try every item, including Colour, Due date → Pick…, Clone | Each works; the status line says what happened |
| B5 | Live refresh | With the board open, capture a note with `Ctrl+Alt+J` | The new card appears without reopening the board |
| B6 | Dark mode | Switch Windows to dark mode, open the board | Card tints muted, text readable, the overlay readable |
| B7 | Many notes | `build\Release\board-bench.exe` (2,000 notes: prints timings, flashes the board), then a real folder with as many notes as you have | Bench prints PASS; the real board scrolls smoothly |
| B8 | Display scale | 150% scaling | Cards crisp, nothing clipped |
| B9 | Reopen keeps place | Pick the Completed tab and a search, close the board, reopen it | Same tab and search |
| B10 | Tabs and counts | Look at the tab row; type a search; mark a card done | Four equal caps buttons; counts match the cards and follow the search and the action |
| B11 | Age on cards | Look at old, new and dated cards | Each active card shows its age (`3h`, `12d`), after the due label if any |
| B12 | Plain cards in dark mode | Dark mode, a card with no colour | Its edges are visible |
| B13 | Drag the popups | Drag the capture window by its margin or footer; open a card on the board and drag its panel | Both move; dragging inside the text selects text; each re-centres when reopened |
| B14 | Bundles | Drag a card onto another card, then a third onto the stack; click the stack; drag a member to empty space; right-click the stack → Rename bundle…; `Esc` on a member; `D` on the stack; right-click the stack → Unbundle; `Ctrl+Z` | A stack appears and expands in place; the member really leaves (it doesn't just move on screen); rename shows; `Esc` collapses; `D` says "Open the bundle…"; Unbundle frees every note; undo brings the bundle back |
| B15 | Filter chips | Click a colour chip, then a tag chip; watch the tab counts; press `Esc` twice | Cards and counts narrow (AND across kinds); chips with no matches disappear; the first `Esc` clears the chips, the second closes |
| B16 | Tag field | In a capture press `Ctrl+T`, type `#tr` and pick a completion; type `#1x`; fix it; `Esc` | Autocomplete offers existing tags; "Invalid tag: 1x" shows; the saved note has the tag and a chip appears on the board |
