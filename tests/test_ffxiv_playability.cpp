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
 *   2. HOLD-overlaps are NOT flagged (user-verified in-game rule: a held
 *      note overlapped by later staccato notes is inaudible at speed) -
 *      only notes STARTING on the same tick collide.
 *   3. Distinct pitches starting on one tick are a simultaneous-notes
 *      issue - ONE issue per tick group (a triad counts once), with all
 *      NoteOn events attached (the GUI selects them).
 *   4. Same pitch + same start tick is its own class (stacked duplicate).
 *   5. Guitar tracks: same-tick starts on DIFFERENT channels are variant
 *      switches and legal; on the SAME channel they are flagged.
 *   6. Notes outside C3-C6 are flagged per note.
 *   7. A non-instrument name on a track WITH notes is flagged; the app's
 *      default "Tempo Track" (no notes) is NOT - a silent track occupies
 *      no performer. Program changes are NEVER validated: in game the
 *      track name selects the instrument, programs only drive the
 *      editor's SF2 playback.
 *   8. offendingNotes() deduplicates notes shared by several issues.
 *   9. ALL groups are reported, not just the first per track (the old
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

    // --- 2. hold-overlaps are legal ----------------------------------------
    void holdOverlapIsNotFlagged() {
        MidiFile *f = makeFile("Trumpet", 0);
        // A held note with two staccato notes on top of it - the pattern
        // every real arrangement carries. Different start ticks: legal.
        addNote(f, 0, f->track(1), 60, 0, 400);
        addNote(f, 0, f->track(1), 64, 100, 150);
        addNote(f, 0, f->track(1), 64, 200, 250);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        delete f;
    }

    // --- 3. simultaneous starts: one issue per tick group -------------------
    void simultaneousStartsFlaggedOncePerGroup() {
        MidiFile *f = makeFile("Trumpet", 0);
        // A triad starting on one tick = ONE issue carrying all three notes.
        NoteOnEvent *a = addNote(f, 0, f->track(1), 60, 0, 200);
        NoteOnEvent *b = addNote(f, 0, f->track(1), 64, 0, 300);
        NoteOnEvent *c = addNote(f, 0, f->track(1), 67, 0, 250);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        const FfxivPlayabilityIssue &issue = r.issues.first();
        QCOMPARE(issue.events.size(), 3);
        QVERIFY(issue.events.contains(a));
        QVERIFY(issue.events.contains(b));
        QVERIFY(issue.events.contains(c));
        delete f;
    }

    // --- 4. stacked duplicate is its own class -----------------------------
    void stackedDuplicateIsOwnClass() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 60, 0, 100);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        delete f;
    }

    // --- 5. guitar switch channels ----------------------------------------
    void guitarCrossChannelSameTickIsLegal() {
        MidiFile *f = makeFile("ElectricGuitarOverdriven", 0);
        // Same track, same START tick, DIFFERENT channels = a variant
        // switch, not polyphony.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 1, f->track(1), 64, 0, 300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 0);
        QCOMPARE(r.countOf(Type::DuplicateNote), 0);

        // ... but on the SAME channel it is real.
        addNote(f, 0, f->track(1), 67, 0, 150);
        const FfxivPlayabilityReport r2 = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r2.countOf(Type::Overlap), 1);
        delete f;
    }

    // --- 6. range ----------------------------------------------------------
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

    // --- 7. track names; programs are NEVER validated -----------------------
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

    // In-game the TRACK NAME selects the instrument; program changes only
    // drive the editor's SF2 playback. A "wrong" program must NOT be a
    // playability finding - this pins the removal of the ProgramMismatch
    // check (its premise was disproven the day it shipped).
    void wrongProgramChangeIsNotAPlayabilityIssue() {
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 100, 200);
        addProgChange(f, 0, f->track(1), 40, 0); // says Violin - irrelevant
        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QVERIFY2(r.valid(), qPrintable(r.issues.isEmpty()
                                           ? QString()
                                           : r.issues.first().details));
        delete f;
    }

    // --- 8. offendingNotes dedup + duplicates inside a chord group ----------
    void offendingNotesAreDeduplicated() {
        MidiFile *f = makeFile("Trumpet", 0);
        // C + C + E all starting on one tick: the two Cs are a duplicate
        // issue, the mixed pitches a simultaneous-notes issue - the shared
        // C notes appear in both but must be selected once each.
        NoteOnEvent *c1 = addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 0, f->track(1), 60, 0, 120);
        addNote(f, 0, f->track(1), 64, 0, 100);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::DuplicateNote), 1);
        QCOMPARE(r.countOf(Type::Overlap), 1);
        const QList<MidiEvent *> offending = r.offendingNotes();
        QCOMPARE(offending.count(c1), 1);
        QCOMPARE(offending.size(), 3);
        delete f;
    }

    // --- 10. check selection: categories run only when asked ----------------
    void checkSelectionIsHonoured() {
        MidiFile *f = makeFile("Lead Guitar", 0); // bad name, WITH notes
        addNote(f, 0, f->track(1), 40, 0, 100);   // also out of range
        addNote(f, 0, f->track(1), 44, 0, 100);   // and a same-tick collision

        FfxivPlayabilityChecks none;
        none.monophony = false;
        none.range = false;
        none.trackNames = false;
        none.channelSpread = false;
        none.emptyTracks = false;
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f, none);
        QVERIFY(r.valid()); // everything off -> nothing found

        FfxivPlayabilityChecks onlyNames = none;
        onlyNames.trackNames = true;
        r = FfxivPlayabilityValidator::validate(f, onlyNames);
        QCOMPARE(r.issues.size(), 1);
        QCOMPARE(r.countOf(Type::TrackName), 1);
        delete f;
    }

    // --- 11. channel spread (editor playback) -------------------------------
    void channelSpreadFlaggedForNonGuitarOnly() {
        // Non-guitar instrument with notes on two channels -> flagged.
        MidiFile *f = makeFile("Trumpet", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 1, f->track(1), 62, 200, 300);
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ChannelSpread), 1);
        delete f;

        // Guitars spread channels BY DESIGN (variant switches) -> never.
        f = makeFile("ElectricGuitarOverdriven", 0);
        addNote(f, 0, f->track(1), 60, 0, 100);
        addNote(f, 1, f->track(1), 62, 200, 300);
        r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::ChannelSpread), 0);
        delete f;
    }

    // --- 12. empty instrument tracks ---------------------------------------
    void emptyInstrumentTrackIsInformational() {
        MidiFile *f = makeFile("Trumpet", 0); // named, no notes anywhere
        FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::EmptyTrack), 1);
        // ... but a non-instrument name without notes stays silent
        // ("Tempo Track" must not flag every file).
        QCOMPARE(r.countOf(Type::TrackName), 0);
        delete f;
    }

    // --- 9. ALL groups reported, not just the first -------------------------
    void allGroupsReportedNotJustFirst() {
        MidiFile *f = makeFile("Trumpet", 0);
        // Three separate same-tick pairs, far apart.
        addNote(f, 0, f->track(1), 60, 0, 200);
        addNote(f, 0, f->track(1), 62, 0, 300);
        addNote(f, 0, f->track(1), 64, 1000, 1200);
        addNote(f, 0, f->track(1), 65, 1000, 1300);
        addNote(f, 0, f->track(1), 67, 2000, 2200);
        addNote(f, 0, f->track(1), 69, 2000, 2300);

        const FfxivPlayabilityReport r = FfxivPlayabilityValidator::validate(f);
        QCOMPARE(r.countOf(Type::Overlap), 3);
        delete f;
    }
};

QTEST_MAIN(TestFfxivPlayability)
#include "test_ffxiv_playability.moc"
