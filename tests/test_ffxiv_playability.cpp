/*
 * test_ffxiv_playability
 *
 * Hard-gate tests for FfxivPlayabilityValidator (Phase 46) - the headless
 * FFXIV playability check shared by the `validate_ffxiv` AI tool and the
 * GUI's "Check FFXIV Playability" dialog. The octet experiment showed the
 * monophony check existed only behind the AI tool; now that it has a GUI
 * surface and reports ALL findings with event pointers, its rules are
 * contracts:
 *
 *   1. A clean octet-style file validates with zero issues.
 *   2. Overlapping notes on one channel are flagged, with BOTH NoteOn
 *      events attached (the GUI selects them).
 *   3. Same pitch + same start tick is its own class (stacked duplicate).
 *   4. Guitar tracks: overlaps across DIFFERENT channels are variant
 *      switches and legal; on the SAME channel they are flagged.
 *   5. Notes outside C3-C6 are flagged per note.
 *   6. A non-instrument name on a track WITH notes is flagged; the app's
 *      default "Tempo Track" (no notes) is NOT - a silent track occupies
 *      no performer.
 *   7. Program mismatch: a "Trumpet" track whose program change says 40
 *      is flagged; a matching program change is not; a track with NO
 *      program change is not (that is the channel fixer's job).
 *   8. offendingNotes() deduplicates notes shared by several issues.
 *   9. ALL overlaps are reported, not just the first per track (the old
 *      tool behaviour the GUI cannot live with).
 *
 * Harness: same ODR-shim approach as test_ffxiv_fixer_resync - real
 * MidiFile/MidiChannel/MidiTrack/Protocol/MidiEvent stack, GUI periphery
 * shimmed.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>

#include "../src/ai/FfxivPlayabilityValidator.h"
#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"

// ---- ODR shims: Appearance colors (statics used by midi core / events) ---
#include "../src/gui/Appearance.h"
QColor Appearance::borderColor() { return QColor(); }
QColor *Appearance::channelColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}
QColor *Appearance::trackColor(int) {
    static QColor c(128, 128, 128);
    return &c;
}

// ---- ODR shims: EventWidget ----------------------------------------------
#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}
QList<MidiEvent *> EventWidget::events() { return {}; }

// ==========================================================================

using Type = FfxivPlayabilityIssue::Type;

class TestFfxivPlayability : public QObject {
    Q_OBJECT

private:
    // New empty file; renames track 1 and pins it to a channel.
    static MidiFile *makeFile(const QString &track1Name, int track1Channel) {
        MidiFile *f = new MidiFile();
        f->track(1)->setName(track1Name);
        f->track(1)->assignChannel(track1Channel);
        return f;
    }

    static NoteOnEvent *addNote(MidiFile *f, int ch, MidiTrack *track,
                                int note, int startTick, int endTick) {
        f->protocol()->startNewAction("setup-note");
        NoteOnEvent *on =
            f->channel(ch)->insertNote(note, startTick, endTick, 100, track);
        f->protocol()->endAction();
        return on;
    }

    static void addProgChange(MidiFile *f, int ch, MidiTrack *track,
                              int program, int tick) {
        f->protocol()->startNewAction("setup-pc");
        ProgChangeEvent *pc = new ProgChangeEvent(ch, program, track);
        f->channel(ch)->insertEvent(pc, tick);
        f->protocol()->endAction();
    }

private slots:

    // --- 1. clean file ----------------------------------------------------
    void cleanFileIsValid() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 62, 100, 200);
        addNote(f, 0, f->track(1), 64, 200, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY(r.ok);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        QCOMPARE(r.checkedNotes, 3);
        delete f;
    }

    void nullFileReportsError() {
        const FfxivPlayabilityReport r =
            FfxivPlayabilityValidator::validate(nullptr);
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    // --- 2. overlap with both events attached -----------------------------
    void overlapFlaggedWithBothEvents() {
        MidiFile *f = makeFile("Trumpet", 0);
        NoteOnEvent *a = addNote(f, 0, f->track(1), 60, 0, 200);
        NoteOnEvent *b = addNote(f, 0, f->track(1), 64, 100, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        const FfxivPlayabilityIssue &issue = r.issues.first();
        QCOMPARE(issue.events.size(), 2);
        QVERIFY(issue.events.contains(a));
        QVERIFY(issue.events.contains(b));
        delete f;
    }

    // --- 3. stacked duplicate is its own class -----------------------------
    void stackedDuplicateIsOwnClass() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 60, 0, 100);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        delete f;
    }

    // --- 4. guitar switch channels ----------------------------------------
    void guitarCrossChannelOverlapIsLegal() {
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 0);
        // Same track, overlapping in time, DIFFERENT channels = a variant
        // switch, not polyphony.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 1, f->track(1), 64, 100, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        QCOMPARE(r.countOf(Type::DuplicateNote), 0);

        // ... but on the SAME channel it is real.
        addNote(f, 0, f->track(1), 67, 50, 150);
        const FfxivPlayabilityReport r2 = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r2.countOf(Type::Overlap), 1);
        delete f;
    }

    // --- 5. range ----------------------------------------------------------
    void outOfRangeFlaggedPerNote() {
        MidiFile *f = makeFile("Trumpet", 0);
        NoteOnEvent *low = addNote(f, 0, f->track(1), 40, 0, 100);   // E1
        addNote(f, 0, f->track(1), 60, 200, 300);                     // fine
        NoteOnEvent *high = addNote(f, 0, f->track(1), 96, 400, 500); // C7

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::OutOfRange), 2);
        const QList<MidiEvent *> offending = r.offendingNotes();
        QVERIFY(offending.contains(low));
        QVERIFY(offending.contains(high));
        delete f;
    }

    // --- 6. track names ----------------------------------------------------
    void badNameFlaggedOnlyWithNotes() {
        MidiFile *f = makeFile("Lead Guitar", 0);
        // No notes anywhere yet: neither "Tempo Track" (track 0), nor
        // "Lead Guitar" - silent tracks occupy no performer.
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 0);

        addNote(f, 0, f->track(1), 60, 0, 100);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 1);
        QVERIFY(r.issues.first().details.contains("Lead Guitar"));
        delete f;
    }

    void suffixedInstrumentNameIsLegal() {
        MidiFile *f = makeFile("Trumpet+12", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::TrackName), 0);
        delete f;
    }

    // --- 7. program mismatch ----------------------------------------------
    void programMismatchRules() {
        // Wrong program: "Trumpet" is 56, the PC says 40 (Violin) -> flagged.
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        addProgChange(f, 0, f->track(1), 40, 0);
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ProgramMismatch), 1);
        delete f;

        // Matching program -> clean.
        f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        addProgChange(f, 0, f->track(1), 56, 0);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ProgramMismatch), 0);
        delete f;

        // NO program change at all -> not an error (the fixer's job).
        f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ProgramMismatch), 0);
        delete f;

        // Guitar tracks are exempt: switch channels intentionally carry
        // OTHER variants' programs.
        f = makeFile("ElectricGuitarOverdriven", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        addProgChange(f, 0, f->track(1), 27, 0); // Clean variant's program
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ProgramMismatch), 0);
        delete f;
    }

    // --- 8. offendingNotes dedup -------------------------------------------
    void offendingNotesAreDeduplicated() {
        MidiFile *f = makeFile("Trumpet", 0);
        // One long note overlapped by two others: it appears in two issues
        // but must be selected once.
        NoteOnEvent *loong = addNote(f, 0, f->track(1), 60, 0, 400);
        addNote(f, 0, f->track(1), 64, 100, 200);
        addNote(f, 0, f->track(1), 67, 250, 350);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 2);
        const QList<MidiEvent *> offending = r.offendingNotes();
        QCOMPARE(offending.count(loong), 1);
        QCOMPARE(offending.size(), 3);
        delete f;
    }

    // --- 9. ALL overlaps reported, not just the first ----------------------
    void allOverlapsReportedNotJustFirst() {
        MidiFile *f = makeFile("Trumpet", 0);
        // Three separate overlapping pairs, far apart.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 0, f->track(1), 62, 100, 300);
        addNote(f, 0, f->track(1), 64, 1000, 1200);
        addNote(f, 0, f->track(1), 65, 1100, 1300);
        addNote(f, 0, f->track(1), 67, 2000, 2200);
        addNote(f, 0, f->track(1), 69, 2100, 2300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 3);
        delete f;
    }
};

QTEST_MAIN(TestFfxivPlayability)
#include "test_ffxiv_playability.moc"
