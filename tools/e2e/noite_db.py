# Reads (and, for setup, prepares) a noite data folder's database for the end-to-end run.
#
#   noite_db.py prepare <db>        marks first-run setup completed, keeps every other setting
#   noite_db.py expect <db> --text TEXT --status {active,done,trashed}
#                     [--trashed-from {active,done}] [--timeout SECONDS]
#                                   waits until exactly one note matches; read-only
#   noite_db.py --self-test         checks both commands on a temporary database
#
# Exit codes: 0 ok, 1 not as expected (expect) or a failed self-test, 2 a usage or file error.
# Standard library only; run with `python -I`.
import argparse
import json
import os
import shutil
import sqlite3
import sys
import tempfile
import threading
import time


class UsageError(Exception):
    pass


def _require(db):
    if not os.path.isfile(db):
        raise UsageError("no database at " + db)


def prepare(db):
    _require(db)
    con = sqlite3.connect(db)
    try:
        row = con.execute("SELECT value FROM settings WHERE key='settings'").fetchone()
        settings = json.loads(row[0]) if row else {}
        settings["setupCompleted"] = True
        con.execute("INSERT OR REPLACE INTO settings(key, value) VALUES ('settings', ?)",
                    (json.dumps(settings, separators=(",", ":")),))
        con.commit()
    finally:
        con.close()


def _notes(db):
    # A fresh read-only connection per poll: each read sees noite's latest commit (WAL).
    con = sqlite3.connect("file:" + db.replace("\\", "/") + "?mode=ro", uri=True)
    try:
        return con.execute("SELECT plain_text, status, trashed_from FROM scratches").fetchall()
    finally:
        con.close()


def expect(db, text, status, trashed_from=None, timeout=5.0):
    """Returns (ok, message)."""
    _require(db)
    wanted = "%s|%s|%s" % (text, status, trashed_from or "")
    deadline = time.monotonic() + timeout
    while True:
        notes = _notes(db)
        matches = [n for n in notes
                   if (n[0] or "").strip() == text and n[1] == status
                   and (trashed_from is None or n[2] == trashed_from)]
        if len(matches) == 1:
            return True, "ok: " + status
        if time.monotonic() >= deadline:
            found = ["%s|%s|%s" % ((n[0] or "").strip(), n[1], n[2] or "") for n in notes]
            return False, "expected %s, found: [%s]" % (wanted, ", ".join(found))
        time.sleep(0.1)


def _self_test():
    tmp = tempfile.mkdtemp()
    db = os.path.join(tmp, "noite.db")
    con = sqlite3.connect(db)
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("CREATE TABLE settings(key TEXT PRIMARY KEY, value TEXT)")
    con.execute("CREATE TABLE scratches(id TEXT PRIMARY KEY, plain_text TEXT, status TEXT, trashed_from TEXT)")
    con.execute("INSERT INTO settings VALUES ('settings', ?)", (json.dumps({"captureHotkey": "Ctrl+Alt+J"}),))
    con.commit()

    def rows(*values):
        con.execute("DELETE FROM scratches")
        for i, (text, status, trashed_from) in enumerate(values):
            con.execute("INSERT INTO scratches VALUES (?, ?, ?, ?)", (str(i), text, status, trashed_from))
        con.commit()

    def case(name, check):
        if not check():
            print("noite_db self-test: FAILED " + name)
            sys.exit(1)

    def prepare_sets_setup_completed():
        prepare(db)
        value = json.loads(con.execute("SELECT value FROM settings WHERE key='settings'").fetchone()[0])
        return value == {"captureHotkey": "Ctrl+Alt+J", "setupCompleted": True}

    def expect_passes():
        rows(("e2e buy milk", "done", None))
        return expect(db, "e2e buy milk", "done", timeout=1)[0]

    def expect_waits_for_a_late_write():
        rows()

        def late():
            time.sleep(0.5)
            writer = sqlite3.connect(db)
            writer.execute("INSERT INTO scratches VALUES ('late', 'e2e buy milk', 'active', NULL)")
            writer.commit()
            writer.close()

        thread = threading.Thread(target=late)
        thread.start()
        ok = expect(db, "e2e buy milk", "active", timeout=3)[0]
        thread.join()
        return ok

    def expect_times_out_with_what_it_found():
        rows(("e2e buy milk", "done", None))
        ok, message = expect(db, "e2e buy milk", "trashed", timeout=0.3)
        return not ok and "e2e buy milk|done" in message

    def expect_checks_trashed_from():
        rows(("e2e buy milk", "trashed", "active"))
        return not expect(db, "e2e buy milk", "trashed", trashed_from="done", timeout=0.3)[0]

    def expect_rejects_two_matches():
        rows(("e2e buy milk", "active", None), ("e2e buy milk", "active", None))
        return not expect(db, "e2e buy milk", "active", timeout=0.3)[0]

    case("prepareSetsSetupCompleted", prepare_sets_setup_completed)
    case("expectPasses", expect_passes)
    case("expectWaitsForALateWrite", expect_waits_for_a_late_write)
    case("expectTimesOutWithWhatItFound", expect_times_out_with_what_it_found)
    case("expectChecksTrashedFrom", expect_checks_trashed_from)
    case("expectRejectsTwoMatches", expect_rejects_two_matches)
    con.close()
    shutil.rmtree(tmp, ignore_errors=True)
    print("noite_db self-test: ok")


def main(argv):
    if argv == ["--self-test"]:
        _self_test()
        return 0
    parser = argparse.ArgumentParser(prog="noite_db.py")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("prepare").add_argument("db")
    e = commands.add_parser("expect")
    e.add_argument("db")
    e.add_argument("--text", required=True)
    e.add_argument("--status", required=True, choices=["active", "done", "trashed"])
    e.add_argument("--trashed-from", choices=["active", "done"])
    e.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args(argv)
    try:
        if args.command == "prepare":
            prepare(args.db)
            print("prepared")
            return 0
        ok, message = expect(args.db, args.text, args.status, args.trashed_from, args.timeout)
        print(message)
        return 0 if ok else 1
    except (UsageError, sqlite3.Error, OSError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
