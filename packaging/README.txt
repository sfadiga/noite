noite <version> — notes that stay on your radar

Install
  Unzip anywhere you can write to (e.g. Documents\noite) — not Program Files.
  Run noite.exe. The first run opens a short setup.

Windows protected your PC?
  noite isn't code-signed. Click "More info", then "Run anyway".

Update
  Quit noite (tray icon → Quit), then replace the files with the new ones.
  Keep the data folder. A newer noite backs up the database before it changes its format.

Restore a backup
  Quit noite. Copy a snapshot from <your backup folder>\noite-backups\<id>\snapshots\
  over data\noite.db (keep a copy of the old file first). Start noite.

Where things are
  data\         your notes (one file: noite.db)
  data\logs\    logs and crash reports — send these if something breaks

Uninstall
  Untick "Start with Windows" in Settings, quit, delete the folder.
