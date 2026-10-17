#include "Seed.h"

#include <noite/clock.h>
#include <noite/store.h>

#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QRandomGenerator>

namespace {

const QStringList kWords = {
    "call",    "buy",     "review", "send",   "fix",      "meeting", "invoice", "report", "draft",  "idea",
    "ligar",   "comprar", "revisar", "enviar", "reunião", "amanhã",  "projeto", "cliente", "notes", "budget",
    "release", "test",    "deploy", "design", "question", "answer",  "follow",  "up",      "with",  "team",
};

QString sentence(QRandomGenerator& rng)
{
    QStringList words;
    const int n = 3 + rng.bounded(12);
    for (int i = 0; i < n; ++i)
        words << kWords[rng.bounded(int(kWords.size()))];
    words[0][0] = words[0][0].toUpper();
    return words.join(' ');
}

QByteArray randomPng(QRandomGenerator& rng)
{
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(QColor::fromRgb(rng.generate() | 0xFF000000u));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

void seedBoard(noite::Store& store, int count, QDate today, quint32 seed,
               const std::function<void(qint64 ms)>& setNow)
{
    static constexpr noite::Color colors[] = {noite::Color::Red,   noite::Color::Orange, noite::Color::Yellow,
                                              noite::Color::Green, noite::Color::Blue,   noite::Color::Purple};
    QRandomGenerator rng(seed);
    const qint64 now = QDateTime(today, QTime(9, 0)).toMSecsSinceEpoch();
    for (int i = 0; i < count; ++i) {
        setNow(now - qint64(rng.bounded(120 * 24)) * 3'600'000);
        QString html;
        const int paragraphs = 1 + rng.bounded(12);
        for (int p = 0; p < paragraphs; ++p)
            html += "<p>" + sentence(rng).toHtmlEscaped() + "</p>";
        if (rng.bounded(100) < 10)
            html += "<p><img src=\"noite-att:" + store.putAttachment(randomPng(rng), "png") + "\"/></p>";
        const noite::ScratchId id = store.createScratch(html).id;
        if (rng.bounded(100) < 30)
            store.setDue(id, today.addDays(rng.bounded(-10, 21)));
        if (rng.bounded(100) < 40)
            store.setColor(id, colors[rng.bounded(6)]);
        const int fate = rng.bounded(100);
        if (fate < 10)
            store.markDone(id);
        else if (fate < 15)
            store.trash(id);
    }
}
