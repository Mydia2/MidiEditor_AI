#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <vector>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <mach-o/dyld.h>
#endif

namespace {
QString s_testOrg;
QString s_testApp;
QString s_exeDir;    // resolved by initSettings; works pre-QApplication
QString s_exeDirTestOverride;
int s_portable = -1; // -1 = undecided

// The running executable's full path, straight from the OS.
//
// Deliberately NOT argv[0] (v2.2 review): Qt's WinMain shim rebuilds argv via
// WideCharToMultiByte(CP_ACP, ...) and this build ships no UTF-8
// activeCodePage manifest, so an exe path containing characters outside the
// system ANSI codepage (CJK/Cyrillic profile name, emoji folder) arrives with
// '?' - soundFontsDir() could then not even be created. And argv[0] carries no
// directory at all when the program is started by bare name from PATH or by a
// wrapper, which silently turned the CWD into the data directory (log, prompt
// file and soundfonts/ created there, bundled SoundFont and portable.txt next
// to the exe missed).
//
// Native and not Qt because initSettings() must run before QApplication.
QString nativeExecutablePath() {
#if defined(Q_OS_WIN)
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (written == 0) {
            return QString(); // hard failure - caller falls back to argv[0]
        }
        if (written < buf.size()) {
            return QString::fromWCharArray(buf.data(), static_cast<int>(written));
        }
        if (buf.size() >= 32768) {
            return QString(); // longer than any legal Windows path
        }
        buf.resize(buf.size() * 2); // truncated - retry with room
    }
#elif defined(Q_OS_MACOS)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // fails on purpose, reports the size
    if (size == 0) {
        return QString();
    }
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        return QString();
    }
    return QFileInfo(QFile::decodeName(QByteArray(buf.data()))).absoluteFilePath();
#elif defined(Q_OS_LINUX)
    const QString self = QFile::symLinkTarget(QStringLiteral("/proc/self/exe"));
    return self;
#else
    return QString();
#endif
}

// Directory part of nativeExecutablePath(), empty when the OS query failed.
QString nativeExeDir() {
    const QString exe = nativeExecutablePath();
    if (exe.isEmpty()) {
        return QString();
    }
    return QFileInfo(exe).absolutePath();
}
} // namespace

namespace AppPaths {

QString exeDir() {
    // The test seam wins over everything: test_app_paths drives initSettings()
    // against a QTemporaryDir, and the test binary's own folder must not leak
    // into that.
    if (!s_exeDirTestOverride.isEmpty()) {
        return s_exeDirTestOverride;
    }
    // Once Qt is up its own resolution is authoritative (and uses the same OS
    // queries as nativeExecutablePath(), so the two must agree).
    if (QCoreApplication::instance()) {
        const QString qtDir = QCoreApplication::applicationDirPath();
        if (!qtDir.isEmpty()) {
#ifndef QT_NO_DEBUG
            if (!s_exeDir.isEmpty()
                && QFileInfo(s_exeDir).absoluteFilePath()
                       != QFileInfo(qtDir).absoluteFilePath()) {
                qWarning("AppPaths: exe dir resolved pre-QApplication as \"%s\" "
                         "but Qt reports \"%s\"",
                         qUtf8Printable(s_exeDir), qUtf8Printable(qtDir));
            }
#endif
            return qtDir;
        }
    }
    // Pre-QApplication (initSettings) or a Qt that has no path for us.
    if (s_exeDir.isEmpty()) {
        s_exeDir = nativeExeDir();
    }
    return s_exeDir;
}

QString exeDirFromArgv(int argc, char *argv[]) {
    if (argc <= 0 || !argv || !argv[0] || !argv[0][0]) {
        return QString();
    }
    const QString arg0 = QString::fromLocal8Bit(argv[0]);
    if (arg0.contains(QLatin1Char('/')) || arg0.contains(QLatin1Char('\\'))) {
        return QFileInfo(arg0).absolutePath();
    }
    // Bare name: the shell found it on PATH, so we can too. Deliberately NOT
    // falling back to the CWD - that is the failure this function exists to
    // avoid.
    const QString onPath = QStandardPaths::findExecutable(arg0);
    if (!onPath.isEmpty()) {
        return QFileInfo(onPath).absolutePath();
    }
    return QString();
}

QString dataDir() {
    // Portable mode: next to the exe on EVERY platform - that is what
    // "portable" means (the stick carries config, logs and soundfonts as
    // one folder). APPPATHS-NAME-001: this also sidesteps the
    // applicationName-dependent AppData path, which is not even set yet
    // when initSettings runs.
    if (isPortable()) {
        return exeDir();
    }
#ifdef Q_OS_WIN
    // The portable promise: everything lives next to the exe, exactly as it
    // always has. test_app_paths guards this - a copied folder must keep
    // carrying its soundfonts, logs and prompt files.
    return exeDir();
#else
    // Issue #13: a signed .app bundle cannot write next to itself. Standard
    // per-user data location instead (e.g. ~/Library/Application Support/...
    // on macOS), created on demand.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = exeDir(); // last resort
    }
    QDir().mkpath(dir);
    return dir;
#endif
}

QString soundFontsDir() {
    QDir dir(dataDir());
    if (!dir.exists(QStringLiteral("soundfonts"))) {
        dir.mkpath(QStringLiteral("soundfonts"));
    }
    return dir.absoluteFilePath(QStringLiteral("soundfonts"));
}

QString dataFilePath(const QString &fileName) {
    return dataDir() + QLatin1Char('/') + fileName;
}

bool isPortable() {
    if (s_portable < 0) {
        // Fallback when initSettings was not called (tests): marker only.
        s_portable = QFile::exists(exeDir() + QStringLiteral("/portable.txt"))
                         ? 1 : 0;
    }
    return s_portable == 1;
}

void initSettings(int argc, char *argv[]) {
    // Pre-QApplication: resolve the exe dir ourselves and scan argv for
    // --portable ourselves (QCoreApplication::arguments() needs an app
    // instance that deliberately does not exist yet).
    //
    // OS query first, argv[0] only as a last resort: see nativeExecutablePath()
    // for the two argv[0] failure modes (ANSI-mangled path, no directory at
    // all) that used to put the data directory in the CWD.
    if (s_exeDirTestOverride.isEmpty()) {
        s_exeDir = nativeExeDir();
        if (s_exeDir.isEmpty()) {
            s_exeDir = exeDirFromArgv(argc, argv);
        }
    }
    bool flag = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && qstrcmp(argv[i], "--portable") == 0) {
            flag = true;
        }
    }
    const bool marker =
        QFile::exists(exeDir() + QStringLiteral("/portable.txt"));
    s_portable = (marker || flag) ? 1 : 0;

    if (!isPortable()) {
        return;
    }
    // Redirect BOTH construction styles the codebase uses:
    //  - default-constructed QSettings() follow the default format + path,
    //  - the explicit sites go through settings() below, which builds the
    //    matching IniFormat/UserScope object.
    // Result file: <dataDir>/config/MidiEditor/NONE.ini.
    const QString configDir = dataDir() + QStringLiteral("/config");
    QDir().mkpath(configDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Under the test seams there is nothing to migrate: copying the real
    // store into a throwaway directory would drag the developer's provider
    // configuration (API keys included) into a temp folder.
    if (!s_testOrg.isEmpty() || !s_exeDirTestOverride.isEmpty()) {
        return;
    }

    // Migration, copy-on-first-use: an empty INI plus a populated native
    // scope means this install just went portable - carry the settings
    // over ONCE. Afterwards the INI is the truth and the registry is left
    // alone (deliberately not deleted: switching back keeps working).
    QSettings ini(QSettings::IniFormat, QSettings::UserScope,
                  QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    if (ini.allKeys().isEmpty()) {
        QSettings native(QSettings::NativeFormat, QSettings::UserScope,
                         QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        const QStringList keys = native.allKeys();
        for (const QString &key : keys) {
            ini.setValue(key, native.value(key));
        }
        ini.sync();
    }
}

std::unique_ptr<QSettings> settings() {
    if (!s_testOrg.isEmpty()) {
        return std::make_unique<QSettings>(s_testOrg, s_testApp);
    }
    if (isPortable()) {
        return std::make_unique<QSettings>(
            QSettings::IniFormat, QSettings::UserScope,
            QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    }
    return std::make_unique<QSettings>(QStringLiteral("MidiEditor"),
                                       QStringLiteral("NONE"));
}

QStringList relaunchArgs() {
    // The switch and portable.txt are documented as equivalent, so a copy
    // started with `--portable` alone has no marker next to the exe. Every
    // self-relaunch (theme-change restart, auto-updater) must therefore
    // re-pass the switch or the new process comes up on the system settings
    // store and reads a different configuration than the one just written.
    if (isPortable()) {
        return QStringList{QStringLiteral("--portable")};
    }
    return QStringList();
}

void setSettingsScopeForTests(const QString &organization,
                              const QString &application) {
    s_testOrg = organization;
    s_testApp = application;
}

void setExeDirForTests(const QString &dir) {
    s_exeDirTestOverride = dir;
    s_portable = -1; // re-decide against the new (or the real) directory
}

} // namespace AppPaths
