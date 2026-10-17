# Attachments in the database — design

**Date:** 2026-10-17 · **Status:** approved in conversation · **Changes:** D4, D7, D13, D21, D22 (spec.md)

## Goal

Everything a note holds lives in one file, `data\noite.db`. Pasted images and copied files move from `data\attachments\` into the database, so copying `noite.db` copies all of the data.

## Decisions

| # | Decision | Why |
|---|---|---|
| A1 | Blobs in an `attachments` table inside `noite.db` | One file. A second database file or a zip container would still leave more than one file. |
| A2 | Attachment ids stay `<sha256>.<ext>` | Note HTML (`noite-att:<id>`) is unchanged; no content rewrite, no FTS rebuild. |
| A3 | A snapshot is one self-contained `.db`; keep 14 | Simplest restore: any snapshot opens on its own. Cost: every snapshot carries every attachment (100 MB of images × 14 ≈ 1.4 GB of backups). |
| A4 | No import of existing attachment files; v2 only adds the table | Unreleased software with no attachments in use (the author's call: not worth the effort). |
| A5 | Ctrl+click on a file chip writes the file to `%TEMP%\noite\` and opens it there; the folder is emptied on startup | The OS needs a real file. Edits to the opened copy are not saved back (they weren't before either: stored bytes are content-addressed). |

## Storage

```sql
CREATE TABLE attachments (
  id TEXT PRIMARY KEY,          -- <sha256>.<ext>
  data BLOB NOT NULL,
  size INTEGER NOT NULL,
  touched_at INTEGER NOT NULL   -- ms; the purge's grace clock
);
```

- Board queries never read `attachments`, so board speed is unaffected.
- `auto_vacuum = INCREMENTAL`, set during the migration (it needs one `VACUUM`), so the purge can return space to the filesystem.

`data\` after this change:

```
data\
  noite.db            (+ -wal, -shm while running)
  noite.lock
  logs\
  migration-backups\
```

`DataDir` no longer creates `attachments\`.

## Migration v1 → v2

noite is unreleased and the author's database holds no attachments, so v2 does **not** import files from `data\attachments\` (ruled 2026-10-17). Existing attachment files, if any, are left alone and are no longer referenced.

1. The existing pre-migration snapshot is taken first (`migration-backups\noite-v1-<ms>.db`).
2. SQL (`schema_v2.sql`): create `attachments`; set `user_version = 2`.
3. When the store opens a database whose `auto_vacuum` isn't INCREMENTAL, it runs `PRAGMA auto_vacuum = INCREMENTAL; VACUUM;` once. This covers both new and migrated databases.
4. If `data\attachments\` is empty, the store removes it.

On failure, startup fails with `MigrationFailed`, as for any migration today.

## Core API (noite-core)

- `QString putAttachment(const QByteArray& bytes, const QString& ext)`: signature, 25 MiB limit and extension rules unchanged. It inserts the row if the id is new, then sets `touched_at = now` (a re-paste restarts the grace period, as the file mtime did).
- `QByteArray attachmentBytes(const QString& id) const` replaces `attachmentPath`. It throws `InvalidAttachmentId` for a malformed id, and `AttachmentMissing` (a new `ErrorCode`) when no row exists.
- `purgeTrash`: the mark phase is unchanged. The sweep becomes `DELETE FROM attachments WHERE id NOT IN (referenced) AND touched_at <= now - kAttachmentGraceMs`, then `PRAGMA incremental_vacuum`. `PurgeReport::attachmentsDeleted` counts the deleted rows.

## App

- `AttachmentStore::pathFor(id)` becomes `bytes(id) → QByteArray`; `StoreAttachments` forwards to `attachmentBytes`. The test fake follows suit.
- `NoteEdit::loadResource` and `CardDelegate::thumbnail` use `QImage::fromData(bytes)`. Errors give a null image, as a missing file does today.
- `NoteEdit::openTargetFor(id, name)`:
  - writes the bytes to `%TEMP%\noite\<first 12 hex of id>\<name>`, where `name` is the chip's text with characters invalid in Windows file names replaced by `_`; if that leaves nothing, the id is the name;
  - an existing file with identical size is reused;
  - runnable extensions (the existing list) still return the containing folder.
- `App` empties `%TEMP%\noite\` at startup, best-effort: files still open elsewhere stay.

## Backup (noite-core `backup`)

- `snapshot`: `VACUUM INTO` the final name's `.tmp`, then rename. No attachment copying.
- `verify`: `integrity_check`, and every `noite-att:` reference must have a row.
- `prune`: the snapshot count rule is unchanged; it no longer touches an `attachments\` folder. There is no compatibility code for v1 snapshots.

## Errors

| Case | Behaviour |
|---|---|
| Blob missing for a reference | Broken image or a chip that doesn't open, plus a log line; never a crash. |
| Temp folder unwritable | The chip doesn't open; the status/log says why. |
| Migration fails | Rollback; startup error `MigrationFailed`. |

## Testing (Qt Test, test-first)

Core:
- `migrateV1ToV2`: a v1 database opens as v2 with an empty `attachments` table, its notes intact, and `auto_vacuum` INCREMENTAL; an empty `data\attachments\` is removed and a non-empty one is left.
- `putDedupsAndStampsTouchedAt`, `attachmentBytesErrors` (invalid id, missing id).
- `purgeSweepsRowsAfterGraceAndShrinksFile`.
- The spec's attachment-safety case, rewritten for the database: paste → clone → trash the original → purge → the clone's image bytes are still there.
- Backup: `snapshotIsSelfContained` (verifies with no `attachments\` folder; the image's bytes read back from the snapshot), `verifyMissingRowFails`.
- DataDir: `attachments\` is not created.

App:
- The editor loads a pasted image from the database after a reopen; a card thumbnail is drawn from the bytes.
- `openTargetFor` writes the temp file under the chip name (sanitised), reuses it, and returns the folder for runnable types.
- Startup empties `%TEMP%\noite`.

## Spec edits (spec.md)

- **D4/D13 layout:** drop `attachments\`; `noite.db` holds attachments.
- **D7:** "loaded from `attachments\`" → "stored in the database's `attachments` table".
- **D13 backup:** a snapshot is a single self-contained `.db`.
- **D21:** copied files are stored in the database (25 MB limit unchanged).
- **D22:** the sweep deletes unreferenced rows, then reclaims space.
