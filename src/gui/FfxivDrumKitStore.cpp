#include "FfxivDrumKitStore.h"

#include "../AppPaths.h"

#include <QCoreApplication>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace {

// Phase 45 step 4: the scope decision (and the test seam - on Windows this
// is the registry, which QStandardPaths test mode does not cover) lives in
// AppPaths now. Tests redirect via AppPaths::setSettingsScopeForTests, the
// ONE central seam, instead of a per-service copy.
inline std::unique_ptr<QSettings> drumKitSettings() {
    return AppPaths::settings();
}

const QString kIndexKey = QStringLiteral("FFXIV/drumKitIndex");
const QString kKitsGroup = QStringLiteral("FFXIV/drumKits");

} // namespace

FfxivDrumKitStore *FfxivDrumKitStore::instance() {
    static FfxivDrumKitStore store;
    return &store;
}

QList<QPair<QString, int>> FfxivDrumKitStore::requiredGroups() {
    // Derived from the first shipped kit so the skeleton has a single source
    // of truth: adding a percussion instrument to the built-ins extends the
    // contract automatically instead of drifting from a second hardcoded list.
    QList<QPair<QString, int>> groups;
    const QList<FfxivDrumMapPreset> builtins = FfxivDrumMapPreset::presets();
    if (!builtins.isEmpty()) {
        for (const FfxivDrumMapGroup &g : builtins.first().groups)
            groups.append(qMakePair(g.trackName, g.programNumber));
    }
    return groups;
}

bool FfxivDrumKitStore::isBuiltinName(const QString &name) {
    // Case-insensitive: QSettings key lookups are case-insensitive on the
    // Windows registry backend (and in INI files), so "mog amp" would end up
    // sharing storage with a differently-cased sibling.
    for (const FfxivDrumMapPreset &p : FfxivDrumMapPreset::presets()) {
        if (QString::compare(p.name, name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString FfxivDrumKitStore::existingUserKitName(const QString &name) const {
    const QString trimmed = name.trimmed();
    for (const QString &existing : userKitNames()) {
        if (QString::compare(existing, trimmed, Qt::CaseInsensitive) == 0)
            return existing;
    }
    return QString();
}

QStringList FfxivDrumKitStore::userKitNames() const {
    auto settingsPtr = drumKitSettings();
    QSettings &settings = *settingsPtr;
    QStringList names = settings.value(kIndexKey).toStringList();
    // Safety net for kits written by a build whose index went missing:
    // fold in whatever the child-group scan can see (see header note).
    settings.beginGroup(kKitsGroup);
    const QStringList scanned = settings.childGroups();
    settings.endGroup();
    for (const QString &g : scanned) {
        if (!names.contains(g)) names.append(g);
    }
    // A user kit can never carry a built-in's name; drop any that slipped in
    // so the combo cannot show the same name twice.
    for (int i = names.size() - 1; i >= 0; --i) {
        if (names[i].isEmpty() || isBuiltinName(names[i])) names.removeAt(i);
    }
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b) {
                  return a.localeAwareCompare(b) < 0;
              });
    return names;
}

QList<FfxivDrumMapPreset> FfxivDrumKitStore::allKits() const {
    QList<FfxivDrumMapPreset> kits = FfxivDrumMapPreset::presets();
    auto settingsPtr = drumKitSettings();
    QSettings &settings = *settingsPtr;
    for (const QString &name : userKitNames()) {
        FfxivDrumMapPreset kit;
        kit.name = name;
        const QString base = kKitsGroup + QLatin1Char('/') + name;
        for (const QPair<QString, int> &g : requiredGroups()) {
            FfxivDrumMapGroup group;
            group.trackName = g.first;
            group.programNumber = g.second;
            group.mappings = decodeMappings(
                settings.value(base + QLatin1Char('/') + g.first).toStringList());
            kit.groups.append(group);
        }
        kits.append(kit);
    }
    return kits;
}

FfxivDrumMapPreset FfxivDrumKitStore::kitByName(const QString &name) const {
    for (const FfxivDrumMapPreset &p : allKits()) {
        if (p.name == name) return p;
    }
    return FfxivDrumMapPreset();
}

QString FfxivDrumKitStore::validate(const FfxivDrumMapPreset &kit) {
    const QString trimmed = kit.name.trimmed();
    if (trimmed.isEmpty()) {
        return QCoreApplication::translate("FfxivDrumKitStore",
            "The kit needs a name.");
    }
    if (isBuiltinName(trimmed)) {
        return QCoreApplication::translate("FfxivDrumKitStore",
            "\"%1\" is a shipped kit and cannot be overwritten - save it "
            "under a different name.").arg(trimmed);
    }
    // QSettings uses '/' as its group separator, so a name carrying one
    // would silently split into a nested group and never be found again.
    if (trimmed.contains(QLatin1Char('/')) || trimmed.contains(QLatin1Char('\\'))) {
        return QCoreApplication::translate("FfxivDrumKitStore",
            "The kit name cannot contain slashes.");
    }

    const QList<QPair<QString, int>> required = requiredGroups();
    if (kit.groups.size() != required.size()) {
        return QCoreApplication::translate("FfxivDrumKitStore",
            "A kit must carry exactly the %1 FFXIV percussion groups.")
            .arg(required.size());
    }
    for (int i = 0; i < required.size(); ++i) {
        const FfxivDrumMapGroup &g = kit.groups.at(i);
        // The track name and program are how playback and export find the
        // FFXIV percussion preset - they are not the user's to change.
        if (g.trackName != required.at(i).first
            || g.programNumber != required.at(i).second) {
            return QCoreApplication::translate("FfxivDrumKitStore",
                "Group %1 must stay \"%2\" with program %3.")
                .arg(i + 1)
                .arg(required.at(i).first)
                .arg(required.at(i).second);
        }
    }

    int mappingCount = 0;
    QSet<int> seenSources;
    for (const FfxivDrumMapGroup &g : kit.groups) {
        for (const FfxivDrumNoteMap &m : g.mappings) {
            if (m.sourceNote < kMinSource || m.sourceNote > kMaxSource) {
                return QCoreApplication::translate("FfxivDrumKitStore",
                    "%1: source note %2 is outside the GM drum range %3-%4.")
                    .arg(g.trackName).arg(m.sourceNote)
                    .arg(kMinSource).arg(kMaxSource);
            }
            if (m.targetNote < kMinTarget || m.targetNote > kMaxTarget) {
                return QCoreApplication::translate("FfxivDrumKitStore",
                    "%1: target pitch %2 is outside the playable range %3-%4.")
                    .arg(g.trackName).arg(m.targetNote)
                    .arg(kMinTarget).arg(kMaxTarget);
            }
            // One drum cannot land on two instruments at once, and mapping it
            // twice inside one group makes the second entry dead weight.
            if (seenSources.contains(m.sourceNote)) {
                return QCoreApplication::translate("FfxivDrumKitStore",
                    "Source note %1 is mapped more than once - every drum can "
                    "only go to one place.").arg(m.sourceNote);
            }
            seenSources.insert(m.sourceNote);
            ++mappingCount;
        }
    }
    if (mappingCount == 0) {
        return QCoreApplication::translate("FfxivDrumKitStore",
            "The kit has no mappings at all.");
    }
    return QString();
}

bool FfxivDrumKitStore::saveKit(const FfxivDrumMapPreset &kit, QString *error) {
    const QString reason = validate(kit);
    if (!reason.isEmpty()) {
        if (error) *error = reason;
        return false;
    }
    const QString name = kit.name.trimmed();

    // A case-VARIANT of an existing kit would silently share (and here:
    // overwrite) that kit's storage on the case-insensitive backends, and the
    // index would end up with two names pointing at one dataset. Same-case
    // saves stay the documented insert-or-overwrite.
    const QString variant = existingUserKitName(name);
    if (!variant.isEmpty() && variant != name) {
        if (error) {
            *error = QCoreApplication::translate("FfxivDrumKitStore",
                "A kit named \"%1\" already exists - kit names only differing "
                "in upper/lower case would share the same storage. Use that "
                "exact name to overwrite it, or pick a different one.")
                .arg(variant);
        }
        return false;
    }

    auto settingsPtr = drumKitSettings();
    QSettings &settings = *settingsPtr;
    // Explicit beginGroup so Qt registers the kit as a real child group; a
    // path-style setValue() writes fine but is not always enumerable.
    settings.beginGroup(kKitsGroup);
    settings.beginGroup(name);
    settings.remove(QString()); // drop stale groups from a previous save
    for (const FfxivDrumMapGroup &g : kit.groups)
        settings.setValue(g.trackName, encodeMappings(g.mappings));
    settings.endGroup();
    settings.endGroup();

    QStringList index = settings.value(kIndexKey).toStringList();
    if (!index.contains(name)) {
        index.append(name);
        settings.setValue(kIndexKey, index);
    }
    settings.sync();
    if (error) error->clear();
    return true;
}

bool FfxivDrumKitStore::deleteKit(const QString &name, QString *error) {
    if (isBuiltinName(name)) {
        if (error) {
            *error = QCoreApplication::translate("FfxivDrumKitStore",
                "\"%1\" is a shipped kit and cannot be deleted.").arg(name);
        }
        return false;
    }
    auto settingsPtr = drumKitSettings();
    QSettings &settings = *settingsPtr;
    settings.beginGroup(kKitsGroup);
    settings.remove(name);
    settings.endGroup();

    QStringList index = settings.value(kIndexKey).toStringList();
    const bool removed = index.removeAll(name) > 0;
    settings.setValue(kIndexKey, index);
    settings.sync();
    if (error) error->clear();
    // Removing something that was not there is not an error worth surfacing,
    // but report it so a caller can tell "gone" from "never existed".
    return removed;
}

void FfxivDrumKitStore::clearUserKits() {
    auto settingsPtr = drumKitSettings();
    QSettings &settings = *settingsPtr;
    settings.remove(kKitsGroup);
    settings.remove(kIndexKey);
    settings.sync();
}

QStringList FfxivDrumKitStore::encodeMappings(
    const QList<FfxivDrumNoteMap> &mappings) {
    // "source:target" keeps the INI readable and hand-editable.
    QStringList out;
    out.reserve(mappings.size());
    for (const FfxivDrumNoteMap &m : mappings) {
        out.append(QStringLiteral("%1:%2").arg(m.sourceNote).arg(m.targetNote));
    }
    return out;
}

QList<FfxivDrumNoteMap> FfxivDrumKitStore::decodeMappings(
    const QStringList &encoded) {
    QList<FfxivDrumNoteMap> out;
    for (const QString &entry : encoded) {
        const QStringList parts = entry.split(QLatin1Char(':'));
        if (parts.size() != 2) continue;
        bool okSource = false, okTarget = false;
        const int source = parts[0].toInt(&okSource);
        const int target = parts[1].toInt(&okTarget);
        if (!okSource || !okTarget) continue;
        // Drop anything out of range instead of trusting the file: a
        // hand-edited INI must not be able to transpose drums outside the
        // playable range.
        if (source < kMinSource || source > kMaxSource) continue;
        if (target < kMinTarget || target > kMaxTarget) continue;
        out.append({source, target});
    }
    return out;
}
