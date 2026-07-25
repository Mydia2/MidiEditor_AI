/*
 * test_tempo_curve
 *
 * Unit tests for src/gui/TempoCurve.h - the step/tick math behind the Edit
 * Tempo dialog's Smooth Transition (v2.1.0 feature #3).
 *
 * WHY this target exists: the curve feature shipped with a regression that a
 * GUI test would never have caught. Rounding let an intermediate BPM step
 * claim the range's end tick, and the keep-first dedupe then dropped the
 * FINAL step - the one carrying the target BPM - so a transition ended at
 * 289 instead of 300 BPM and, absent later tempo events, the whole rest of
 * the song played at that wrong tempo. The math is now a pure header and
 * these tests pin it.
 *
 * Coverage:
 *   1. reachesTargetAtEndTick_allCurves - the exact reported regression
 *      cases (Ease-in 60->300 over 20 ticks, Linear 100->350 over 100
 *      ticks): the LAST step must sit exactly on endTick carrying endBpm.
 *   2. reachesTargetAtEndTick_sweep - the same endpoint contract over a
 *      matrix of BPM pairs x spans x all four curves, including spans far
 *      shorter than the BPM step count (steps > span) which is what
 *      triggers the collision, and ritardando (falling BPM).
 *   3. ticksStrictlyIncreasing - one event per tick, ascending, and never
 *      outside [startTick, endTick]: the insert order the tempo channel and
 *      every later tick<->ms conversion rely on.
 *   4. firstStepCarriesStartBpm - the ramp starts at startTick with the
 *      start BPM (no silent jump at the range's beginning).
 *   5. curveShapesDiffer_andBracketLinear - Ease-in stays BELOW the linear
 *      BPM at mid-range and Ease-out ABOVE it (the audible difference the
 *      combo box promises), while the S-curve stays between them.
 *   6. degenerateRanges_singleEvent - span <= 0 or a zero BPM delta yields
 *      exactly one event at startTick carrying the target BPM (the dialog's
 *      instant-change fallback).
 *   7. eventCountStaysBounded - one event per integer BPM step at most, so
 *      the documented "no thousands of duplicate events" property holds
 *      even for huge ranges.
 *   8. monotonicBpm - BPM never moves backwards along an accelerando (nor
 *      forwards along a ritardando), for every curve.
 *
 * Pure header, no Qt widgets and no MidiFile: the target links nothing but
 * QtTest.
 */

#include <QtTest/QtTest>
#include <QList>

#include "../src/gui/TempoCurve.h"

namespace {

const TempoCurveShape kAllShapes[] = {
    TempoCurveShape::Linear,
    TempoCurveShape::EaseIn,
    TempoCurveShape::EaseOut,
    TempoCurveShape::SCurve,
};

const char *shapeName(TempoCurveShape s) {
    switch (s) {
    case TempoCurveShape::Linear: return "Linear";
    case TempoCurveShape::EaseIn: return "Ease-in";
    case TempoCurveShape::EaseOut: return "Ease-out";
    case TempoCurveShape::SCurve: return "S-curve";
    }
    return "?";
}

/// BPM of the step in effect at \a tick (the last step at or before it).
int bpmAt(const QList<TempoCurveStep> &steps, int tick) {
    int bpm = steps.isEmpty() ? 0 : steps.first().bpm;
    for (const TempoCurveStep &s : steps) {
        if (s.tick > tick) break;
        bpm = s.bpm;
    }
    return bpm;
}

} // namespace

class TestTempoCurve : public QObject {
    Q_OBJECT

private slots:

    // ---------------------------------------------------------------------
    void reachesTargetAtEndTick_allCurves() {
        // The two cases verified in the regression report. Ease-in 60->300
        // over 20 ticks has 240 BPM steps for 20 ticks, so several steps
        // round onto the same tick - exactly the collision that used to
        // swallow the final step (it ended at 289 BPM).
        struct Case { int from, to, startTick, endTick; };
        const Case cases[] = {
            {60, 300, 1000, 1020},
            {100, 350, 1000, 1100},
        };
        for (const Case &c : cases) {
            for (TempoCurveShape shape : kAllShapes) {
                const QList<TempoCurveStep> steps =
                    TempoCurve::build(c.from, c.to, c.startTick, c.endTick, shape);
                QVERIFY2(!steps.isEmpty(), shapeName(shape));
                const QString ctx = QStringLiteral("%1 %2->%3 ticks %4-%5")
                                        .arg(shapeName(shape)).arg(c.from).arg(c.to)
                                        .arg(c.startTick).arg(c.endTick);
                QVERIFY2(steps.last().tick == c.endTick, qPrintable(ctx));
                QVERIFY2(steps.last().bpm == c.to, qPrintable(ctx));
            }
        }
    }

    // ---------------------------------------------------------------------
    void reachesTargetAtEndTick_sweep() {
        // Spans of 1..5000 ticks against BPM deltas of 1..240: the small
        // spans put steps > span (the collision regime), the large ones the
        // ordinary case. Ritardando included - the dedupe is direction-blind
        // but the endpoint contract must hold either way.
        const int bpms[] = {60, 100, 200, 300};
        const int spans[] = {1, 2, 5, 20, 100, 5000};
        for (int from : bpms) {
            for (int to : bpms) {
                if (from == to) continue;
                for (int span : spans) {
                    for (TempoCurveShape shape : kAllShapes) {
                        const QList<TempoCurveStep> steps =
                            TempoCurve::build(from, to, 480, 480 + span, shape);
                        const QString ctx =
                            QStringLiteral("%1 %2->%3 span %4")
                                .arg(shapeName(shape)).arg(from).arg(to).arg(span);
                        QVERIFY2(!steps.isEmpty(), qPrintable(ctx));
                        QVERIFY2(steps.last().tick == 480 + span, qPrintable(ctx));
                        QVERIFY2(steps.last().bpm == to, qPrintable(ctx));
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    void ticksStrictlyIncreasing() {
        for (TempoCurveShape shape : kAllShapes) {
            const QList<TempoCurveStep> steps =
                TempoCurve::build(60, 300, 1000, 1020, shape);
            for (int i = 1; i < steps.size(); ++i) {
                QVERIFY2(steps[i].tick > steps[i - 1].tick, shapeName(shape));
            }
            for (const TempoCurveStep &s : steps) {
                QVERIFY2(s.tick >= 1000 && s.tick <= 1020, shapeName(shape));
            }
        }
    }

    // ---------------------------------------------------------------------
    void firstStepCarriesStartBpm() {
        for (TempoCurveShape shape : kAllShapes) {
            const QList<TempoCurveStep> steps =
                TempoCurve::build(90, 180, 2000, 4000, shape);
            QVERIFY(!steps.isEmpty());
            QCOMPARE(steps.first().tick, 2000);
            QCOMPARE(steps.first().bpm, 90);
        }
    }

    // ---------------------------------------------------------------------
    void curveShapesDiffer_andBracketLinear() {
        // 60 -> 180 BPM over 1200 ticks; sample the middle of the range.
        // Ease-in changes late (still slow at half time), Ease-out changes
        // early (already fast), and the S-curve sits between them. If the
        // g() mappings were swapped or collapsed this test fails.
        const int mid = 600;
        const int linear = bpmAt(
            TempoCurve::build(60, 180, 0, 1200, TempoCurveShape::Linear), mid);
        const int easeIn = bpmAt(
            TempoCurve::build(60, 180, 0, 1200, TempoCurveShape::EaseIn), mid);
        const int easeOut = bpmAt(
            TempoCurve::build(60, 180, 0, 1200, TempoCurveShape::EaseOut), mid);
        const int sCurve = bpmAt(
            TempoCurve::build(60, 180, 0, 1200, TempoCurveShape::SCurve), mid);

        QCOMPARE(linear, 120);              // exactly half way
        QVERIFY(easeIn < linear - 10);      // gentle start
        QVERIFY(easeOut > linear + 10);     // quick start, settles later
        QVERIFY(sCurve > easeIn);
        QVERIFY(sCurve < easeOut);
    }

    // ---------------------------------------------------------------------
    void degenerateRanges_singleEvent() {
        for (TempoCurveShape shape : kAllShapes) {
            // Zero-length range and inverted range: one instant event.
            for (int endTick : {1000, 900}) {
                const QList<TempoCurveStep> steps =
                    TempoCurve::build(60, 300, 1000, endTick, shape);
                QCOMPARE(steps.size(), 1);
                QCOMPARE(steps.first().tick, 1000);
                QCOMPARE(steps.first().bpm, 300);
            }
            // No BPM delta: also one event, at the start tick.
            const QList<TempoCurveStep> flat =
                TempoCurve::build(120, 120, 1000, 5000, shape);
            QCOMPARE(flat.size(), 1);
            QCOMPARE(flat.first().tick, 1000);
            QCOMPARE(flat.first().bpm, 120);
        }
    }

    // ---------------------------------------------------------------------
    void eventCountStaysBounded() {
        // The whole point of the per-BPM-step model: a huge range must not
        // emit more events than there are integer BPM values to pass
        // through (the old per-5-ticks model emitted ~3000).
        for (TempoCurveShape shape : kAllShapes) {
            const QList<TempoCurveStep> steps =
                TempoCurve::build(100, 125, 0, 100000, shape);
            QVERIFY2(steps.size() <= 26, shapeName(shape)); // 25 steps + start
        }
    }

    // ---------------------------------------------------------------------
    void monotonicBpm() {
        for (TempoCurveShape shape : kAllShapes) {
            const QList<TempoCurveStep> up =
                TempoCurve::build(60, 300, 0, 2000, shape);
            for (int i = 1; i < up.size(); ++i) {
                QVERIFY2(up[i].bpm >= up[i - 1].bpm, shapeName(shape));
            }
            const QList<TempoCurveStep> down =
                TempoCurve::build(300, 60, 0, 2000, shape);
            for (int i = 1; i < down.size(); ++i) {
                QVERIFY2(down[i].bpm <= down[i - 1].bpm, shapeName(shape));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestTempoCurve)
#include "test_tempo_curve.moc"
