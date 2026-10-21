-- The deferred status (M8c, F1). SQLite can't change a CHECK in place, so scratches is
-- rebuilt. Migrations run in a transaction with foreign keys on, where DROP TABLE deletes
-- every row first and the cascade empties scratch_tags: the tags are kept aside and put back.
CREATE TEMP TABLE kept_tags AS SELECT * FROM scratch_tags;
CREATE TABLE scratches_v3 (
  id TEXT PRIMARY KEY,
  content TEXT NOT NULL,
  plain_text TEXT NOT NULL,
  title TEXT NOT NULL,
  first_image TEXT,
  created_at INTEGER NOT NULL,
  modified_at INTEGER NOT NULL,
  due_date TEXT,
  color TEXT CHECK (color IN ('red','orange','yellow','green','blue','purple')),
  bundle_id TEXT REFERENCES bundles(id) ON DELETE SET NULL,
  status TEXT NOT NULL CHECK (status IN ('active','done','trashed','deferred')),
  trashed_from TEXT CHECK (trashed_from IN ('active','done','deferred')),
  done_at INTEGER,
  trashed_at INTEGER
);
INSERT INTO scratches_v3 SELECT id, content, plain_text, title, first_image, created_at, modified_at, due_date,
  color, bundle_id, status, trashed_from, done_at, trashed_at FROM scratches;
DROP TABLE scratches;
ALTER TABLE scratches_v3 RENAME TO scratches;
CREATE INDEX scratches_status ON scratches(status);
INSERT INTO scratch_tags SELECT * FROM kept_tags;
DROP TABLE kept_tags;
