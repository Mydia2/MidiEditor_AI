#include "FfxivPlayabilityValidator.h"

#include "FFXIVChannelFixer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"

#include <QMap>
#include <QPair>
#include <QSet>
#include <QStringList>

namespace {

QString noteName(int note) {
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F",
                                  "F#", "G", "G#", "A", "A#", "B"};
    return QStringLiteral("%1%2").arg(QLatin1String(names[note % 12]))
                                 .arg(note / 12 - 1);
}

} // namespace

int FfxivPlayabilityReport::countOf(FfxivPlayabilityIssue::Type t) const {
    int n = 0;
    for (const FfxivPlayabilityIssue &i : issues) {
        if (i.type == t) ++n;
    }
    return n;
}

QList<MidiEvent *> FfxivPlayabilityReport::offendingNotes() const {
    QSet<MidiEvent *> seen;
    QList<MidiEvent *> out;
    for (const FfxivPlayabilityIssue &i : issues) {
        for (MidiEvent *ev : i.events) {
            if (ev && !seen.contains(ev)) {
                seen.insert(ev);
                out.append(ev);
            }
        }
    }
    return out;
}

FfxivPlayabilityReport FfxivPlayabilityValidator::validate(
    MidiFile *file, const FfxivPlayabilityChecks &checks) {
    FfxivPlayabilityReport report;
    if (!file) {
        report.error = QStringLiteral("No file loaded.");
        return report;
    }
    report.ok = true;

    const int trackCount = file->numTracks();
    report.checkedTracks = trackCount;

    for (int t = 0; t < trackCount; ++t) {
        MidiTrack *track = file->track(t);
        if (!track) continue;
        const QString name = track->name();
        const QString baseName = FFXIVChannelFixer::stripSuffix(name);
        const bool isInstrument = FFXIVChannelFixer::programNumber(baseName) >= 0;

        const bool trackIsGuitar = FFXIVChannelFixer::isGuitar(baseName);

        // Collect this track's notes (with their channel - guitars spread
        // over several channels for variant switches).
        struct NoteInfo {
            int tick;
            int note;
            int channel;
            NoteOnEvent *ev;
        };
        QList<NoteInfo> notes;

        for (int ch = 0; ch < 16; ++ch) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (!ev || ev->track() != track) continue;
                if (auto *noteOn = dynamic_cast<NoteOnEvent *>(ev)) {
                    notes.append({noteOn->midiTime(), noteOn->note(), ch, noteOn});
                }
            }
        }
        report.checkedNotes += notes.size();

        // Track-name check, only for tracks that HAVE notes: a silent track
        // occupies no performer, so its name is irrelevant to playability -
        // without this rule every file's "Tempo Track" (the app's own
        // default) would be flagged and the report would cry wolf on every
        // check. (Deliberate sharpening over the old AI-tool rule, which
        // flagged any non-empty non-instrument name.)
        if (checks.trackNames && !notes.isEmpty() && !baseName.isEmpty()
            && !isInstrument) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::TrackName;
            issue.track = t;
            issue.details = QStringLiteral(
                "Track name '%1' doesn't match any FFXIV instrument").arg(name);
            report.issues.append(issue);
        }

        // Empty instrument track: named like a performer, but nothing to
        // play. Informational - it wastes one of the eight slots and is
        // usually a leftover from experimenting.
        if (checks.emptyTracks && notes.isEmpty() && isInstrument) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::EmptyTrack;
            issue.track = t;
            issue.details = QStringLiteral(
                "Track '%1' is named like an instrument but has no notes").arg(name);
            report.issues.append(issue);
        }

        // Channel spread - EDITOR playback only (in game the track name
        // selects the instrument): a non-guitar track whose notes sit on
        // several channels plays as several instruments in the editor's
        // SoundFont preview. The channel fixer is the repair. Deliberately
        // NOT a program-change comparison: shared channels and stacked
        // tick-0 PCs made that produce false findings on real octets.
        if (checks.channelSpread && isInstrument && !trackIsGuitar
            && !notes.isEmpty()) {
            QSet<int> usedChannels;
            for (const NoteInfo &n : notes) usedChannels.insert(n.channel);
            if (usedChannels.size() > 1) {
                FfxivPlayabilityIssue issue;
                issue.type = FfxivPlayabilityIssue::Type::ChannelSpread;
                issue.track = t;
                issue.tick = notes.first().tick;
                for (const NoteInfo &n : notes) issue.events.append(n.ev);
                issue.details = QStringLiteral(
                    "Track '%1' has notes on %2 channels - editor playback "
                    "will use different sounds for them (in game the track "
                    "name decides; run the channel fixer to tidy up)")
                        .arg(name).arg(usedChannels.size());
                report.issues.append(issue);
            }
        }

        // Range check: C3-C6 = MIDI 48-84.
        if (checks.range) {
            for (const NoteInfo &n : notes) {
                if (n.note < 48 || n.note > 84) {
                    FfxivPlayabilityIssue issue;
                    issue.type = FfxivPlayabilityIssue::Type::OutOfRange;
                    issue.track = t;
                    issue.tick = n.tick;
                    issue.details = QStringLiteral(
                        "Note %1 (%2) outside C3-C6 (MIDI 48-84) at tick %3")
                            .arg(noteName(n.note)).arg(n.note).arg(n.tick);
                    issue.events.append(n.ev);
                    report.issues.append(issue);
                }
            }
        }

        if (!checks.monophony) {
            continue;
        }

        // Monophony - user-verified in-game rule (2026-07-27): a held note
        // overlapped by later staccato notes is inaudible at speed and
        // nobody fixes it, so hold-overlaps are NOT flagged. What really
        // collides on a monophonic performer are notes STARTING on the same
        // tick. Group note starts by tick (guitars: per channel, because
        // different channels are variant switches and legal):
        //   - one pitch appearing twice in a group -> stacked duplicate
        //   - two or more distinct pitches in a group -> simultaneous notes
        //     (a chord the performer cannot play; one issue per group, not
        //     per pair, so a triad counts once)
        QMap<QPair<int, int>, QList<int>> groups; // (channel|0, tick) -> idx
        for (int i = 0; i < notes.size(); ++i) {
            const int chKey = trackIsGuitar ? notes[i].channel : 0;
            groups[qMakePair(chKey, notes[i].tick)].append(i);
        }
        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QList<int> &idx = it.value();
            if (idx.size() < 2) continue;
            const int tick = it.key().second;

            // Per-pitch duplicate detection inside the group.
            QMap<int, QList<int>> byPitch;
            for (int i : idx) byPitch[notes[i].note].append(i);
            for (auto p = byPitch.constBegin(); p != byPitch.constEnd(); ++p) {
                if (p.value().size() < 2) continue;
                FfxivPlayabilityIssue issue;
                issue.type = FfxivPlayabilityIssue::Type::DuplicateNote;
                issue.track = t;
                issue.tick = tick;
                for (int i : p.value()) issue.events.append(notes[i].ev);
                issue.details = p.value().size() == 2
                    ? QStringLiteral("Duplicate %1 stacked on tick %2")
                          .arg(noteName(p.key())).arg(tick)
                    : QStringLiteral("%1 x%2 stacked on tick %3")
                          .arg(noteName(p.key())).arg(p.value().size()).arg(tick);
                report.issues.append(issue);
            }

            // Distinct pitches sounding at once.
            if (byPitch.size() >= 2) {
                FfxivPlayabilityIssue issue;
                issue.type = FfxivPlayabilityIssue::Type::Overlap;
                issue.track = t;
                issue.tick = tick;
                QStringList pitchNames;
                for (auto p = byPitch.constBegin(); p != byPitch.constEnd(); ++p)
                    pitchNames.append(noteName(p.key()));
                for (int i : idx) issue.events.append(notes[i].ev);
                issue.details = QStringLiteral(
                    "%1 simultaneous notes at tick %2 (%3)")
                        .arg(idx.size()).arg(tick)
                        .arg(pitchNames.join(QStringLiteral(", ")));
                report.issues.append(issue);
            }
        }
    }

    return report;
}
