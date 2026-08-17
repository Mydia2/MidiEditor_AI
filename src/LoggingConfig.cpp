/*
 * MidiEditor AI - LoggingConfig implementation.
 */

#include "LoggingConfig.h"

#include "AppPaths.h"

#include <QLoggingCategory>
#include <QSettings>
#include <QStringList>

namespace {
const char *kLevelKey = "Logging/level";
const char *kPerCatKey = "Logging/perCategory";
const char *kCollabVerboseKey = "Collab/verboseLogging";

std::unique_ptr<QSettings> appSettings() {
    // Same store as RtcRendezvousClient / CollabSettingsWidget so all
    // settings end up in one place - which AppPaths decides (Phase 45:
    // native scope normally, INI next to the exe in portable mode).
    return AppPaths::settings();
}
} // namespace

QString LoggingConfig::buildFilterRules(Level level, const QString &perCategoryOverrides) {
    // Each "*.<sev>=false" line silences that severity globally; the rest
    // (and everything qCDebug-style emits) stays at its default unless a
    // per-category override below disables / re-enables it.
    QStringList rules;
    switch (level) {
    case Level::Off:
        rules << QStringLiteral("*.debug=false");
        rules << QStringLiteral("*.info=false");
        rules << QStringLiteral("*.warning=false");
        rules << QStringLiteral("*.critical=false");
        break;
    case Level::Errors:
        rules << QStringLiteral("*.debug=false");
        rules << QStringLiteral("*.info=false");
        rules << QStringLiteral("*.warning=false");
        break;
    case Level::Warnings:
        rules << QStringLiteral("*.debug=false");
        rules << QStringLiteral("*.info=false");
        break;
    case Level::Info:
        rules << QStringLiteral("*.debug=false");
        break;
    case Level::Debug:
        rules << QStringLiteral("*=true");
        break;
    }
    if (!perCategoryOverrides.trimmed().isEmpty()) {
        rules << perCategoryOverrides.trimmed();
    }
    // Collab verbose flag is layered last so it overrides the global
    // level for collab categories specifically. Order matters in Qt's
    // filter-rules evaluation — later wins.
    if (loadCollabVerbose()) {
        rules << QStringLiteral("midieditor.collab.*=true");
    }
    return rules.join(QStringLiteral("\n"));
}

LoggingConfig::Level LoggingConfig::loadLevel() {
    auto sp = appSettings();
    QSettings &s = *sp;
    int raw = s.value(QLatin1String(kLevelKey),
                      static_cast<int>(Level::Warnings)).toInt();
    if (raw < 0 || raw > 4) raw = static_cast<int>(Level::Warnings);
    return static_cast<Level>(raw);
}

QString LoggingConfig::loadPerCategory() {
    auto sp = appSettings();
    QSettings &s = *sp;
    return s.value(QLatin1String(kPerCatKey)).toString();
}

void LoggingConfig::applyFromSettings() {
    QString rules = buildFilterRules(loadLevel(), loadPerCategory());
    QLoggingCategory::setFilterRules(rules);
}

void LoggingConfig::applyAndPersist(Level level, const QString &perCategoryOverrides) {
    auto sp = appSettings();
    QSettings &s = *sp;
    s.setValue(QLatin1String(kLevelKey), static_cast<int>(level));
    if (perCategoryOverrides.trimmed().isEmpty()) {
        s.remove(QLatin1String(kPerCatKey));
    } else {
        s.setValue(QLatin1String(kPerCatKey), perCategoryOverrides.trimmed());
    }
    QLoggingCategory::setFilterRules(buildFilterRules(level, perCategoryOverrides));
}

bool LoggingConfig::loadCollabVerbose() {
    return appSettings()->value(QLatin1String(kCollabVerboseKey), false).toBool();
}

void LoggingConfig::setCollabVerbose(bool on) {
    auto sp = appSettings();
    QSettings &s = *sp;
    if (on) {
        s.setValue(QLatin1String(kCollabVerboseKey), true);
    } else {
        s.remove(QLatin1String(kCollabVerboseKey));
    }
    // Re-apply with the current level + per-category so the change
    // takes effect immediately. buildFilterRules() picks up the new
    // collab-verbose value via loadCollabVerbose().
    QLoggingCategory::setFilterRules(
        buildFilterRules(loadLevel(), loadPerCategory()));
}
