#include "helpers.h"
#include "registry.h"

#include "board/BoardModel.h"
#include "board/CardDelegate.h"
#include "editor/AttachmentStore.h"

#include <noite/store.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QTest>

using namespace noite;

namespace {

ScratchSummary active(std::optional<QDate> due, qint64 createdAt = test::t0())
{
    ScratchSummary s;
    s.status = Status::Active;
    s.due = due;
    s.createdAt = createdAt;
    return s;
}

QByteArray pngBytes()
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QPalette darkPalette()
{
    QPalette p;
    p.setColor(QPalette::Base, QColor(30, 30, 30));
    p.setColor(QPalette::Window, QColor(40, 40, 40));
    p.setColor(QPalette::Text, QColor(230, 230, 230));
    p.setColor(QPalette::WindowText, QColor(230, 230, 230));
    return p;
}

// Paints every row of `model` selected and unselected, with light and dark palettes.
void paintAll(const BoardModel& model, CardDelegate& delegate)
{
    for (const QPalette& palette : {QPalette(), darkPalette()}) {
        for (bool selected : {false, true}) {
            for (int row = 0; row < model.rowCount(); ++row) {
                QImage image(CardDelegate::kCardSize, QImage::Format_ARGB32);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                QStyleOptionViewItem option;
                option.rect = QRect(QPoint(0, 0), CardDelegate::kCardSize);
                option.palette = palette;
                option.state = QStyle::State_Enabled;
                if (selected)
                    option.state |= QStyle::State_Selected;
                delegate.paint(&painter, option, model.index(row));
                painter.end();
                QVERIFY(!image.isNull());
            }
        }
    }
}

} // namespace

class CardDelegateTest : public QObject {
    Q_OBJECT

private slots:
    void badgeTexts()
    {
        const QDate today = test::today();
        const qint64 now = test::t0();
        const qint64 old = now - 12 * kDayMs;
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 14), old), today, now), QString("overdue · 12d"));
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 15), old), today, now), QString("today · 12d"));
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 20), old), today, now),
                 QLocale().toString(QDate(2026, 10, 20), "d MMM") + " · 12d");
        QCOMPARE(CardDelegate::badgeText(active(std::nullopt, old), today, now), QString("12d"));
        ScratchSummary done = active(QDate(2026, 10, 1), old);
        done.status = Status::Done;
        QCOMPARE(CardDelegate::badgeText(done, today, now), QString());
    }

    // Every active card says how long it has been on the board (hours on its first day).
    void ageTexts()
    {
        const QDate today = test::today();
        const qint64 now = test::t0();
        const qint64 hourMs = 3'600'000;
        QCOMPARE(CardDelegate::ageText(now, today, now), QString("now"));
        QCOMPARE(CardDelegate::ageText(now - 59 * 60'000, today, now), QString("now"));
        QCOMPARE(CardDelegate::ageText(now - 3 * hourMs, today, now), QString("3h"));
        QCOMPARE(CardDelegate::ageText(now + hourMs, today, now), QString("now")); // clock skew
        QCOMPARE(CardDelegate::ageText(now - kDayMs, today, now), QString("1d"));
        QCOMPARE(CardDelegate::ageText(now - 40 * kDayMs, today, now), QString("40d"));
        QCOMPARE(CardDelegate::badgeText(active(std::nullopt, now - 3 * hourMs), today, now), QString("3h"));
    }

    void sizeHintIsFixed()
    {
        auto f = test::fixture();
        f->store->createScratch(test::html({"one"}));
        BoardModel model(*f->store, [] { return test::today(); });
        model.reload();
        CardDelegate delegate(nullptr);
        QCOMPARE(delegate.sizeHint(QStyleOptionViewItem(), model.index(0)), CardDelegate::kCardSize);
    }

    void tintsAreReadable()
    {
        for (Color c : {Color::Red, Color::Orange, Color::Yellow, Color::Green, Color::Blue, Color::Purple}) {
            QVERIFY2(CardDelegate::tint(c, false).lightness() >= 200, qPrintable(colorName(c)));
            QVERIFY2(CardDelegate::tint(c, true).lightness() <= 90, qPrintable(colorName(c)));
        }
        QVERIFY(!CardDelegate::tint(std::nullopt, false).isValid());
        QVERIFY(!CardDelegate::tint(std::nullopt, true).isValid());
    }

    // An uncoloured card stands out from the grid's background (Base) in both themes.
    void plainCardsHaveVisibleEdges()
    {
        for (const QPalette& palette : {QPalette(), darkPalette()}) {
            const int base = palette.color(QPalette::Base).lightness();
            const int fill = CardDelegate::cardFill(std::nullopt, palette).lightness();
            const int border = CardDelegate::cardBorder(palette).lightness();
            QVERIFY2(std::abs(fill - base) >= 8, qPrintable(QString("fill %1 base %2").arg(fill).arg(base)));
            QVERIFY2(std::abs(border - base) >= 40, qPrintable(QString("border %1 base %2").arg(border).arg(base)));
        }
        QCOMPARE(CardDelegate::cardFill(Color::Blue, darkPalette()), CardDelegate::tint(Color::Blue, true));
        QCOMPARE(CardDelegate::cardFill(Color::Blue, QPalette()), CardDelegate::tint(Color::Blue, false));
    }

    void paintsCardsWithoutCrashing()
    {
        auto f = test::fixture();
        StoreAttachments attachments(*f->store);
        f->store->createScratch(test::html({"plain"}));
        const QString image = f->store->putAttachment(pngBytes(), "png");
        f->store->createScratch(test::htmlWithImage("with image", image));
        f->store->createScratch(test::htmlWithImage("missing image", test::fakeAttachmentId())); // never stored
        f->store->createScratch(test::html({QString(200, 'T')}));
        const ScratchId late = f->store->createScratch(test::html({"late"})).id;
        f->store->setDue(late, QDate(2026, 10, 1));
        f->store->setColor(late, Color::Blue);

        BoardModel model(*f->store, [] { return test::today(); });
        model.reload();
        QCOMPARE(model.rowCount(), 5);
        CardDelegate delegate(&attachments);
        paintAll(model, delegate);
    }

    void paintsStacksWithoutCrashing()
    {
        auto f = test::fixture();
        const ScratchId b = f->store->createScratch(test::html({"b"})).id;
        const ScratchId c = f->store->createScratch(test::html({"c"})).id;
        const BundleId bundle = f->store->bundleTogether(b, c);
        BoardModel model(*f->store, [] { return test::today(); });
        model.reload();
        CardDelegate delegate(nullptr);
        paintAll(model, delegate); // collapsed stack
        model.setExpanded(bundle, true);
        QCOMPARE(model.rowCount(), 3);
        paintAll(model, delegate); // expanded stack plus in-bundle cards
    }

    void stackLinesNameCountTitles()
    {
        auto f = test::fixture();
        const ScratchId b = f->store->createScratch(test::html({"b"})).id;
        const ScratchId c = f->store->createScratch(test::html({"c"})).id;
        f->store->bundleTogether(b, c);
        BoardModel model(*f->store, [] { return test::today(); });
        model.reload();
        QCOMPARE(CardDelegate::stackLines(model.row(0)), QStringList({"b", "2 notes", "b", "c"}));
    }

    // Review Focus 5.
    void paintsHostileText()
    {
        auto f = test::fixture();
        const QStringList titles = {QString(300, 'x'), QString::fromUtf8("🚀🔥 emoji 🎉"), QString::fromUtf8("שלום עולם")};
        for (const QString& t : titles)
            f->store->createScratch(test::html({t}));
        BoardModel model(*f->store, [] { return test::today(); });
        model.reload();
        CardDelegate delegate(nullptr);
        paintAll(model, delegate);

        QFont bold;
        bold.setBold(true);
        const int width = CardDelegate::titleWidth();
        QVERIFY(width > 0 && width < CardDelegate::kCardSize.width());
        for (const QString& t : titles) {
            const QString elided = CardDelegate::elidedTitle(t, bold, width);
            QVERIFY2(QFontMetrics(bold).horizontalAdvance(elided) <= width, qPrintable(elided));
        }
    }
};

NOITE_TEST(CardDelegateTest)
#include "tst_card_delegate.moc"
