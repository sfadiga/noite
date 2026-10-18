#include "db.h"

#include <noite/error.h>

#include <sqlite3.h>

#include <utility>

namespace noite::detail {

void throwDb(sqlite3* db, const QString& context)
{
    const QString message = db ? QString::fromUtf8(sqlite3_errmsg(db)) : QString("out of memory");
    throw Error(ErrorCode::Database, context + ": " + message);
}

// ---- Stmt ----

Stmt::Stmt(sqlite3* db, sqlite3_stmt* stmt)
    : db_(db)
    , stmt_(stmt)
{
}

Stmt::Stmt(Stmt&& other) noexcept
    : db_(other.db_)
    , stmt_(std::exchange(other.stmt_, nullptr))
{
}

Stmt::~Stmt()
{
    sqlite3_finalize(stmt_);
}

Stmt& Stmt::bind(int index, qint64 value)
{
    if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK)
        throwDb(db_, "bind");
    return *this;
}

Stmt& Stmt::bind(int index, const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    if (sqlite3_bind_text(stmt_, index, utf8.constData(), int(utf8.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throwDb(db_, "bind");
    return *this;
}

Stmt& Stmt::bindBlob(int index, const QByteArray& value)
{
    if (sqlite3_bind_blob64(stmt_, index, value.constData(), sqlite3_uint64(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throwDb(db_, "bind");
    return *this;
}

Stmt& Stmt::bind(int index, std::nullopt_t)
{
    if (sqlite3_bind_null(stmt_, index) != SQLITE_OK)
        throwDb(db_, "bind");
    return *this;
}

bool Stmt::step()
{
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    throwDb(db_, "step");
}

void Stmt::reset()
{
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

qint64 Stmt::int64(int column) const
{
    return sqlite3_column_int64(stmt_, column);
}

QString Stmt::text(int column) const
{
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    return QString::fromUtf8(data, sqlite3_column_bytes(stmt_, column));
}

QByteArray Stmt::blob(int column) const
{
    const auto* data = static_cast<const char*>(sqlite3_column_blob(stmt_, column));
    return QByteArray(data, sqlite3_column_bytes(stmt_, column));
}

bool Stmt::isNull(int column) const
{
    return sqlite3_column_type(stmt_, column) == SQLITE_NULL;
}

std::optional<qint64> Stmt::optInt64(int column) const
{
    if (isNull(column))
        return std::nullopt;
    return int64(column);
}

std::optional<QString> Stmt::optText(int column) const
{
    if (isNull(column))
        return std::nullopt;
    return text(column);
}

// ---- Db ----

Db Db::open(const QString& path, bool readOnly)
{
    sqlite3* raw = nullptr;
    const int flags = readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const int rc = sqlite3_open_v2(path.toUtf8().constData(), &raw, flags, nullptr);
    Db db(raw);
    if (rc != SQLITE_OK)
        throwDb(raw, "open " + path);
    sqlite3_busy_timeout(raw, 5000);
    if (!readOnly)
        db.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
    return db;
}

Db::Db(Db&& other) noexcept
    : db_(std::exchange(other.db_, nullptr))
{
}

Db& Db::operator=(Db&& other) noexcept
{
    if (this != &other) {
        sqlite3_close_v2(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

Db::~Db()
{
    sqlite3_close_v2(db_);
}

void Db::exec(const QString& sql)
{
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.toUtf8().constData(), nullptr, nullptr, &error) != SQLITE_OK) {
        const QString message = QString::fromUtf8(error);
        sqlite3_free(error);
        throw Error(ErrorCode::Database, "exec: " + message);
    }
}

Stmt Db::prepare(const QString& sql)
{
    sqlite3_stmt* stmt = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    if (sqlite3_prepare_v2(db_, utf8.constData(), int(utf8.size()), &stmt, nullptr) != SQLITE_OK)
        throwDb(db_, "prepare");
    return Stmt(db_, stmt);
}

int Db::userVersion()
{
    Stmt s = prepare("PRAGMA user_version");
    s.step();
    return int(s.int64(0));
}

void Db::setUserVersion(int version)
{
    exec(QString("PRAGMA user_version = %1").arg(version));
}

int Db::changes() const
{
    return sqlite3_changes(db_);
}

// ---- Transaction ----

Transaction::Transaction(Db& db)
    : db_(db)
{
    db_.exec("BEGIN IMMEDIATE");
}

Transaction::~Transaction()
{
    if (!done_)
        sqlite3_exec(db_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
}

void Transaction::commit()
{
    db_.exec("COMMIT");
    done_ = true;
}

void vacuumInto(Db& db, const QString& dest)
{
    Stmt s = db.prepare("VACUUM INTO ?");
    s.bind(1, dest);
    s.step();
}

} // namespace noite::detail
