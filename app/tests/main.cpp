// Runs every registered app test class (same registry as core/tests). With
// NOITE_TEST_OUT=<dir>, each class writes <dir>/<Class>.txt.
#include "registry.h"

#include <QApplication>
#include <QTest>

#include <memory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    const QString outDir = qEnvironmentVariable("NOITE_TEST_OUT");
    int failures = 0;
    for (const auto& entry : noite::test::registry()) {
        std::unique_ptr<QObject> test(entry.make());
        QStringList args = {QCoreApplication::arguments().first()};
        if (!outDir.isEmpty())
            args << "-o" << outDir + "/" + entry.name + ".txt,txt";
        failures += QTest::qExec(test.get(), args);
    }
    return failures;
}
