#pragma once

#include <QDate>
#include <QString>

#include <optional>

// The date an @token means (token without the "@"), relative to `today` (D9).
// Case-insensitive, accents ignored:
//   today/hoje, tomorrow/amanha
//   mon..sun / seg ter qua qui sex sab dom  -> next occurrence, today included
//   YYYY-MM-DD, DD/MM (this year, or next year if already past), DD/MM/YYYY
std::optional<QDate> parseDateToken(const QString& token, QDate today);
