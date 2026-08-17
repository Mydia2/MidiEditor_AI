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
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <string>

#include "../src/AppPaths.h"

namespace {

// Restores everything initSettings() mutates process-globally, so the
// portable-mode cases below cannot leak into any other test.
//
// QSettings::setPath() has no getter and therefore cannot be restored, but
// putting defaultFormat back to NativeFormat and re-opening the portable
// decision makes it unreachable: settings() then builds the native scope again.
struct GlobalSettingsStateGuard {
    QSettings::Format format = QSettings::defaultFormat();
    ~GlobalSettingsStateGuard()
    {
        QSettings::setDefaultFormat(format);
        AppPaths::setExeDirForTests(QString()); // also forgets s_portable
    }
};

bool touchFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("\n");
    f.close();
#ifndef Q_OS_WIN
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner
                     | QFileDevice::ExeUser);
#endif
    return true;
}

} // namespace

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

    // PORTABLE-SPLIT-001 (v2.2 review): explicit QSettings("MidiEditor", ...)
    // constructions bypass AppPaths::settings() - in portable mode they write
    // the registry while the rest of the app writes the portable ini, silently
    // forking the config. AppPaths.cpp is the only file allowed to construct
    // that scope. (Default-ctor QSettings() sites are a separate pre-existing
    // scope and are not matched here.)
    void noStrayMidiEditorSettingsConstructions() {
        const QString srcRoot = QStringLiteral(APPPATHS_REPO_ROOT "/src");
        QVERIFY(QDir(srcRoot).exists());

        static const QRegularExpression ctor(QStringLiteral(
            "QSettings\\s*\\(\\s*(?:QStringLiteral\\s*\\(\\s*)?\"MidiEditor\""));

        QStringList offenders;
        QDirIterator it(srcRoot, {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString name = QFileInfo(path).fileName();
            if (name == QStringLiteral("AppPaths.cpp")) continue; // the owner
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QStringList lines =
                QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                const QString trimmed = lines.at(i).trimmed();
                // Prose in comments may legitimately spell the old pattern out.
                if (trimmed.startsWith(QStringLiteral("//"))
                    || trimmed.startsWith(QLatin1Char('*'))) continue;
                if (ctor.match(trimmed).hasMatch()) {
                    offenders.append(QStringLiteral("%1:%2").arg(name).arg(i + 1));
                }
            }
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                     "Direct QSettings(\"MidiEditor\", ...) outside AppPaths - "
                     "use AppPaths::settings() instead: %1")
                                .arg(offenders.join(QStringLiteral(", ")))));
    }

    // --- 4. initSettings(): exe-dir resolution + the portable decision -----
    //
    // Declared LAST on purpose: initSettings() mutates process-global QSettings
    // state (default format + IniFormat path), which GlobalSettingsStateGuard
    // undoes but which must not race the cases above either way.

    // argv[0] carrying a full path resolves to that directory. This is the
    // last-resort branch of the exe-dir resolution (the OS query comes first);
    // it is exposed separately precisely so it can be pinned.
    void exeDirFromArgv_usesTheDirectoryOfAFullPath()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString fakeExe =
            QDir(tmp.path()).absoluteFilePath(QStringLiteral("MidiEditorAI_fake.exe"));
        QVERIFY(touchFile(fakeExe));

        std::string arg0 = fakeExe.toStdString();
        char *argv[] = {arg0.data(), nullptr};
        QCOMPARE(QDir(AppPaths::exeDirFromArgv(1, argv)).canonicalPath(),
                 QDir(tmp.path()).canonicalPath());
    }

    // A bare argv[0] (started by name from PATH, or by a wrapper) must resolve
    // through PATH - and NEVER silently become the current working directory,
    // which is how midieditor_ai.log, system_prompts.json and soundfonts/ used
    // to be created wherever the launcher happened to stand.
    void exeDirFromArgv_bareNameResolvesViaPathNotTheCwd()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString base = QStringLiteral("MidiEditorAI_fake_onpath");
#ifdef Q_OS_WIN
        const QString fileName = base + QStringLiteral(".exe");
        const char sep = ';';
#else
        const QString fileName = base;
        const char sep = ':';
#endif
        QVERIFY(touchFile(QDir(tmp.path()).absoluteFilePath(fileName)));

        const QByteArray savedPath = qgetenv("PATH");
        qputenv("PATH", QDir::toNativeSeparators(tmp.path()).toLocal8Bit()
                            + sep + savedPath);

        std::string bare = base.toStdString();
        char *argv[] = {bare.data(), nullptr};
        const QString resolved = AppPaths::exeDirFromArgv(1, argv);

        std::string missing = std::string("MidiEditorAI_not_on_path_2f4c9");
        char *argvMissing[] = {missing.data(), nullptr};
        const QString unresolvable = AppPaths::exeDirFromArgv(1, argvMissing);

        qputenv("PATH", savedPath);

        QCOMPARE(QDir(resolved).canonicalPath(), QDir(tmp.path()).canonicalPath());
        QVERIFY2(unresolvable.isEmpty(),
                 "An unresolvable bare argv[0] must yield nothing - not the CWD.");

        // Degenerate argv must not resolve to the CWD either.
        char *emptyArg[] = {const_cast<char *>(""), nullptr};
        QVERIFY(AppPaths::exeDirFromArgv(1, emptyArg).isEmpty());
        QVERIFY(AppPaths::exeDirFromArgv(0, nullptr).isEmpty());
    }

    // portable.txt next to the exe: portable on, data next to the exe, settings
    // in <exeDir>/config/MidiEditor/NONE.ini.
    void initSettings_markerFileTurnsPortableOn()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        GlobalSettingsStateGuard guard;

        const QString fakeExe =
            QDir(tmp.path()).absoluteFilePath(QStringLiteral("MidiEditorAI_fake.exe"));
        QVERIFY(touchFile(fakeExe));
        QVERIFY(touchFile(QDir(tmp.path()).absoluteFilePath(QStringLiteral("portable.txt"))));

        AppPaths::setExeDirForTests(tmp.path());
        std::string arg0 = fakeExe.toStdString();
        char *argv[] = {arg0.data(), nullptr};
        AppPaths::initSettings(1, argv);

        QVERIFY(AppPaths::isPortable());
        QCOMPARE(QDir(AppPaths::exeDir()).canonicalPath(),
                 QDir(tmp.path()).canonicalPath());
        QCOMPARE(QDir(AppPaths::dataDir()).canonicalPath(),
                 QDir(tmp.path()).canonicalPath());

        {
            auto s = AppPaths::settings();
            s->setValue(QStringLiteral("probe"), 7);
            s->sync();
        }
        QVERIFY2(QFile::exists(tmp.path()
                               + QStringLiteral("/config/MidiEditor/NONE.ini")),
                 "Portable settings must land in <exeDir>/config/MidiEditor/NONE.ini.");

        // And the relaunch has to carry the mode forward.
        QCOMPARE(AppPaths::relaunchArgs(),
                 QStringList{QStringLiteral("--portable")});
    }

    // The --portable switch alone (no marker file) is documented as equivalent
    // to the marker - including the INI redirection and the relaunch argument.
    void initSettings_switchTurnsPortableOnWithoutAMarker()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        GlobalSettingsStateGuard guard;

        const QString fakeExe =
            QDir(tmp.path()).absoluteFilePath(QStringLiteral("MidiEditorAI_fake.exe"));
        QVERIFY(touchFile(fakeExe));
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/portable.txt")));

        AppPaths::setExeDirForTests(tmp.path());
        std::string arg0 = fakeExe.toStdString();
        std::string flag = "--portable";
        char *argv[] = {arg0.data(), flag.data(), nullptr};
        AppPaths::initSettings(2, argv);

        QVERIFY(AppPaths::isPortable());
        QCOMPARE(QDir(AppPaths::dataDir()).canonicalPath(),
                 QDir(tmp.path()).canonicalPath());
        {
            auto s = AppPaths::settings();
            s->setValue(QStringLiteral("probe"), 7);
            s->sync();
        }
        QVERIFY(QFile::exists(tmp.path()
                              + QStringLiteral("/config/MidiEditor/NONE.ini")));
        QCOMPARE(AppPaths::relaunchArgs(),
                 QStringList{QStringLiteral("--portable")});
    }

    // Neither marker nor switch: nothing changes, and no relaunch argument is
    // added (a regular install must never be told --portable).
    void initSettings_withoutMarkerOrSwitchStaysNonPortable()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        GlobalSettingsStateGuard guard;

        const QString fakeExe =
            QDir(tmp.path()).absoluteFilePath(QStringLiteral("MidiEditorAI_fake.exe"));
        QVERIFY(touchFile(fakeExe));

        AppPaths::setExeDirForTests(tmp.path());
        std::string arg0 = fakeExe.toStdString();
        char *argv[] = {arg0.data(), nullptr};
        AppPaths::initSettings(1, argv);

        QVERIFY(!AppPaths::isPortable());
        QVERIFY(AppPaths::relaunchArgs().isEmpty());
        QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/config")));
        // Deliberately no settings() write here: without portable mode that
        // would go straight into the developer's real configuration.
#ifdef Q_OS_WIN
        QCOMPARE(QDir(AppPaths::dataDir()).canonicalPath(),
                 QDir(tmp.path()).canonicalPath());
#endif
    }
};

QTEST_GUILESS_MAIN(TestAppPaths)
#include "test_app_paths.moc"
