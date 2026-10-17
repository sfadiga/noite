-- Attachments live in the database (attachments-in-db design, A1).
CREATE TABLE attachments (
  id TEXT PRIMARY KEY,
  data BLOB NOT NULL,
  size INTEGER NOT NULL,
  touched_at INTEGER NOT NULL
);
