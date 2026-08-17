/*
 * MidiEditor AI
 *
 * TempoConversionService — Phase 33
 *
 * Headless engine that performs a "Time-Preserving Tempo Conversion":
 * the user picks a target BPM and the existing event ticks are scaled
 * so the real-time duration of the piece is preserved while the new
 * project tempo takes effect.
 *
 * The service is intentionally UI-free so it can be unit tested and
 * reused by future automation entry points (CLI, MCP tool, etc.).
 */

#ifndef TEMPO_CONVERSION_SERVICE_H_
#define TEMPO_CONVERSION_SERVICE_H_

#include <QSet>
#include <QString>

class MidiFile;

/**
 * \brief Scope selector — which subset of events the conversion applies to.
 */
enum class TempoConversionScope {
    WholeProject,
    SelectedTracks,
    SelectedChannels,
    SelectedEvents
};

/**
 * \brief How to update the tempo map itself.
 */
enum class TempoConversionTempoMode {
    /// Remove every tempo event and insert a single new one (target BPM) at tick 0.
    ReplaceFixed,
    /// Keep the shape of the tempo map: scale tick positions and multiply each
    /// stored BPM by (target / source) so musical curves survive.
    ScaleTempoMap,
    /// Touch event ticks only — leave the tempo map alone (user takes the wheel).
    EventsOnly
};

/**
 * \brief Options for a single conversion run.
 *
 * \note The tempo map is shared by the whole file. A partial scope
 *       (SelectedTracks / SelectedChannels / SelectedEvents) may therefore
 *       only be combined with TempoConversionTempoMode::EventsOnly — every
 *       other mode rewrites the shared map and would retime the material
 *       outside the scope as well. preview() and convert() reject that
 *       combination (see TempoConversionService::scopeModeConflict()).
 */
struct TempoConversionOptions {
    double sourceBpm = 120.0;
    double targetBpm = 120.0;

    TempoConversionScope scope = TempoConversionScope::WholeProject;
    TempoConversionTempoMode tempoMode = TempoConversionTempoMode::ReplaceFixed;

    /// Track numbers to include when scope == SelectedTracks.
    QSet<int> trackIds;
    /// MIDI channels (0..15) to include when scope == SelectedChannels.
    QSet<int> channelIds;
    /// Pre-collected event pointers when scope == SelectedEvents.
    /// (Stored as void* so this header does not pull in MidiEvent.)
    QSet<quintptr> selectedEventPtrs;

    /// Whether to scale tempo events (channel 17) according to tempoMode.
    bool includeTempo = true;
    /// Whether to scale time-signature events (channel 18). They are ticks too,
    /// so for time preservation they must be scaled when scope == WholeProject.
    bool includeTimeSig = true;
    /// Whether to scale meta events on channel 16 (lyrics, text, key sig).
    bool includeMeta = true;

    /// Optional Protocol-panel label for the single undo action convert()
    /// opens. Empty (the dialog's case) keeps the service's own
    /// "Convert tempo (preserve duration): X → Y BPM". The AI/MCP tool sets it
    /// so the action carries the same actor attribution
    /// ("MidiPilot" / "MidiPilotMCP (<client>)") every other agent action has -
    /// see ToolDefinitions::protocolActorPrefix.
    QString actionLabel;
};

/**
 * \brief Result / preview of a conversion.
 */
struct TempoConversionResult {
    bool ok = false;
    QString warning;        ///< Non-fatal note (e.g. "scale factor near 1.0")
    QString error;          ///< Fatal error if !ok
    int affectedEvents = 0; ///< Events whose tick was changed
    int tempoEventsRemoved = 0;
    int tempoEventsInserted = 0;
    double scaleFactor = 1.0;     ///< target / source
    qint64 oldDurationMs = 0;     ///< Project endTick in ms BEFORE conversion
    qint64 newDurationMs = 0;     ///< Predicted endTick in ms AFTER conversion
};

/**
 * \brief Stateless service.
 */
class TempoConversionService {
public:
    /**
     * \brief Validate the scope / tempo-mode pair.
     *
     * The tempo map is shared by the whole file, so replacing or rescaling it
     * from a partial scope would retime every event OUTSIDE that scope too.
     * \return An empty string when the combination is legal, otherwise the
     *         user-facing reason it is refused. Callers that build their own
     *         UI (the Convert Tempo dialog, the AI tool) use this to keep the
     *         impossible combination out of reach in the first place;
     *         preview() / convert() enforce it regardless.
     */
    static QString scopeModeConflict(const TempoConversionOptions &options);

    /**
     * \brief Compute what `convert()` would do without mutating the file.
     */
    static TempoConversionResult preview(MidiFile *file,
                                         const TempoConversionOptions &options);

    /**
     * \brief Apply the conversion in-place, wrapped in a single Protocol action.
     */
    static TempoConversionResult convert(MidiFile *file,
                                         const TempoConversionOptions &options);
};

#endif // TEMPO_CONVERSION_SERVICE_H_
