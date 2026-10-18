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
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 14)), today), QString("overdue"));
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 15)), today), QString("today"));
        QCOMPARE(CardDelegate::badgeText(active(QDate(2026, 10, 20)), today),
                 QLocale().toString(QDate(2026, 10, 20), "d MMM"));
        QCOMPARE(CardDelegate::badgeText(active(std::nullopt, test::t0() - 12 * kDayMs), today), QString("12d"));
        QCOMPARE(CardDelegate::badgeText(active(std::nullopt), today), QString());
        ScratchSummary done = active(QDate(2026, 10, 1));
        done.status = Status::Done;
        QCOMPARE(CardDelegate::badgeText(done, today), QString());
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
