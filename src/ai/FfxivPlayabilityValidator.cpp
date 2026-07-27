#include "FfxivPlayabilityValidator.h"

#include "FFXIVChannelFixer.h"
#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/ProgChangeEvent.h"

#include <QSet>
#include <algorithm>

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

FfxivPlayabilityReport FfxivPlayabilityValidator::validate(MidiFile *file) {
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
        // over several channels for variant switches) and its program
        // changes.
        struct NoteInfo {
            int tick;
            int endTick;
            int note;
            int channel;
            NoteOnEvent *ev;
        };
        QList<NoteInfo> notes;
        QList<ProgChangeEvent *> progChanges;

        for (int ch = 0; ch < 16; ++ch) {
            MidiChannel *channel = file->channel(ch);
            if (!channel) continue;
            QMultiMap<int, MidiEvent *> *map = channel->eventMap();
            for (auto it = map->begin(); it != map->end(); ++it) {
                MidiEvent *ev = it.value();
                if (!ev || ev->track() != track) continue;
                if (auto *noteOn = dynamic_cast<NoteOnEvent *>(ev)) {
                    const int tick = noteOn->midiTime();
                    int endTick = tick;
                    if (MidiEvent *offEv = noteOn->offEvent())
                        endTick = offEv->midiTime();
                    notes.append({tick, endTick, noteOn->note(), ch, noteOn});
                } else if (auto *pc = dynamic_cast<ProgChangeEvent *>(ev)) {
                    progChanges.append(pc);
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
        if (!notes.isEmpty() && !baseName.isEmpty() && !isInstrument) {
            FfxivPlayabilityIssue issue;
            issue.type = FfxivPlayabilityIssue::Type::TrackName;
            issue.track = t;
            issue.details = QStringLiteral(
                "Track name '%1' doesn't match any FFXIV instrument").arg(name);
            report.issues.append(issue);
        }

        // Range check: C3-C6 = MIDI 48-84.
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

        // Monophony: sort by tick, then flag every same-channel collision.
        // Same pitch + same start tick is its own class (stacked duplicate -
        // the classic hand-arranging slip); everything else is an overlap.
        // Unlike the original tool code this reports ALL of them, because
        // the GUI selects the offending notes for fixing.
        std::sort(notes.begin(), notes.end(),
                  [](const NoteInfo &a, const NoteInfo &b) {
                      return a.tick < b.tick;
                  });
        for (int i = 0; i < notes.size(); ++i) {
            for (int j = i + 1; j < notes.size(); ++j) {
                if (notes[j].tick >= notes[i].endTick)
                    break; // sorted: nothing later can overlap notes[i]
                if (trackIsGuitar && notes[i].channel != notes[j].channel)
                    continue; // different guitar-switch channels - intended
                if (notes[i].endTick > notes[j].tick
                    && notes[j].endTick > notes[i].tick) {
                    FfxivPlayabilityIssue issue;
                    issue.track = t;
                    issue.tick = notes[i].tick;
                    issue.events.append(notes[i].ev);
                    issue.events.append(notes[j].ev);
                    if (notes[i].tick == notes[j].tick
                        && notes[i].note == notes[j].note) {
                        issue.type = FfxivPlayabilityIssue::Type::DuplicateNote;
                        issue.details = QStringLiteral(
                            "Duplicate %1 stacked on tick %2")
                                .arg(noteName(notes[i].note))
                                .arg(notes[i].tick);
                    } else {
                        issue.type = FfxivPlayabilityIssue::Type::Overlap;
                        issue.details = QStringLiteral(
                            "Overlapping notes at tick %1 (%2) and %3 (%4)")
                                .arg(notes[i].tick).arg(noteName(notes[i].note))
                                .arg(notes[j].tick).arg(noteName(notes[j].note));
                    }
                    report.issues.append(issue);
                }
            }
        }

        // Name / program mismatch (octet finding #5): a non-guitar track
        // whose name maps to program P but whose program changes say
        // something else plays the WRONG instrument in game while the name
        // check passes. Guitars are exempt (their channels intentionally
        // carry the variant programs); "no program change at all" is the
        // channel fixer's business, not an error here.
        if (isInstrument && !trackIsGuitar && !notes.isEmpty()) {
            const int expected = FFXIVChannelFixer::programNumber(baseName);
            for (ProgChangeEvent *pc : progChanges) {
                if (pc->program() != expected) {
                    FfxivPlayabilityIssue issue;
                    issue.type = FfxivPlayabilityIssue::Type::ProgramMismatch;
                    issue.track = t;
                    issue.tick = pc->midiTime();
                    issue.details = QStringLiteral(
                        "Track is named '%1' (program %2) but its program "
                        "change at tick %3 says %4 - it will sound as the "
                        "wrong instrument. Re-run the channel fixer or fix "
                        "the program change.")
                            .arg(baseName).arg(expected)
                            .arg(pc->midiTime()).arg(pc->program());
                    report.issues.append(issue);
                }
            }
        }
    }

    return report;
}
