#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>

namespace {
QString s_testOrg;
QString s_testApp;
int s_portable = -1; // -1 = undecided (needs QCoreApplication for arguments())
} // namespace

namespace AppPaths {

QString dataDir() {
#ifdef Q_OS_WIN
    // The portable promise: everything lives next to the exe, exactly as it
    // always has. test_app_paths guards this - a copied folder must keep
    // carrying its soundfonts, logs and prompt files.
    return QCoreApplication::applicationDirPath();
#else
    // Issue #13: a signed .app bundle cannot write next to itself. Standard
    // per-user data location instead (e.g. ~/Library/Application Support/...
    // on macOS), created on demand.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        dir = QCoreApplication::applicationDirPath(); // last resort
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
        const bool marker = QFile::exists(
            QCoreApplication::applicationDirPath() + QStringLiteral("/portable.txt"));
        const bool flag = QCoreApplication::arguments()
                              .contains(QStringLiteral("--portable"));
        s_portable = (marker || flag) ? 1 : 0;
    }
    return s_portable == 1;
}

void initSettings() {
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

void setSettingsScopeForTests(const QString &organization,
                              const QString &application) {
    s_testOrg = organization;
    s_testApp = application;
}

} // namespace AppPaths
