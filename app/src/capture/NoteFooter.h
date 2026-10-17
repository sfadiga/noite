#pragma once

#include <noite/model.h>

#include <QDate>
#include <QString>

#include <optional>

// The footer under a note being edited: "Due 20/10/2026  ·  red", empty when neither is set.
// Shared by the capture window and the board's note overlay.
QString noteFooterText(std::optional<QDate> due, std::optional<noite::Color> color);
