# Manual capture checks

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
| 15 | Tray | Left-click the tray icon; right-click → New note | Both open a capture |
| 16 | Second launch | Start `noite.exe` again with the same data folder | The running copy opens a capture; no second tray icon |
| 17 | Second monitor | Focus a window on the other monitor, press the hotkey | Capture centres on that monitor, crisp |
| 18 | Display scale | Check at the display's scaling | Text crisp, nothing clipped |
| 19 | Hotkey taken | Start with another app holding `Ctrl+Alt+J` | Tray warning; tray still opens a capture |
