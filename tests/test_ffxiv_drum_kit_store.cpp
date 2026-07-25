/*
 * test_ffxiv_drum_kit_store
 *
 * Unit tests for src/gui/FfxivDrumKitStore - the persistence and validation
 * layer behind the v2.1.0 drum-kit editor (roadmap Phase 35).
 *
 * WHY these cases: the store is the only thing standing between a hand-made
 * kit and the playback/export path. Group track names and program numbers are
 * a byte-contract with FluidSynthEngine::drumProgramForTrackName, and target
 * pitches outside C3..C6 are not playable by an FFXIV bard - a kit that
 * violates either would fail silently at performance time, which is the worst
 * possible moment. Validation used to live in the tests only (as assertions
 * about the shipped kits); now that users can author kits it is enforced at
 * runtime, and these cases pin that enforcement.
 *
 * Coverage:
 *   1. builtinsAreReadOnly - the shipped kits are recognised as built-in,
 *      cannot be saved over and cannot be deleted.
 *   2. roundTripPreservesMappings - save + reload returns the exact mapping
 *      set, per group, in order; allKits() lists built-ins first and the user
 *      kits after them.
 *   3. overwriteReplacesInsteadOfMerging - saving the same name twice leaves
 *      only the second version (a stale mapping from the first save would
 *      transpose a drum the user removed).
 *   4. deleteRemovesFromIndexAndData - a deleted kit disappears from
 *      userKitNames(), allKits() and kitByName().
 *   5. validateRejectsRangeViolations - source outside the GM drum range and
 *      target outside the playable C3..C6 band.
 *   6. validateRejectsDuplicateSources - the same GM drum mapped twice, both
 *      inside one group and across two groups.
 *   7. validateRejectsGroupTampering - renamed group, wrong program number,
 *      missing group.
 *   8. validateRejectsBadNames - empty, built-in, and slash-bearing names
 *      (a slash would become a QSettings group separator and lose the kit).
 *   9. corruptEntriesAreIgnoredOnLoad - garbage and out-of-range pairs
 *      written straight into QSettings do not surface as mappings.
 *
 * The store is Qt-only (QSettings + the plain DrumKitPreset data types), so
 * the test compiles just those two .cpp files. QSettings is redirected into
 * the temp scope the same way test_ffxiv_equalizer_service does it.
 */

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest/QtTest>

#include "../src/gui/FfxivDrumKitStore.h"

namespace {

/// A valid kit skeleton: correct group names/programs, one mapping.
FfxivDrumMapPreset makeKit(const QString &name) {
    FfxivDrumMapPreset kit;
    kit.name = name;
    kit.groups = {
        {"Bass Drum",  117, {{35, 60}}},
        {"Snare Drum", 118, {{38, 72}}},
        {"Cymbal",     119, {{49, 78}}},
        {"Bongo",      116, {{60, 70}}},
    };
    return kit;
}

int mappingCount(const FfxivDrumMapPreset &kit) {
    int n = 0;
    for (const FfxivDrumMapGroup &g : kit.groups) n += g.mappings.size();
    return n;
}

} // namespace

class TestFfxivDrumKitStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("MidiEditorTest");
        QCoreApplication::setApplicationName("DrumKitStoreTest");
        QSettings().clear();
        QSettings(QStringLiteral("MidiEditor"), QStringLiteral("NONE")).clear();
    }

    void cleanup() {
        // Every case starts from "no user kits" so they cannot see each
        // other's writes (the store is a singleton over real QSettings).
        FfxivDrumKitStore::instance()->clearUserKits();
    }

    // -----------------------------------------------------------------
    void builtinsAreReadOnly() {
        auto *store = FfxivDrumKitStore::instance();
        const QList<FfxivDrumMapPreset> builtins = FfxivDrumMapPreset::presets();
        QVERIFY(!builtins.isEmpty());

        for (const FfxivDrumMapPreset &kit : builtins) {
            QVERIFY2(FfxivDrumKitStore::isBuiltinName(kit.name),
                     qPrintable(kit.name));
            // Saving over a shipped kit must fail with a reason, not silently
            // shadow it with a user copy of the same name.
            QString error;
            QVERIFY(!store->saveKit(kit, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!store->deleteKit(kit.name, &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(store->userKitNames().isEmpty());
        // ... and they are all still there afterwards.
        QCOMPARE(store->allKits().size(), builtins.size());
    }

    // -----------------------------------------------------------------
    void roundTripPreservesMappings() {
        auto *store = FfxivDrumKitStore::instance();
        FfxivDrumMapPreset kit = makeKit(QStringLiteral("My Kit"));
        kit.groups[0].mappings = {{35, 60}, {36, 61}, {41, 65}};
        kit.groups[2].mappings = {{49, 72}, {52, 78}, {57, 84}};

        QString error;
        QVERIFY2(store->saveKit(kit, &error), qPrintable(error));
        QVERIFY(error.isEmpty());

        QCOMPARE(store->userKitNames(), QStringList{QStringLiteral("My Kit")});

        const FfxivDrumMapPreset loaded = store->kitByName(QStringLiteral("My Kit"));
        QCOMPARE(loaded.name, QStringLiteral("My Kit"));
        QCOMPARE(loaded.groups.size(), kit.groups.size());
        for (int gi = 0; gi < kit.groups.size(); ++gi) {
            QCOMPARE(loaded.groups[gi].trackName, kit.groups[gi].trackName);
            QCOMPARE(loaded.groups[gi].programNumber, kit.groups[gi].programNumber);
            QCOMPARE(loaded.groups[gi].mappings.size(), kit.groups[gi].mappings.size());
            for (int mi = 0; mi < kit.groups[gi].mappings.size(); ++mi) {
                QCOMPARE(loaded.groups[gi].mappings[mi].sourceNote,
                         kit.groups[gi].mappings[mi].sourceNote);
                QCOMPARE(loaded.groups[gi].mappings[mi].targetNote,
                         kit.groups[gi].mappings[mi].targetNote);
            }
        }

        // Built-ins keep their shipped order and stay in front, so the split
        // dialog's combo does not reshuffle when a user kit appears.
        const QList<FfxivDrumMapPreset> all = store->allKits();
        const QList<FfxivDrumMapPreset> builtins = FfxivDrumMapPreset::presets();
        QCOMPARE(all.size(), builtins.size() + 1);
        for (int i = 0; i < builtins.size(); ++i)
            QCOMPARE(all[i].name, builtins[i].name);
        QCOMPARE(all.last().name, QStringLiteral("My Kit"));
    }

    // -----------------------------------------------------------------
    void overwriteReplacesInsteadOfMerging() {
        auto *store = FfxivDrumKitStore::instance();
        FfxivDrumMapPreset first = makeKit(QStringLiteral("Iterate"));
        first.groups[0].mappings = {{35, 60}, {36, 61}, {41, 65}};
        QVERIFY(store->saveKit(first));

        // Second save drops two kick mappings. A merge would leave them
        // behind and keep transposing drums the user just removed.
        FfxivDrumMapPreset second = makeKit(QStringLiteral("Iterate"));
        second.groups[0].mappings = {{35, 62}};
        QVERIFY(store->saveKit(second));

        const FfxivDrumMapPreset loaded = store->kitByName(QStringLiteral("Iterate"));
        QCOMPARE(loaded.groups[0].mappings.size(), 1);
        QCOMPARE(loaded.groups[0].mappings[0].sourceNote, 35);
        QCOMPARE(loaded.groups[0].mappings[0].targetNote, 62);
        // Saving twice must not register the name twice either.
        QCOMPARE(store->userKitNames().size(), 1);
    }

    // -----------------------------------------------------------------
    void deleteRemovesFromIndexAndData() {
        auto *store = FfxivDrumKitStore::instance();
        QVERIFY(store->saveKit(makeKit(QStringLiteral("Temp Kit"))));
        QCOMPARE(store->userKitNames().size(), 1);

        QString error;
        QVERIFY(store->deleteKit(QStringLiteral("Temp Kit"), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(store->userKitNames().isEmpty());
        QVERIFY(store->kitByName(QStringLiteral("Temp Kit")).name.isEmpty());
        QCOMPARE(store->allKits().size(), FfxivDrumMapPreset::presets().size());

        // Deleting again reports "nothing removed" rather than an error.
        QVERIFY(!store->deleteKit(QStringLiteral("Temp Kit"), &error));
        QVERIFY(error.isEmpty());
    }

    // -----------------------------------------------------------------
    void validateRejectsRangeViolations() {
        FfxivDrumMapPreset kit = makeKit(QStringLiteral("Range"));
        // Source below the GM percussion key map.
        kit.groups[0].mappings = {{34, 60}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        kit.groups[0].mappings = {{82, 60}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        // Target outside the bard's playable C3..C6.
        kit.groups[0].mappings = {{35, 47}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        kit.groups[0].mappings = {{35, 85}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        // The exact edges are legal.
        kit.groups[0].mappings = {{35, 48}};
        QVERIFY(FfxivDrumKitStore::validate(kit).isEmpty());
        kit.groups[0].mappings = {{81, 84}};
        QVERIFY(FfxivDrumKitStore::validate(kit).isEmpty());
    }

    // -----------------------------------------------------------------
    void validateRejectsDuplicateSources() {
        FfxivDrumMapPreset kit = makeKit(QStringLiteral("Dupes"));
        // Twice inside one group.
        kit.groups[0].mappings = {{35, 60}, {35, 62}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        // Once per group, but the same drum - it cannot sound on two
        // instruments, so this is the more dangerous of the two.
        kit = makeKit(QStringLiteral("Dupes"));
        kit.groups[0].mappings = {{38, 60}};
        kit.groups[1].mappings = {{38, 72}};
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
    }

    // -----------------------------------------------------------------
    void validateRejectsGroupTampering() {
        // Renamed group: the name is how playback finds the FFXIV program.
        FfxivDrumMapPreset renamed = makeKit(QStringLiteral("Tampered"));
        renamed.groups[0].trackName = QStringLiteral("Kick");
        QVERIFY(!FfxivDrumKitStore::validate(renamed).isEmpty());

        // Wrong program: 96-99 are the reference build's SoundFont, ours are
        // 116-119 - a blind port would play silence.
        FfxivDrumMapPreset wrongProgram = makeKit(QStringLiteral("Tampered"));
        wrongProgram.groups[0].programNumber = 96;
        QVERIFY(!FfxivDrumKitStore::validate(wrongProgram).isEmpty());

        // Missing group.
        FfxivDrumMapPreset missing = makeKit(QStringLiteral("Tampered"));
        missing.groups.removeLast();
        QVERIFY(!FfxivDrumKitStore::validate(missing).isEmpty());

        // Reordered groups (same set) is also rejected: the editor builds
        // rows from the fixed skeleton, so a reorder means something else
        // assembled the kit.
        FfxivDrumMapPreset reordered = makeKit(QStringLiteral("Tampered"));
        std::swap(reordered.groups[0], reordered.groups[1]);
        QVERIFY(!FfxivDrumKitStore::validate(reordered).isEmpty());
    }

    // -----------------------------------------------------------------
    void validateRejectsBadNames() {
        FfxivDrumMapPreset kit = makeKit(QString());
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());

        kit.name = QStringLiteral("   ");
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());

        kit.name = FfxivDrumMapPreset::presets().first().name;
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());

        // A slash is the QSettings group separator: the kit would be written
        // into a nested group and never be found again.
        kit.name = QStringLiteral("My/Kit");
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());
        kit.name = QStringLiteral("My\\Kit");
        QVERIFY(!FfxivDrumKitStore::validate(kit).isEmpty());

        // An empty kit (valid groups, zero mappings) is pointless and would
        // present itself as a mapping kit that transposes nothing.
        FfxivDrumMapPreset empty = makeKit(QStringLiteral("Empty"));
        for (FfxivDrumMapGroup &g : empty.groups) g.mappings.clear();
        QVERIFY(!FfxivDrumKitStore::validate(empty).isEmpty());
    }

    // -----------------------------------------------------------------
    void corruptEntriesAreIgnoredOnLoad() {
        auto *store = FfxivDrumKitStore::instance();
        QVERIFY(store->saveKit(makeKit(QStringLiteral("Hand Edited"))));

        // Simulate a hand-edited INI: junk, half pairs, non-numeric values and
        // out-of-range numbers must all be dropped rather than reaching the
        // transpose path.
        QSettings settings(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        settings.setValue(
            QStringLiteral("FFXIV/drumKits/Hand Edited/Bass Drum"),
            QStringList({QStringLiteral("35:60"),   // keep
                         QStringLiteral("nonsense"),
                         QStringLiteral("36"),
                         QStringLiteral("36:"),
                         QStringLiteral("a:b"),
                         QStringLiteral("34:60"),   // source out of range
                         QStringLiteral("36:47"),   // target below C3
                         QStringLiteral("36:85"),   // target above C6
                         QStringLiteral("41:65")}));// keep
        settings.sync();

        const FfxivDrumMapPreset loaded =
            store->kitByName(QStringLiteral("Hand Edited"));
        QCOMPARE(loaded.groups[0].mappings.size(), 2);
        QCOMPARE(loaded.groups[0].mappings[0].sourceNote, 35);
        QCOMPARE(loaded.groups[0].mappings[1].sourceNote, 41);
        // The other groups are untouched by the tampering.
        QCOMPARE(mappingCount(loaded), 5);
    }
};

QTEST_MAIN(TestFfxivDrumKitStore)
#include "test_ffxiv_drum_kit_store.moc"
