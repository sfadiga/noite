#pragma once

// Thin RAII layer over the SQLite C API. Every SQLite error is thrown as
// noite::Error(ErrorCode::Database). Internal to noite-core.

#include <QString>

#include <optional>

struct sqlite3;
struct sqlite3_stmt;

namespace noite::detail {

class Stmt {
public:
    Stmt(sqlite3* db, sqlite3_stmt* stmt);
    Stmt(Stmt&& other) noexcept;
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;
    ~Stmt();

    Stmt& bind(int index, qint64 value);
    Stmt& bind(int index, int value) { return bind(index, qint64(value)); }
    Stmt& bind(int index, const QString& value);
    Stmt& bind(int index, std::nullopt_t);
    template <typename T>
    Stmt& bind(int index, const std::optional<T>& value)
    {
        return value ? bind(index, *value) : bind(index, std::nullopt);
    }

    // True while a row is available, false when done.
    bool step();
    void reset();

    qint64 int64(int column) const;
    QString text(int column) const;
    bool isNull(int column) const;
    std::optional<qint64> optInt64(int column) const;
    std::optional<QString> optText(int column) const;

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_;
};

class Db {
public:
    // Read-write databases get WAL, synchronous=NORMAL, foreign keys and a busy timeout.
    static Db open(const QString& path, bool readOnly = false);

    Db(Db&& other) noexcept;
    Db& operator=(Db&& other) noexcept;
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    ~Db();

    void exec(const QString& sql); // one or more statements, no results
    Stmt prepare(const QString& sql);
    int userVersion();
    void setUserVersion(int version);
    int changes() const;
    sqlite3* handle() const { return db_; }

private:
    explicit Db(sqlite3* db)
        : db_(db)
    {
    }

    sqlite3* db_ = nullptr;
};

// BEGIN IMMEDIATE on construction; ROLLBACK on destruction unless committed.
class Transaction {
public:
    explicit Transaction(Db& db);
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();
    void commit();

private:
    Db& db_;
    bool done_ = false;
};

// Consistent copy of the database into `dest`. Must not run inside a transaction.
void vacuumInto(Db& db, const QString& dest);

[[noreturn]] void throwDb(sqlite3* db, const QString& context);

} // namespace noite::detail
