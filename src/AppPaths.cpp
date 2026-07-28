#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

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

} // namespace AppPaths
