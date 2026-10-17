#include "registry.h"

#include "App.h"

#include <QTest>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void appLibraryLinks()
    {
        App app(AppOptions{});
        Q_UNUSED(app);
    }
};

NOITE_TEST(SmokeTest)
#include "tst_smoke.moc"
