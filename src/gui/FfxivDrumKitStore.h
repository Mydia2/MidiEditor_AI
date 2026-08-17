/*
 * MidiEditor AI
 *
 * FfxivDrumKitStore - v2.1.0 feature #2 (roadmap Phase 35).
 *
 * Persistence for the FFXIV drum pitch-mapping kits. The shipped community
 * kits (FfxivDrumMapPreset::presets()) stay hardcoded and READ-ONLY; user
 * kits are copies stored in QSettings and always start from an existing kit
 * (copy-then-edit - never from an empty table).
 *
 * Persistence layout, modeled on FfxivEqualizerService:
 *   QSettings("MidiEditor", "NONE")
 *     FFXIV/drumKitIndex            -> QStringList of user kit names
 *     FFXIV/drumKits/<name>/<group> -> QStringList of "source:target" pairs
 * The explicit index is authoritative: childGroups() does not reliably
 * enumerate intermediate path components on every QSettings backend (the
 * quirk FfxivEqualizerService::allPresetNames documents), so a scan is only
 * used as a migration safety net.
 *
 * What a user kit may change: the note mappings, nothing else. The group
 * track names and their FFXIV program numbers are a byte-contract with
 * FluidSynthEngine::drumProgramForTrackName - a kit that renames a group or
 * moves a program would silently lose its percussion program on playback and
 * export, so validate() rejects it.
 */

#ifndef FFXIVDRUMKITSTORE_H_
#define FFXIVDRUMKITSTORE_H_

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "DrumKitPreset.h"

class QSettings;

class FfxivDrumKitStore {
public:
    /// Legal target range: the FFXIV bard range C3..C6 the SoundFont covers.
    static constexpr int kMinTarget = 48;
    static constexpr int kMaxTarget = 84;
    /// Legal source range: the GM percussion key map on channel 9.
    static constexpr int kMinSource = 35;
    static constexpr int kMaxSource = 81;

    static FfxivDrumKitStore *instance();

    /// Every kit the split dialog offers: the built-ins first (in their
    /// shipped order), then the user kits sorted by name.
    QList<FfxivDrumMapPreset> allKits() const;

    /// User kit names only, sorted.
    QStringList userKitNames() const;

    /// True when `name` belongs to a shipped kit and is therefore read-only.
    static bool isBuiltinName(const QString &name);

    /// Look a kit up by name (built-in or user). Returns an empty preset
    /// (name.isEmpty()) when nothing matches.
    FfxivDrumMapPreset kitByName(const QString &name) const;

    /**
     * \brief Validates a kit the user is about to save.
     * \return An empty string when the kit is valid, otherwise a
     *         human-readable reason suitable for a dialog.
     *
     * Enforced (the rules the tests pinned before this was reachable from
     * the UI): a non-empty name that is not a built-in's, the fixed group
     * set with their fixed program numbers, source notes in [35, 81],
     * target notes in [48, 84], at least one mapping, and no source note
     * mapped twice anywhere in the kit (a drum can only go to one place).
     */
    static QString validate(const FfxivDrumMapPreset &kit);

    /// Persist a user kit (insert or overwrite). Refuses built-in names and
    /// anything validate() rejects; on refusal `error` carries the reason.
    bool saveKit(const FfxivDrumMapPreset &kit, QString *error = nullptr);

    /// Remove a user kit. Built-ins cannot be deleted.
    bool deleteKit(const QString &name, QString *error = nullptr);

    /// Test seam: drop every user kit (used by the unit tests to start from
    /// a known state, mirroring test_ffxiv_equalizer_service).
    void clearUserKits();

    // Phase 45 step 4: the former per-service setSettingsScopeForTests seam
    // moved to AppPaths::setSettingsScopeForTests - ONE central seam. Tests
    // redirect there; the reason (Windows registry is not covered by
    // QStandardPaths test mode) is documented on AppPaths.

    /// Case-insensitive lookup of an existing USER kit name (QSettings key
    /// lookups are case-insensitive on the Windows registry backend, so two
    /// case-variant names would silently share one dataset). Returns the
    /// stored spelling, or an empty string.
    QString existingUserKitName(const QString &name) const;

private:
    FfxivDrumKitStore() = default;
    Q_DISABLE_COPY(FfxivDrumKitStore)

    /// The group skeleton (names + programs) every kit must carry, taken
    /// from the shipped kits so there is one source of truth.
    static QList<QPair<QString, int>> requiredGroups();

    static QStringList encodeMappings(const QList<FfxivDrumNoteMap> &mappings);
    static QList<FfxivDrumNoteMap> decodeMappings(const QStringList &encoded);
};

#endif // FFXIVDRUMKITSTORE_H_
