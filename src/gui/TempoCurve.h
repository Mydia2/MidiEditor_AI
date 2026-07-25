/*
 * MidiEditor AI
 *
 * TempoCurve - the pure math behind TempoDialog's Smooth Transition
 * (v2.1.0 feature #3, roadmap Phase 35).
 *
 * Extracted from TempoDialog::accept() so it can be unit-tested without a
 * widget, a MidiFile or a protocol: the loop had shipped a regression where
 * an intermediate BPM step could claim the range's end tick and the
 * keep-first dedupe then dropped the FINAL step - the one carrying the
 * target BPM - so everything after the range played at a near-target tempo
 * (Ease-in 60->300 over 20 ticks ended at 289 BPM). That class of bug is
 * invisible in a GUI test but trivial to pin here.
 *
 * Model: one event per integer BPM step (BPM is integral, so denser spacing
 * only emits duplicates). Step k carries BPM start + delta*k/steps and lands
 * at startTick + span * g(k/steps), where g is the inverse of the tempo
 * curve f in bpm(t) = start + delta * f(t):
 *
 *   Linear    f(t) = t              g(x) = x
 *   Ease-in   f(t) = t^2            g(x) = sqrt(x)
 *   Ease-out  f(t) = 1-(1-t)^2      g(x) = 1 - sqrt(1-x)
 *   S-curve   f = smoothstep        g(x) = 0.5 - sin(asin(1-2x)/3)
 */

#ifndef TEMPO_CURVE_H_
#define TEMPO_CURVE_H_

#include <QList>
#include <cmath>

/// Curve shapes, in the order the dialog's combo box lists them.
enum class TempoCurveShape {
    Linear = 0,
    EaseIn = 1,
    EaseOut = 2,
    SCurve = 3,
};

/// One tempo event to emit: BPM value at a MIDI tick.
struct TempoCurveStep {
    int tick = 0;
    int bpm = 0;
};

namespace TempoCurve {

/// Maps BPM progress x (0..1) to time progress (0..1) for \a shape.
inline double timeAtBpmFraction(TempoCurveShape shape, double x) {
    switch (shape) {
    case TempoCurveShape::EaseIn:
        return std::sqrt(x);
    case TempoCurveShape::EaseOut:
        return 1.0 - std::sqrt(1.0 - x);
    case TempoCurveShape::SCurve:
        return 0.5 - std::sin(std::asin(1.0 - 2.0 * x) / 3.0);
    case TempoCurveShape::Linear:
    default:
        return x;
    }
}

/**
 * \brief Builds the tempo events of one smooth transition.
 *
 * Guarantees, in the order they matter:
 *  - the LAST step always sits exactly on \a endTick and carries \a endBpm
 *    (the end tick belongs to the final step alone: intermediate steps are
 *    clamped below it, otherwise rounding lets an earlier step claim the
 *    tick and the dedupe drops the target BPM);
 *  - ticks are strictly increasing (one event per tick, keep-first);
 *  - a degenerate range (span <= 0) or a zero BPM delta yields the single
 *    target event at \a startTick.
 */
inline QList<TempoCurveStep> build(int startBpm, int endBpm, int startTick,
                                   int endTick, TempoCurveShape shape) {
    QList<TempoCurveStep> out;
    const int span = endTick - startTick;
    const int steps = (endBpm > startBpm) ? (endBpm - startBpm)
                                          : (startBpm - endBpm);
    if (span <= 0 || steps == 0) {
        out.append({startTick, endBpm});
        return out;
    }

    int lastTick = -1;
    for (int k = 0; k <= steps; ++k) {
        const int bpm = startBpm + ((endBpm - startBpm) * k) / steps;
        const double x = static_cast<double>(k) / steps;
        int tick = startTick
            + static_cast<int>(static_cast<double>(span)
                               * timeAtBpmFraction(shape, x) + 0.5);
        if (k < steps && tick >= endTick) {
            tick = endTick - 1; // reserve endTick for the final step
        }
        if (tick == lastTick) {
            continue; // range shorter than the BPM step count
        }
        lastTick = tick;
        out.append({tick, bpm});
    }
    return out;
}

} // namespace TempoCurve

#endif // TEMPO_CURVE_H_
