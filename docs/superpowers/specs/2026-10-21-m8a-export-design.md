# M8a: export a note — design

**Date:** 2026-10-21 · **Status:** agreed in a grilling session (Q1–Q13) · **Spec:** new decision D35; D14 (the core owns files), D30 (keys)

## Goal

One note, with its text, formatting and images, saved as a file other programs open: a self-contained **HTML** page or an A4 **PDF**.

## Decisions

| # | Decision | Why |
|---|---|---|
| X1 | Formats: **HTML** (one file, images embedded as base64 `data:` URIs) and **PDF** (A4). The Save dialog's file type picks one. | The author's choice: HTML keeps everything and re-opens in noite; PDF for sharing and printing. |
| X2 | One note at a time. No bundle, multi-select or tab export. | Scope for 1.1.0. |
| X3 | Entry points: the card menu's **Export…**, `Ctrl+E` on the board (the selected card), `Ctrl+E` in the note open over the board. Not in the capture window. | Capture stays minimal. |
| X4 | A standard Save dialog. Default name: the note's title made safe for Windows (X9). It starts in the last export folder (`last_export_dir`, kept apart from Settings like `last_review_day`), else Documents. Overwrite asks (the dialog's own prompt). | Familiar. |
| X5 | Always light: white page, dark text, Segoe UI 10.5 pt. No card colour. | Prints and shares cleanly, whatever the Windows theme. |
| X6 | Content only: no header with created, due or tags. | The author's choice. |
| X7 | Images: the stored bytes, full resolution, not recompressed, at the size they have in the note. PDF: images wider than the text width are scaled down to it (aspect kept). HTML: `img { max-width: 100%; height: auto }`. | Fidelity; a page has a width. |
| X8 | Checkboxes: in HTML, Qt's `<li class="checked|unchecked">` gets CSS so a browser shows ☑ / ☐; opening the file again in a `QTextDocument` gives real checkboxes back. In PDF, Qt's layout draws them. | Both readers. |
| X9 | File name: `<>:"/\|?*` and control characters become `_`; trailing dots and spaces are dropped; a reserved device name (`CON`, `NUL`, `COM1`…) gets `_` appended; at most 100 characters; empty becomes `note`. | Windows rules. |
| X10 | A missing image (attachment gone or unreadable) becomes the text `[missing image]`; the export still succeeds. | One lost image shouldn't lose the note. |
| X11 | In `noite-core`: `Store::exportScratch(id, path, ExportFormat)`. The board only asks for the path and shows errors. | D14: only the core touches files. |
| X12 | Works on every tab, Trash included. | A trashed note may be the one to keep a copy of. |
| X13 | Success shows nothing. A failure shows a message box with the path and the error. | The file is where you saved it. |
| X14 | If the note is open over the board, it is saved first, then exported from the database. | The export matches what's on screen. |

## Core

- `enum class ExportFormat { Html, Pdf };`
- `void Store::exportScratch(const ScratchId& id, const QString& path, ExportFormat format) const` throws `NotFound`, or `Io` with the path and the OS message (a folder that doesn't exist, a read-only file, a file in use).
- `std::optional<QString> Store::lastExportDir() const` / `void setLastExportDir(const QString&)`.
- `QString content::exportFileName(const QString& title)` (X9), without an extension.
- **Preparing the document**, the same for both formats:
  1. Every `noite-att:` image whose bytes can't be read is replaced by `[missing image]` (X10).
  2. The remaining images are added as document resources.
- **HTML:**
  1. Take `toHtml()` of the prepared document.
  2. `src="noite-att:<id>"` becomes `src="data:<mime>;base64,…"`, with the mime type from the id's extension (`png`, `jpg`/`jpeg`, `gif`, `bmp`, `webp`; anything else `application/octet-stream`).
  3. `<title>` is the note's title. The `<style>` covers X5, X7 and X8.
  4. Written UTF-8, no BOM, through `QSaveFile`, so a failed write never leaves a half file.
- **PDF:**
  1. `QPdfWriter`: A4, 20 mm margins, 300 dpi, title as the document title.
  2. Images wider than the text width are given a width (and a proportional height) in their `QTextImageFormat`.
  3. `QTextDocument::print`. Links stay clickable, since Qt writes `/URI` annotations.
  4. Written to a temporary file in the target folder, then renamed into place.

## App

- `BoardWindow`:
  - **Export…** in the card menu;
  - a window-scoped `Ctrl+E` that exports the selected card, or the open note after the overlay saves it (X14);
  - two seams, like `askBackupFolder`: `std::function<QString(const QString& suggestedPath)>` for the path (default `QFileDialog::getSaveFileName`, filters `Web page (*.html)` and `PDF (*.pdf)`), and `std::function<void(const QString&)>` for the error (default `QMessageBox::warning`).
- The format follows the chosen file's extension: `.pdf` for PDF, anything else is HTML (`.html` is added when the name has no extension). The folder of the chosen path is saved as `last_export_dir`.

## Errors

| Case | Behaviour |
|---|---|
| Dialog cancelled | Nothing. |
| Folder gone, file read-only or in use | Message box: `Couldn't export to <path>: <OS message>`; no file left behind. |
| Note deleted meanwhile | Message box with the `NotFound` message. |
| An image is missing | `[missing image]` in its place; the export succeeds. |

## Testing

- **Core** (`tst_export`):
  - **HTML round-trip:** heading, bold, link, checkbox (checked and unchecked), table and image all survive `setHtml` of the file. The image comes back as a `data:` URI with the original bytes, and there is no `noite-att:` left.
  - **Content:** `<title>` is the note's title; the CSS has the checkbox rules.
  - **PDF:** the file starts with `%PDF-`, a long note gives more than one page, and a link gives a `/URI`.
  - **Wide images:** in the prepared document, no image is wider than the text width.
  - **Errors:** a missing image gives `[missing image]` in both formats; an unwritable path throws `Io` and leaves no file; an unknown id throws `NotFound`.
  - **Folder:** `lastExportDir` round-trips and survives `saveSettings`.
  - **File names:** `exportFileName` cases (X9).
- **App** (`tst_board_window`):
  - the menu action and `Ctrl+E` call the path seam with `<last dir>/<safe title>.html`;
  - a returned `.pdf` path writes a PDF;
  - an empty path writes nothing;
  - `Ctrl+E` with the overlay open exports the edited text;
  - a failing path calls the error seam.

## Order of work

1. `content::exportFileName`.
2. `Store::exportScratch` for HTML, then PDF; `lastExportDir`.
3. Board: menu, `Ctrl+E`, seams.
4. Docs: `spec.md` D35 and the board keys line, README board keys, CHANGELOG.
