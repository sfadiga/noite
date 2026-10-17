#pragma once

#include <noite/error.h>

#include <QDate>
#include <QDateTime>
#include <QTest>
#include <QTimeZone>

namespace noite::test {

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
