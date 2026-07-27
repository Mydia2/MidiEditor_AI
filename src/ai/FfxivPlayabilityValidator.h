/*
 * MidiEditor AI
 *
 * FfxivPlayabilityValidator — Phase 46
 *
 * Headless FFXIV playability check, shared by the `validate_ffxiv` AI tool
 * and the GUI's "Check FFXIV Playability" dialog. Grown out of the octet
 * experiment: the monophony/overlap check used to exist ONLY inside the AI
 * tool, so a human arranging by hand found those spots by exporting, playing
 * the song in game and hearing notes go missing.
 *
 * What it checks, per track (tracks WITHOUT notes are exempt from the name
 * and program checks - a silent track occupies no performer, and the app's
 * own default "Tempo Track" must not flag every file):
 *   - track name against the canonical FFXIV instrument spellings
 *     (FFXIVChannelFixer::instrumentNames())
 *   - note range C3-C6 (MIDI 48-84)
 *   - stacked duplicates: two notes of the SAME pitch starting on the SAME
 *     tick on one performer - the most common hand-arranging slip (they
 *     sound as one note in game, or not at all)
 *   - overlaps: a performer is monophonic; overlapping notes on one channel
 *     drop notes in game. Guitar tracks may spread notes over SEVERAL
 *     channels (variant switches) - only same-channel overlaps are flagged
 *     there.
 *   - track-name / program_change mismatch: a renamed track whose channel
 *     still carries the OLD instrument's program change plays the wrong
 *     sound while validating fine by name (octet finding #5). Guitar tracks
 *     are exempt (switch channels intentionally carry different variants);
 *     a track with NO program change is not flagged (that is the channel
 *     fixer's job, not a validity failure).
 *
 * Unlike the original tool implementation this reports ALL findings (the
 * tool used to stop at the first overlap per track) and carries the
 * offending NoteOn events, so the GUI can select them in the editor.
 */

#ifndef FFXIVPLAYABILITYVALIDATOR_H_
#define FFXIVPLAYABILITYVALIDATOR_H_

#include <QList>
#include <QString>

class MidiFile;
class MidiEvent;

struct FfxivPlayabilityIssue {
    enum class Type {
        TrackName,        ///< name matches no FFXIV instrument
        OutOfRange,       ///< note outside C3-C6
        DuplicateNote,    ///< same pitch, same start tick, same performer
        Overlap,          ///< overlapping notes on one (guitar: same) channel
        ProgramMismatch   ///< track name says X, program change says Y
    };

    Type type;
    int track = -1;
    int tick = 0;              ///< first tick involved (0 for TrackName)
    QString details;           ///< human-readable, one line
    QList<MidiEvent *> events; ///< offending NoteOn events (may be empty)
};

struct FfxivPlayabilityReport {
    bool ok = false;      ///< false only when no file was given
    QString error;
    QList<FfxivPlayabilityIssue> issues;
    int checkedTracks = 0;
    int checkedNotes = 0;

    bool valid() const { return ok && issues.isEmpty(); }
    int countOf(FfxivPlayabilityIssue::Type t) const;

    /** All offending note events across all issues, deduplicated - what the
     *  GUI selects in the editor. */
    QList<MidiEvent *> offendingNotes() const;
};

class FfxivPlayabilityValidator {
public:
    static FfxivPlayabilityReport validate(MidiFile *file);
};

#endif // FFXIVPLAYABILITYVALIDATOR_H_
