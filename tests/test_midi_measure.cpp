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
};

QTEST_MAIN(TestMidiMeasure)
#include "test_midi_measure.moc"
