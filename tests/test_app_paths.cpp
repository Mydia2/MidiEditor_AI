/*
 * test_app_paths
 *
 * Phase 45 (issue #13): AppPaths is the ONE place that decides where data
 * lives. Two contracts are pinned here:
 *
 *   1. THE PORTABLE PROMISE (Windows): dataDir() is the exe directory,
 *      byte-identical to the historical behaviour - a copied folder keeps
 *      carrying its soundfonts, logs and prompt files. Nobody "improves"
 *      this to AppData without tripping a test.
 *   2. THE ROUTING GUARD: no .cpp outside the whitelist uses
 *      applicationDirPath() - new data files must go through AppPaths, or
 *      the macOS build regresses the moment someone adds an exe-relative
 *      path again. Whitelist: AppPaths.cpp (the owner), AutoUpdater.cpp
 *      (swaps the executable - exe-relative by definition, Windows-only),
 *      MainWindow.cpp (one restart working-directory use).
 *
 * APPPATHS_REPO_ROOT is injected by CMake for the source scan.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSettings>

#include "../src/AppPaths.h"

class TestAppPaths : public QObject {
    Q_OBJECT

private slots:

    // --- 1. the portable promise ------------------------------------------
    void dataDirIsExeDirOnWindows() {
#ifdef Q_OS_WIN
        QCOMPARE(AppPaths::dataDir(), QCoreApplication::applicationDirPath());
#else
        QVERIFY(!AppPaths::dataDir().isEmpty());
        QVERIFY(QDir(AppPaths::dataDir()).exists());
#endif
    }

    void soundFontsDirIsUnderDataDir() {
        const QString sf = AppPaths::soundFontsDir();
        QVERIFY(sf.startsWith(AppPaths::dataDir()));
        QVERIFY(sf.endsWith(QStringLiteral("/soundfonts")));
        QVERIFY(QDir(sf).exists()); // created on demand
    }

    void dataFilePathComposes() {
        QCOMPARE(AppPaths::dataFilePath(QStringLiteral("x.log")),
                 AppPaths::dataDir() + QStringLiteral("/x.log"));
    }

    // --- 2. the central settings seam --------------------------------------
    void settingsSeamRedirects() {
        AppPaths::setSettingsScopeForTests(QStringLiteral("MidiEditorTest"),
                                           QStringLiteral("AppPathsTest"));
        AppPaths::settings()->setValue(QStringLiteral("probe"), 42);
        QCOMPARE(AppPaths::settings()->value(QStringLiteral("probe")).toInt(), 42);
        QSettings(QStringLiteral("MidiEditorTest"),
                  QStringLiteral("AppPathsTest")).clear();
        // Empty scope = seam off; later tests see the normal behaviour.
        AppPaths::setSettingsScopeForTests(QString(), QString());
    }

    // --- 3. the routing guard ---------------------------------------------
    void noStrayApplicationDirPathUses() {
        const QString srcRoot = QStringLiteral(APPPATHS_REPO_ROOT "/src");
        QVERIFY(QDir(srcRoot).exists());

        // file name -> max allowed occurrences
        QHash<QString, int> whitelist;
        whitelist.insert(QStringLiteral("AppPaths.cpp"), 99);
        whitelist.insert(QStringLiteral("AutoUpdater.cpp"), 2);
        whitelist.insert(QStringLiteral("MainWindow.cpp"), 1);

        QStringList offenders;
        QDirIterator it(srcRoot, {QStringLiteral("*.cpp")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QString content = QString::fromUtf8(f.readAll());
            const int count = content.count(QStringLiteral("applicationDirPath"));
            if (count == 0) continue;
            const QString name = QFileInfo(path).fileName();
            if (count > whitelist.value(name, 0)) {
                offenders.append(QStringLiteral("%1 (%2x)").arg(name).arg(count));
            }
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                     "applicationDirPath() outside AppPaths - route these "
                     "through AppPaths or extend the whitelist consciously: %1")
                                .arg(offenders.join(QStringLiteral(", ")))));
    }
};

QTEST_GUILESS_MAIN(TestAppPaths)
#include "test_app_paths.moc"
