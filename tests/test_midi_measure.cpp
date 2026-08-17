/*
 * test_midi_measure
 *
 * Pins the bar-numbering contract of MidiFile::measure() and
 * MidiFile::startTickOfMeasure(). Both are 1-BASED - tick 0 lies in measure 1 -
 * and that is the number the status bar, the time display and the measure tool
 * put in front of the user.
 *
 * This suite exists because the header used to document the return value as
 * "0-based". Every caller that believed the comment added 1 and reported bars
 * one too high; the AI context (EditorContext) shipped that way and told the
 * model the song had one more bar than it has. The convention is cheap to
 * assert and impossible to notice by reading, so it gets nailed down here.
 *
 *   1. Tick 0 is measure 1 (NOT 0) and every tick maps to a positive number.
 *   2. The out-params bracket the queried tick and span exactly one measure.
 *   3. Measure boundaries advance the number by exactly one.
 *   4. startTickOfMeasure() is 1-based too: measure 1 starts at tick 0.
 *   5. measure()/startTickOfMeasure() round-trip for every measure.
 *   6. The totalMeasures contract: measure(endTick()) is the number of bars,
 *      with no +1 - the exact expression EditorContext reports to the AI.
 *   7. meterAt() reports the denominator as the SMF power-of-two EXPONENT (2
 *      means /4) - the same class of trap, and the reason the toolbar's bar
 *      display showed "4/2" and counted half-note beats.
 *   8. A mid-song meter change keeps the numbering continuous and switches to
 *      the new measure length (guards the accumulate loop).
 *   9. measureCount() is the bar count of the SONG, so it does not gain a
 *      phantom bar when the file ends exactly on a bar line (a fresh file is
 *      7680 ticks = exactly 10 bars, and measure(endTick()) says 11).
 *  10. deleteMeasures() leaves the file with a meter: exactly one
 *      TimeSignatureEvent survives at tick 0 with the meter of the first
 *      undeleted bar, and the readers still work afterwards. The re-anchor may
 *      not be decided by comparing num/denom against meterAt()'s no-event
 *      fallback, because that fallback IS a valid 4/4.
 *  11. Nothing in this family dereferences a null TimeSignatureEvent: with
 *      channel 18 emptied, measure() (both overloads), startTickOfMeasure(),
 *      measureCount(), insertMeasures() and deleteMeasures() fall back to 4/4
 *      instead of crashing. The measure tool asks measure() on every paint, so
 *      a null here is an access violation on the next mouse move.
 *
 * NOT testable here: measure() dereferences BOTH out-params unconditionally,
 * so passing nullptr is an access violation, not a soft failure. That is now
 * documented on the declaration - a death test would only re-crash the runner.
 *
 * Harness: compiles the REAL MidiFile/MidiChannel/MidiTrack/Protocol/MidiEvent
 * stack with the GUI periphery ODR-shimmed, same approach as
 * test_ffxiv_fixer_resync.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QColor>

#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiChannel.h"
#include "../src/midi/MidiTrack.h"
#include "../src/protocol/Protocol.h"
#include "../src/MidiEvent/MidiEvent.h"
#include "../src/MidiEvent/TempoChangeEvent.h"
#include "../src/MidiEvent/TimeSignatureEvent.h"

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

class TestMidiMeasure : public QObject {
    Q_OBJECT

private:
    // Expected ticks per measure derived INDEPENDENTLY of measure() itself,
    // so a wrong bar length cannot make the test agree with the bug.
    static int expectedTicksPerMeasure(MidiFile *f, int tick) {
        // NOTE: meterAt()'s denominator out-param is the SMF power-of-two
        // EXPONENT (2 means /4), not the printed denominator.
        int num = 4, denPow = 2;
        f->meterAt(tick, &num, &denPow);
        return f->ticksPerQuarter() * 4 * num / (1 << denPow);
    }

    // Inserts a meter change, same construction the file itself uses
    // (denominator as a power of two: 2 => 4, 3 => 8).
    static void insertMeter(MidiFile *f, int tick, int num, int denPow) {
        f->protocol()->startNewAction("meter");
        TimeSignatureEvent *ev =
            new TimeSignatureEvent(18, num, denPow, 24, 8, f->track(0));
        f->channel(18)->insertEvent(ev, tick);
        f->protocol()->endAction();
    }

    // Replaces the tick-0 4/4 the ctor plants. Insert first, then drop the old
    // one: MidiChannel::removeEvent() refuses to remove the LAST event at tick 0
    // on channels 17/18, so the order matters.
    static void setInitialMeter(MidiFile *f, int num, int denPow) {
        f->protocol()->startNewAction("initial meter");
        const QList<MidiEvent *> before = f->channel(18)->eventMap()->values(0);
        TimeSignatureEvent *ev =
            new TimeSignatureEvent(18, num, denPow, 24, 8, f->track(0));
        f->channel(18)->insertEvent(ev, 0);
        foreach (MidiEvent *old, before) {
            f->channel(18)->removeEvent(old);
        }
        f->protocol()->endAction();
    }

    // Empties channel 18 completely - the state every null-deref guard exists
    // for. Same insert-then-remove dance is not possible here (the guard keeps
    // the last tick-0 event), so this goes through the map directly, which is
    // exactly what a caller that bypasses MidiChannel would do.
    static void stripAllTimeSignatures(MidiFile *f) {
        f->channel(18)->eventMap()->clear();
    }

    static QList<TimeSignatureEvent *> timeSigs(MidiFile *f) {
        QList<TimeSignatureEvent *> out;
        foreach (MidiEvent *ev, f->channel(18)->eventMap()->values()) {
            TimeSignatureEvent *ts = dynamic_cast<TimeSignatureEvent *>(ev);
            if (ts) {
                out.append(ts);
            }
        }
        return out;
    }

private slots:

    // --- 1. tick 0 is measure 1 -------------------------------------------
    void tickZeroIsMeasureOne() {
        MidiFile f;
        int start = -1, end = -1;
        QCOMPARE(f.measure(0, &start, &end), 1);
        QCOMPARE(start, 0);
    }

    void everyMeasureNumberIsPositive() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        for (int tick = 0; tick < bar * 20; tick += bar / 4) {
            int s = 0, e = 0;
            QVERIFY2(f.measure(tick, &s, &e) >= 1,
                     qPrintable(QString("measure(%1) < 1").arg(tick)));
        }
    }

    // --- 2. out-params bracket the tick -----------------------------------
    void outParamsBracketTheTick() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        const int probes[] = {0, 1, bar - 1, bar, bar + 7, bar * 5, bar * 9 + 3};
        for (int tick : probes) {
            int s = -1, e = -1;
            f.measure(tick, &s, &e);
            QVERIFY2(s <= tick && tick < e,
                     qPrintable(QString("tick %1 not inside [%2,%3)")
                                    .arg(tick).arg(s).arg(e)));
            QCOMPARE(e - s, bar);
        }
    }

    // --- 3. boundaries advance by exactly one -----------------------------
    void barBoundariesAdvanceByOne() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        int s = 0, e = 0;
        QCOMPARE(f.measure(bar - 1, &s, &e), 1);
        QCOMPARE(f.measure(bar, &s, &e), 2);
        QCOMPARE(f.measure(bar * 2, &s, &e), 3);
        for (int k = 0; k < 16; ++k) {
            QCOMPARE(f.measure(k * bar, &s, &e), k + 1);
        }
    }

    // --- 4. startTickOfMeasure() is 1-based too ---------------------------
    void startTickOfMeasureIsOneBased() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(2), bar);
        QCOMPARE(f.startTickOfMeasure(9), bar * 8);
    }

    // --- 5. round-trip ----------------------------------------------------
    void measureAndStartTickRoundTrip() {
        MidiFile f;
        for (int m = 1; m <= 16; ++m) {
            int s = 0, e = 0;
            const int tick = f.startTickOfMeasure(m);
            QCOMPARE(f.measure(tick, &s, &e), m);
            QCOMPARE(s, tick);
        }
    }

    // --- 6. the totalMeasures contract EditorContext reports --------------
    void totalMeasuresNeedsNoPlusOne() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        // A song ending mid-way through bar 8 has EIGHT bars.
        f.protocol()->startNewAction("end");
        f.setEndTick(bar * 7 + bar / 2);
        f.protocol()->endAction();
        int s = 0, e = 0;
        QCOMPARE(f.measure(f.endTick(), &s, &e), 8);

        // Ending exactly ON a bar line puts the end tick in the NEXT bar -
        // still no +1 anywhere.
        f.protocol()->startNewAction("end2");
        f.setEndTick(bar * 8);
        f.protocol()->endAction();
        QCOMPARE(f.measure(f.endTick(), &s, &e), 9);
    }

    // --- 7. meterAt() reports the denominator as an EXPONENT --------------
    // The other half of the family: a caller that treats it as the printed
    // denominator gets half-note beats and displays "4/2" for a 4/4 song.
    void meterAtReportsDenominatorAsExponent() {
        MidiFile f;
        int num = 0, denPow = 0;
        f.meterAt(0, &num, &denPow);
        QCOMPARE(num, 4);
        QCOMPARE(denPow, 2);          // 2 means /4, NOT "/2"
        QCOMPARE(1 << denPow, 4);

        // A 6/8 change reports 3, not 8.
        const int tick = f.startTickOfMeasure(3);
        insertMeter(&f, tick, 6, 3);
        f.meterAt(tick, &num, &denPow);
        QCOMPARE(num, 6);
        QCOMPARE(denPow, 3);
        QCOMPARE(1 << denPow, 8);
        // ... and the measure length follows the actual denominator.
        int s = 0, e = 0;
        f.measure(tick, &s, &e);
        QCOMPARE(e - s, f.ticksPerQuarter() * 4 * 6 / 8);
    }

    // --- 8. meter change keeps the numbering continuous -------------------
    void meterChangeKeepsNumbering() {
        MidiFile f;
        const int fourFour = expectedTicksPerMeasure(&f, 0);
        // Switch to 3/4 at the start of bar 5.
        const int switchTick = f.startTickOfMeasure(5);
        insertMeter(&f, switchTick, 3, 2);

        const int threeFour = expectedTicksPerMeasure(&f, switchTick);
        QCOMPARE(threeFour, fourFour * 3 / 4);

        int s = 0, e = 0;
        // Bar 4 is still the last 4/4 bar.
        QCOMPARE(f.measure(switchTick - 1, &s, &e), 4);
        QCOMPARE(e - s, fourFour);
        // Numbering continues at 5 and now advances every 3/4 bar.
        QCOMPARE(f.measure(switchTick, &s, &e), 5);
        QCOMPARE(e - s, threeFour);
        QCOMPARE(f.measure(switchTick + threeFour, &s, &e), 6);
        QCOMPARE(f.measure(switchTick + threeFour * 2, &s, &e), 7);
    }

    // --- 9. measureCount(): the song's bar count, no phantom bar ------------
    // measure(endTick()) is one too high for every file that ends exactly on a
    // bar line, because endTick() is the EXCLUSIVE end and therefore already
    // lies in the next bar. A fresh file is exactly 10 bars.
    void measureCountHasNoPhantomBar() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(f.endTick(), bar * 10);
        QCOMPARE(f.measureCount(), 10);

        // The trap this method exists to avoid - still true of measure() itself.
        int s = 0, e = 0;
        QCOMPARE(f.measure(f.endTick(), &s, &e), 11);

        // One tick INTO bar 11 really is 11 bars.
        f.protocol()->startNewAction("end");
        f.setEndTick(bar * 10 + 1);
        f.protocol()->endAction();
        QCOMPARE(f.measureCount(), 11);

        // Ends in the middle of a bar are unaffected.
        f.protocol()->startNewAction("end2");
        f.setEndTick(bar * 7 + bar / 2);
        f.protocol()->endAction();
        QCOMPARE(f.measureCount(), 8);
    }

    void measureCountOfZeroLengthFileIsOne() {
        MidiFile f;
        f.protocol()->startNewAction("empty");
        f.setEndTick(0);
        f.protocol()->endAction();
        QCOMPARE(f.endTick(), 0);
        QCOMPARE(f.measureCount(), 1);
    }

    // --- 10. deleteMeasures() must leave the file with a meter --------------
    void deletingFirstMeasureKeepsTimeSignature() {
        MidiFile f;
        const int bar = expectedTicksPerMeasure(&f, 0);

        // The safety net that makes this survivable at all: channel 18 refuses
        // to give up its last event at tick 0. Assert it, because the
        // re-anchoring rule below must not silently depend on it.
        QCOMPARE(timeSigs(&f).size(), 1);
        QCOMPARE(f.channel(18)->removeEvent(timeSigs(&f).first()), false);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 1);
        f.protocol()->endAction();

        const QList<TimeSignatureEvent *> after = timeSigs(&f);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.first()->midiTime(), 0);
        QCOMPARE(after.first()->num(), 4);
        QCOMPARE(after.first()->denom(), 2);

        // ... and the readers, which dereference that event, still work.
        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(s, 0);
        QCOMPARE(e, bar);
        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(3), bar * 2);
        QCOMPARE(f.measureCount(), 9);   // 10 bars minus the deleted one
    }

    void deletingFirstMeasureKeepsThreeFourMeter() {
        MidiFile f;
        setInitialMeter(&f, 3, 2);
        const int bar = expectedTicksPerMeasure(&f, 0);
        QCOMPARE(bar, f.ticksPerQuarter() * 3);
        QCOMPARE(timeSigs(&f).size(), 1);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 1);
        f.protocol()->endAction();

        int num = 0, denPow = 0;
        TimeSignatureEvent *anchor = nullptr;
        f.meterAt(0, &num, &denPow, &anchor);
        QVERIFY2(anchor, "no TimeSignatureEvent left to anchor the meter");
        QCOMPARE(anchor->midiTime(), 0);
        QCOMPARE(num, 3);
        QCOMPARE(denPow, 2);
        QCOMPARE(timeSigs(&f).size(), 1);

        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(e - s, bar);
    }

    // Deleting a range that ENDS on a meter change moves that meter to the
    // start. The old tick-0 event survives (channel 18 keeps its last tick-0
    // event), so the new meter must be written INTO it - a second event on tick
    // 0 would shadow it, since the map iterates the newest first and meterAt()
    // keeps the last one it sees.
    void deletingUpToAMeterChangeRetunesTheAnchor() {
        MidiFile f;
        const int fourFour = expectedTicksPerMeasure(&f, 0);
        const int switchTick = f.startTickOfMeasure(3);
        insertMeter(&f, switchTick, 3, 2);
        QCOMPARE(timeSigs(&f).size(), 2);

        f.protocol()->startNewAction("Remove measures");
        f.deleteMeasures(1, 2);
        f.protocol()->endAction();

        QCOMPARE(timeSigs(&f).size(), 1);
        int num = 0, denPow = 0;
        f.meterAt(0, &num, &denPow);
        QCOMPARE(num, 3);
        QCOMPARE(denPow, 2);
        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(e - s, fourFour * 3 / 4);
    }

    // --- 11. no null TimeSignatureEvent dereference anywhere ----------------
    void emptyTimeSignatureChannelDoesNotCrash() {
        MidiFile f;
        const int bar = f.ticksPerQuarter() * 4;   // the 4/4 fallback
        // Kept as a non-null sentinel for the out-param check below; the strip
        // only drops it out of the map, the object stays alive.
        TimeSignatureEvent *sentinel = timeSigs(&f).value(0);
        QVERIFY(sentinel);
        stripAllTimeSignatures(&f);
        QVERIFY(f.channel(18)->eventMap()->isEmpty());

        // meterAt() reports 4/4 AND a null event - the null is the only way a
        // caller can tell "no meter at all" from "a real 4/4".
        int num = 0, denPow = 0;
        TimeSignatureEvent *anchor = sentinel;
        f.meterAt(0, &num, &denPow, &anchor);
        QCOMPARE(num, 4);
        QCOMPARE(denPow, 2);
        QVERIFY2(anchor == nullptr, "meterAt() left the event out-param untouched");

        int s = -1, e = -1;
        QCOMPARE(f.measure(0, &s, &e), 1);
        QCOMPARE(s, 0);
        QCOMPARE(e, bar);
        QCOMPARE(f.measure(bar * 3 + 5, &s, &e), 4);
        QCOMPARE(s, bar * 3);
        QCOMPARE(e, bar * 4);

        QCOMPARE(f.startTickOfMeasure(1), 0);
        QCOMPARE(f.startTickOfMeasure(5), bar * 4);
        QCOMPARE(f.measureCount(), 10);

        // The list-returning overload too.
        QList<TimeSignatureEvent *> *list = nullptr;
        int tickInMeasure = -1;
        QCOMPARE(f.measure(bar * 2, bar * 4, &list, &tickInMeasure), 3);
        QCOMPARE(tickInMeasure, 0);
        QVERIFY(list && list->isEmpty());
        delete list;
    }

    void insertAndDeleteMeasuresSurviveAnEmptyMeterChannel() {
        MidiFile f;
        const int bar = f.ticksPerQuarter() * 4;
        stripAllTimeSignatures(&f);

        f.protocol()->startNewAction("Insert measures");
        f.insertMeasures(1, 2);            // used to read an uninitialised ptr
        f.protocol()->endAction();
        QCOMPARE(f.endTick(), bar * 12);

        MidiFile g;
        stripAllTimeSignatures(&g);
        g.protocol()->startNewAction("Remove measures");
        g.deleteMeasures(1, 1);
        g.protocol()->endAction();
        QCOMPARE(g.endTick(), bar * 9);
        // The delete re-anchors a meter, so the file is no longer meterless.
        QCOMPARE(timeSigs(&g).size(), 1);
        QCOMPARE(timeSigs(&g).first()->midiTime(), 0);
        QCOMPARE(timeSigs(&g).first()->num(), 4);
        QCOMPARE(timeSigs(&g).first()->denom(), 2);
    }
};

QTEST_MAIN(TestMidiMeasure)
#include "test_midi_measure.moc"
