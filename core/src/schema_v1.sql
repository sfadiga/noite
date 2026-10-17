CREATE TABLE bundles (id TEXT PRIMARY KEY, name TEXT NOT NULL, created_at INTEGER NOT NULL);
CREATE TABLE scratches (
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
  status TEXT NOT NULL CHECK (status IN ('active','done','trashed')),
  trashed_from TEXT CHECK (trashed_from IN ('active','done')),
  done_at INTEGER,
  trashed_at INTEGER
);
CREATE INDEX scratches_status ON scratches(status);
CREATE TABLE scratch_tags (
  scratch_id TEXT NOT NULL REFERENCES scratches(id) ON DELETE CASCADE,
  tag TEXT NOT NULL,
  source TEXT NOT NULL CHECK (source IN ('inline','manual')),
  PRIMARY KEY (scratch_id, tag, source)
);
CREATE VIRTUAL TABLE scratch_fts USING fts5(id UNINDEXED, body, tokenize = 'unicode61 remove_diacritics 2');
CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
