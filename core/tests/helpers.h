#pragma once

#include <noite/clock.h>
#include <noite/data_dir.h>
#include <noite/error.h>
#include <noite/store.h>

#include <QDate>
#include <QDateTime>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

namespace noite::test {

inline qint64 t0();

// A store on a fresh temp data dir with a FixedClock at t0(). Heap-allocated because
// QTemporaryDir is not movable.
struct Fixture {
    QTemporaryDir tmp;
    std::shared_ptr<FixedClock> clock;
    std::unique_ptr<Store> store;
    QString root() const { return tmp.filePath("data"); }
};

inline std::unique_ptr<Fixture> fixture()
{
    auto f = std::make_unique<Fixture>();
    f->clock = std::make_shared<FixedClock>(t0());
    f->store = Store::open(DataDir::acquire(f->root()), f->clock);
    return f;
}

// One <p> per line, HTML-escaped; an empty line becomes an empty paragraph.
inline QString html(const QStringList& lines)
{
    QString out;
    for (const QString& line : lines)
        out += "<p>" + line.toHtmlEscaped() + "</p>";
    return out;
}

inline QString htmlWithImage(const QString& text, const QString& attachmentId)
{
    return "<p>" + text.toHtmlEscaped() + "<img src=\"noite-att:" + attachmentId + "\"/></p>";
}

inline QString fakeAttachmentId(char hex = 'a', const QString& ext = "png")
{
    return QString(64, QChar(hex)) + "." + ext;
}

// Closes the store (releasing the lock) and opens it again on the same data dir.
inline void reopen(Fixture& f)
{
    f.store.reset();
    f.store = Store::open(DataDir::acquire(f.root()), f.clock);
}

// 2026-10-15 08:00 UTC: the fixed "now" of every test.
inline qint64 t0()
{
    return QDateTime(QDate(2026, 10, 15), QTime(8, 0), QTimeZone::UTC).toMSecsSinceEpoch();
}

inline QDate today()
{
    return QDate(2026, 10, 15);
}

} // namespace noite::test

// Fails the current test unless `statement` throws noite::Error with `expectedCode`.
// A macro, not a function: QFAIL/QCOMPARE must return from the test function itself.
#define NOITE_EXPECT_ERROR(expectedCode, statement)                                    \
    do {                                                                               \
        bool noiteThrown_ = false;                                                     \
        try {                                                                          \
            statement;                                                                 \
        } catch (const noite::Error& noiteError_) {                                    \
            noiteThrown_ = true;                                                       \
            QVERIFY2(noiteError_.code() == (expectedCode), noiteError_.what());        \
        }                                                                              \
        if (!noiteThrown_)                                                             \
            QFAIL("expected noite::Error " #expectedCode " from: " #statement);       \
    } while (false)
