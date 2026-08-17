/*
 * MidiEditor AI
 *
 * AutoFitVoiceLoadService — v2.1.0 feature #1 (roadmap Phase 35, remodeled
 * after real-song testing).
 *
 * The ACTION counterpart to the FFXIV Voice Limiter's analysis, in ONE
 * undoable protocol step. Two problem classes, two models:
 *
 *  - Voice-ceiling overflows use RAW concurrency (notes physically sounding
 *    at the same time, NO sample-tail extension). The display's tail model
 *    (MogNotate-style "active voices") intentionally overestimates: in-game
 *    voice eviction almost always hits ringing tails, which is inaudible -
 *    thinning against it destroyed songs that play perfectly fine. Removal
 *    order at a raw overflow: duplicates, then an optional per-channel chord
 *    limit (outer voices survive, loudest middles kept), then quietest ->
 *    shortest. Percussion (channel 9) is never thinned for voice count.
 *  - Note-rate desaturation works PER TRACK (one FFXIV performer = one
 *    track, so channel-level lumping would mix e.g. kick/snare/cymbal).
 *    Passages denser than the configurable threshold are thinned by keeping
 *    1 of N notes (N = 2 "halve" or 3 "third" - the manual editing practice
 *    this automates), always keeping the loudest note of each group so
 *    accents survive. Percussion tracks ARE included here (dense cymbals).
 *
 * Headless and UI-free (TempoConversionService pattern). Never runs
 * implicitly: callers show the dry-run result and require explicit
 * confirmation before the live run.
 */

#ifndef AUTO_FIT_VOICE_LOAD_SERVICE_H_
#define AUTO_FIT_VOICE_LOAD_SERVICE_H_

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

class MidiFile;
class MidiEvent;

struct AutoFitOptions {
    int startTick = -1;        ///< -1 = file start
    int endTick = -1;          ///< -1 = file end
    /// Track numbers whose notes may be REMOVED. Empty = all tracks. Notes on
    /// other tracks still count toward voices/density, they just never become
    /// victims - so the tool can thin e.g. only the Cymbal track.
    QSet<int> trackFilter;
    /// Optional selection scope: NoteOn event pointers (as quintptr). When
    /// non-empty, ONLY these notes are in scope - density is measured over
    /// them and only they can be removed; everything else still counts for
    /// the voice sweep. Pointers are compared, never dereferenced, so a
    /// stale entry simply matches nothing.
    QSet<quintptr> selectionScope;
    int targetCeiling = 16;    ///< RAW concurrent-voice ceiling, clamped [2, 32]
    int chordLimit = 3;        ///< max simultaneous voices per channel; 0 = off
    bool desaturateRates = true;
    /// Density desaturation target: thin about this PERCENTAGE of a track's
    /// notes, taking its DENSEST passages first (clamped [0, 85], 0 = off).
    /// Percent-of-track instead of an absolute notes/sec threshold because
    /// musical density is relative to tempo and instrument - a slow ride
    /// cymbal wall and a 200-BPM shred run are both "too dense" at wildly
    /// different absolute rates. The service auto-tunes an internal density
    /// cutoff per track until the target is reached, so the slider always
    /// has an effect.
    int ratePercent = 10;
    /// Optional per-track override of ratePercent (track number -> percent).
    QMap<int, int> ratePercentPerTrack;
    int rateKeepOneOf = 2;     ///< keep 1 of N notes in dense passages (2 = halve, 3 = third), clamped [2, 6]
    /// When thinning dense runs, prefer keeping louder notes (accents) before
    /// the higher voice. OFF by default: the channel fixer normalizes all
    /// velocities to 100, so loudness carries no information in the typical
    /// FFXIV workflow and the higher voice (usually the melody) should win.
    bool preferLoudest = false;
    bool dryRun = false;
    /// Optional Protocol-panel label for the single undo action a live run
    /// opens. Empty (the dialog's case) keeps the service's own
    /// "Auto-fit voice load". The AI/MCP tool sets it so the action carries
    /// the same actor attribution ("MidiPilot" / "MidiPilotMCP (<client>)")
    /// every other agent action has - see ToolDefinitions::protocolActorPrefix.
    QString actionLabel;
};

struct AutoFitRemovedNote {
    int tick = 0;
    int track = 0;
    int channel = 0;
    int pitch = 0;
    int velocity = 0;
    QString reason;            ///< "duplicate" | "chord-limit" | "voice-ceiling" | "note-rate"
};

/// Per-track dry-run breakdown for the confirmation dialog. One entry per
/// track that has notes inside the scope (removed may be 0).
struct AutoFitTrackSummary {
    int track = 0;
    QString name;
    int removed = 0;
    int notes = 0;             ///< track's note count inside the scope
};

struct AutoFitResult {
    bool ok = false;
    QString error;

    int peakBefore = 0;        ///< RAW voice peak before thinning (in scope)
    int remainingPeak = 0;     ///< RAW voice peak after thinning (predicted for dryRun)
    int overflowRangeCount = 0;
    int totalNotesInScope = 0; ///< for "removes X% of the song" reporting

    int removedCount = 0;
    int duplicateRemoved = 0;
    int chordRemoved = 0;
    int ceilingRemoved = 0;
    int rateRemoved = 0;

    QList<AutoFitRemovedNote> removed;
    QList<AutoFitTrackSummary> trackSummaries; ///< only tracks with removals
    /// The NoteOnEvents that were (or would be) removed — used by the
    /// confirmation dialog's "Preview as selection". Only valid until the
    /// file is mutated; empty after a live run (the events are gone).
    QList<MidiEvent *> victims;
};

class AutoFitVoiceLoadService {
public:
    /// Compute (dryRun) or apply (one Protocol action) the thinning.
    static AutoFitResult apply(MidiFile *file, const AutoFitOptions &options);
};

#endif // AUTO_FIT_VOICE_LOAD_SERVICE_H_
