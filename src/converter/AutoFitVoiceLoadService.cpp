#include "AutoFitVoiceLoadService.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiChannel.h"
#include "../midi/MidiTrack.h"
#include "../MidiEvent/MidiEvent.h"
#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../protocol/Protocol.h"

#include <QMap>
#include <QMultiMap>
#include <QVector>

#include <algorithm>

namespace {

// One candidate note. All voice math uses the RAW musical span (see header:
// thinning against the display's sample-tail model destroyed songs that play
// fine in-game, because evicted ringing tails are inaudible).
struct FitNote {
    NoteOnEvent *on = nullptr;
    int track = 0;
    int channel = 0;
    int startTick = 0;
    int endTick = 0;
    int startMs = 0;
    int pitch = 0;
    int velocity = 0;
    bool drum = false;
    bool inScope = false;
    bool thinnable = false; ///< inScope AND matches the track filter
    bool removed = false;
};

// Deterministic candidate order for the ceiling pass: quietest first, then
// shortest, then lowest channel, then latest start, then pitch.
bool ceilingPriorityLess(const FitNote *a, const FitNote *b)
{
    if (a->velocity != b->velocity) return a->velocity < b->velocity;
    const int da = a->endTick - a->startTick;
    const int db = b->endTick - b->startTick;
    if (da != db) return da < db;
    if (a->channel != b->channel) return a->channel < b->channel;
    if (a->startTick != b->startTick) return a->startTick > b->startTick;
    return a->pitch < b->pitch;
}

} // namespace

AutoFitResult AutoFitVoiceLoadService::apply(MidiFile *file,
                                             const AutoFitOptions &optionsIn)
{
    AutoFitResult result;
    if (!file) {
        result.error = QStringLiteral("No MIDI file.");
        return result;
    }

    AutoFitOptions opts = optionsIn;
    opts.targetCeiling = qBound(2, opts.targetCeiling, 32);
    if (opts.chordLimit != 0) opts.chordLimit = qBound(2, opts.chordLimit, 16);
    opts.ratePercent = qBound(0, opts.ratePercent, 85);
    opts.rateKeepOneOf = qBound(2, opts.rateKeepOneOf, 6);
    const int scopeStart = (opts.startTick >= 0) ? opts.startTick : 0;
    const int scopeEnd = (opts.endTick >= 0) ? opts.endTick : file->endTick();
    // Both ticks are INCLUSIVE (tool schema contract), so an equal pair is a
    // valid one-instant scope covering the notes that start at that tick.
    if (scopeEnd < scopeStart) {
        result.error = QStringLiteral("Empty tick range.");
        return result;
    }

    // ---- Collect notes ----------------------------------------------------
    QVector<FitNote> notes;
    notes.reserve(8192);
    for (int ch = 0; ch < 16; ++ch) {
        MidiChannel *channel = file->channel(ch);
        if (!channel || !channel->eventMap()) continue;
        const bool isDrum = (ch == 9);
        QMultiMap<int, MidiEvent *> *events = channel->eventMap();
        for (auto it = events->begin(); it != events->end(); ++it) {
            auto *on = dynamic_cast<NoteOnEvent *>(it.value());
            if (!on) continue;

            FitNote n;
            n.on = on;
            n.track = on->track() ? on->track()->number() : 0;
            n.channel = ch;
            n.drum = isDrum;
            n.pitch = on->note();
            n.velocity = on->velocity();
            n.startTick = on->midiTime();
            n.endTick = n.startTick + 1;
            if (on->offEvent() && on->offEvent()->midiTime() > n.startTick)
                n.endTick = on->offEvent()->midiTime();
            n.startMs = file->timeMS(n.startTick);
            n.inScope = (n.startTick >= scopeStart && n.startTick <= scopeEnd)
                && (opts.selectionScope.isEmpty()
                    || opts.selectionScope.contains(
                           reinterpret_cast<quintptr>(
                               static_cast<MidiEvent *>(on))));
            n.thinnable = n.inScope
                && (opts.trackFilter.isEmpty()
                    || opts.trackFilter.contains(n.track));
            if (n.inScope) ++result.totalNotesInScope;
            notes.push_back(n);
        }
    }

    // ---- Raw peak / overflow metrics (helper usable before + after) -------
    auto rawPeakInScope = [&](bool survivorsOnly, int *rangeCount) {
        struct Edge { int tick; int delta; };
        QVector<Edge> edges;
        edges.reserve(notes.size() * 2);
        for (const FitNote &n : notes) {
            if (survivorsOnly && n.removed) continue;
            edges.push_back({n.startTick, +1});
            edges.push_back({n.endTick, -1});
        }
        std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.delta < b.delta;
        });
        int cur = 0, peak = 0;
        bool inOver = false;
        bool primed = false;
        if (rangeCount) *rangeCount = 0;
        for (const Edge &e : edges) {
            // Sample the concurrency carried INTO the scope by notes still
            // holding: a range lying entirely inside sustained notes has no
            // edge of its own and would otherwise report peak 0 while those
            // voices sound throughout it. OFF edges sitting EXACTLY on
            // scopeStart must be processed first, not sampled over: by the
            // sweep's own OFF-before-ON tie-break those notes no longer
            // sound at that tick, and sampling before their decrement would
            // invent a phantom overflow out of notes that end where the
            // scope begins.
            if (!primed
                && (e.tick > scopeStart
                    || (e.tick == scopeStart && e.delta > 0))) {
                primed = true;
                peak = std::max(peak, cur);
                if (cur > opts.targetCeiling) {
                    if (rangeCount) ++(*rangeCount);
                    inOver = true;
                }
            }
            cur += e.delta;
            if (e.tick < scopeStart || e.tick > scopeEnd) continue;
            peak = std::max(peak, cur);
            const bool over = cur > opts.targetCeiling;
            if (rangeCount) {
                if (over && !inOver) { ++(*rangeCount); inOver = true; }
                if (!over) inOver = false;
            }
        }
        return peak;
    };
    result.peakBefore = rawPeakInScope(false, &result.overflowRangeCount);

    auto removeNote = [&](FitNote &n, const char *reason, int &counter) {
        n.removed = true;
        AutoFitRemovedNote rn;
        rn.tick = n.startTick;
        rn.track = n.track;
        rn.channel = n.channel;
        rn.pitch = n.pitch;
        rn.velocity = n.velocity;
        rn.reason = QString::fromLatin1(reason);
        result.removed.append(rn);
        result.victims.append(n.on);
        ++counter;
        ++result.removedCount;
    };

    // ---- Pass 1: RAW voice-ceiling sweep ----------------------------------
    {
        struct Edge { int tick; int delta; int idx; };
        QVector<Edge> edges;
        edges.reserve(notes.size() * 2);
        for (int i = 0; i < notes.size(); ++i) {
            edges.push_back({notes[i].startTick, +1, i});
            edges.push_back({notes[i].endTick, -1, i});
        }
        std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.delta < b.delta; // OFF before ON at the same tick
        });

        QList<int> active;
        for (const Edge &e : edges) {
            FitNote &n = notes[e.idx];
            if (n.removed) continue;
            if (e.delta < 0) { active.removeOne(e.idx); continue; }
            active.append(e.idx);
            if (active.size() <= opts.targetCeiling) continue;
            if (e.tick < scopeStart || e.tick > scopeEnd) continue;

            // (a) duplicates: same (channel, pitch), starts within 10 ticks.
            for (int i = 0; i < active.size()
                            && active.size() > opts.targetCeiling; ++i) {
                for (int j = i + 1; j < active.size(); ++j) {
                    FitNote &a = notes[active[i]];
                    FitNote &b = notes[active[j]];
                    if (a.channel != b.channel || a.pitch != b.pitch) continue;
                    if (std::abs(a.startTick - b.startTick) > 10) continue;
                    FitNote &victim =
                        (a.endTick - a.startTick != b.endTick - b.startTick)
                            ? ((a.endTick - a.startTick < b.endTick - b.startTick) ? a : b)
                        : (a.velocity != b.velocity)
                            ? ((a.velocity < b.velocity) ? a : b)
                            : ((a.startTick >= b.startTick) ? a : b);
                    if (!victim.drum && victim.thinnable) {
                        const int victimIdx = (&victim == &a) ? active[i] : active[j];
                        removeNote(notes[victimIdx], "duplicate", result.duplicateRemoved);
                        active.removeOne(victimIdx);
                        --i;
                        break;
                    }
                }
            }
            if (active.size() <= opts.targetCeiling) continue;

            // (b) chord limit per channel: keep lowest + highest pitch and
            //     the loudest middles up to the limit.
            if (opts.chordLimit > 0) {
                QMap<int, QList<int>> byChannel;
                for (int idx : active) {
                    if (!notes[idx].drum) byChannel[notes[idx].channel].append(idx);
                }
                for (auto it = byChannel.begin();
                     it != byChannel.end()
                     && active.size() > opts.targetCeiling; ++it) {
                    QList<int> chord = it.value();
                    if (chord.size() <= opts.chordLimit) continue;
                    std::sort(chord.begin(), chord.end(), [&](int x, int y) {
                        return notes[x].pitch < notes[y].pitch;
                    });
                    chord.takeFirst();  // lowest survives
                    chord.takeLast();   // highest survives
                    std::sort(chord.begin(), chord.end(), [&](int x, int y) {
                        if (notes[x].velocity != notes[y].velocity)
                            return notes[x].velocity > notes[y].velocity;
                        return notes[x].pitch > notes[y].pitch;
                    });
                    const int keepMiddles = opts.chordLimit - 2;
                    for (int m = chord.size() - 1;
                         m >= keepMiddles && active.size() > opts.targetCeiling;
                         --m) {
                        if (notes[chord[m]].thinnable && !notes[chord[m]].removed) {
                            removeNote(notes[chord[m]], "chord-limit", result.chordRemoved);
                            active.removeOne(chord[m]);
                        }
                    }
                }
            }
            if (active.size() <= opts.targetCeiling) continue;

            // (c) priority until it fits; protect each channel's outer voices.
            QMap<int, QPair<int, int>> outerPitch;
            for (int idx : active) {
                const FitNote &an = notes[idx];
                if (an.drum) continue;
                auto it = outerPitch.find(an.channel);
                if (it == outerPitch.end()) {
                    outerPitch.insert(an.channel, {an.pitch, an.pitch});
                } else {
                    it->first = std::min(it->first, an.pitch);
                    it->second = std::max(it->second, an.pitch);
                }
            }
            QList<int> inner, outer;
            for (int idx : active) {
                const FitNote &an = notes[idx];
                if (an.drum || !an.thinnable) continue;
                const auto op = outerPitch.value(an.channel);
                if (an.pitch == op.first || an.pitch == op.second)
                    outer.append(idx);
                else
                    inner.append(idx);
            }
            auto lessFn = [&](int x, int y) {
                return ceilingPriorityLess(&notes[x], &notes[y]);
            };
            std::sort(inner.begin(), inner.end(), lessFn);
            std::sort(outer.begin(), outer.end(), lessFn);
            for (int idx : inner + outer) {
                if (active.size() <= opts.targetCeiling) break;
                if (notes[idx].removed) continue;
                removeNote(notes[idx], "voice-ceiling", result.ceilingRemoved);
                active.removeOne(idx);
            }
        }
    }

    // ---- Pass 2: per-TRACK rate desaturation ------------------------------
    // One FFXIV performer = one track. A sliding 250 ms window marks notes in
    // passages denser than the threshold; each dense run is thinned to
    // 1-of-N, keeping the loudest note per group (accents survive). This is
    // the automated version of the manual "halve or third the run" edit.
    if (opts.desaturateRates) {
        // 1-second window: perceived density (a cymbal "wall", a staccato
        // run) builds over seconds - a 250 ms burst window never sees a
        // steady 8th-note pattern even though it saturates the performer.
        const int windowMs = 1000;

        // Stream = the unit density is measured on. Melodic tracks are ONE
        // stream (a staccato run wanders across pitches). Percussion tracks
        // (drum channel) are one stream PER PITCH, because there every pitch
        // is a different instrument: a dense C6 crash wall must thin while a
        // lone C5 china accent sitting inside it survives untouched.
        QMap<QPair<int, int>, QList<int>> byStream;
        for (int i = 0; i < notes.size(); ++i) {
            if (!notes[i].removed && notes[i].thinnable) {
                const int streamPitch = notes[i].drum ? notes[i].pitch : -1;
                byStream[qMakePair(notes[i].track, streamPitch)].append(i);
            }
        }
        for (auto it = byStream.begin(); it != byStream.end(); ++it) {
            const int pct = qBound(
                0, opts.ratePercentPerTrack.value(it.key().first, opts.ratePercent),
                85);
            if (pct <= 0) continue;
            QList<int> idxs = it.value();
            std::sort(idxs.begin(), idxs.end(), [&](int x, int y) {
                if (notes[x].startMs != notes[y].startMs)
                    return notes[x].startMs < notes[y].startMs;
                return notes[x].pitch < notes[y].pitch;
            });
            const int n = idxs.size();
            // Percussion pitch-streams below 8 hits are accent figures (a
            // lone china, a 3-hit fill), never walls - leave them alone.
            const int minStream = (it.key().second >= 0) ? 8 : 3;
            if (n < minStream) continue;
            const int quota = (n * pct) / 100;
            if (quota <= 0) continue;

            // Density per note: peak size of any 1-second window containing
            // it. The percent target is met by auto-tuning a density cutoff
            // for THIS track - densest passages thin first, and the slider
            // works at every tempo and for every instrument (a slow ride
            // wall and a shred run are both relative, not absolute, density).
            QVector<bool> hot(n, false);
            auto markHot = [&](int winLimit) {
                hot.fill(false);
                int j0 = 0;
                for (int j1 = 0; j1 < n; ++j1) {
                    while (notes[idxs[j1]].startMs - notes[idxs[j0]].startMs
                           > windowMs)
                        ++j0;
                    if (j1 - j0 + 1 > winLimit) {
                        for (int k = j0; k <= j1; ++k) hot[k] = true;
                    }
                }
                // exact keep-1-of-N removals over the resulting runs
                int removals = 0;
                int k = 0;
                while (k < n) {
                    if (!hot[k]) { ++k; continue; }
                    int k2 = k;
                    while (k2 < n && hot[k2]) ++k2;
                    for (int g = k; g < k2; g += opts.rateKeepOneOf) {
                        const int len = std::min(g + opts.rateKeepOneOf, k2) - g;
                        if (len >= 2) removals += len - 1;
                    }
                    k = k2;
                }
                return removals;
            };
            // Highest cutoff (densest passages only) whose thinning still
            // reaches the quota; walk down until it does or the floor is hit.
            int maxWin = 1;
            {
                int j0 = 0;
                for (int j1 = 0; j1 < n; ++j1) {
                    while (notes[idxs[j1]].startMs - notes[idxs[j0]].startMs
                           > windowMs)
                        ++j0;
                    maxWin = std::max(maxWin, j1 - j0 + 1);
                }
            }
            int chosen = 1;
            for (int winLimit = maxWin - 1; winLimit >= 1; --winLimit) {
                if (markHot(winLimit) >= quota) { chosen = winLimit; break; }
            }
            markHot(chosen); // leave `hot` marked at the chosen cutoff

            int k = 0;
            while (k < n) {
                if (!hot[k]) { ++k; continue; }
                int k2 = k;
                while (k2 < n && hot[k2]) ++k2;
                for (int g = k; g < k2; g += opts.rateKeepOneOf) {
                    const int gEnd = std::min(g + opts.rateKeepOneOf, k2);
                    if (gEnd - g < 2) continue;
                    // Default: keep the HIGHER pitch of each group - in
                    // two-voice passages the upper voice usually carries the
                    // melody, and fixer-normalized files have no velocity
                    // information anyway. With preferLoudest, accents win
                    // first and pitch only breaks ties.
                    int keep = idxs[g];
                    for (int m = g; m < gEnd; ++m) {
                        const FitNote &cand = notes[idxs[m]];
                        const FitNote &cur = notes[keep];
                        bool better;
                        if (opts.preferLoudest) {
                            better = cand.velocity > cur.velocity
                                     || (cand.velocity == cur.velocity
                                         && cand.pitch > cur.pitch);
                        } else {
                            better = cand.pitch > cur.pitch
                                     || (cand.pitch == cur.pitch
                                         && cand.velocity > cur.velocity);
                        }
                        if (better) keep = idxs[m];
                    }
                    for (int m = g; m < gEnd; ++m) {
                        if (idxs[m] == keep || notes[idxs[m]].removed) continue;
                        removeNote(notes[idxs[m]], "note-rate", result.rateRemoved);
                    }
                }
                k = k2;
            }
        }
    }

    // ---- After metrics + per-track breakdown ------------------------------
    result.remainingPeak = rawPeakInScope(true, nullptr);
    {
        QMap<int, AutoFitTrackSummary> perTrack;
        for (const FitNote &n : notes) {
            if (!n.inScope) continue;
            AutoFitTrackSummary &s = perTrack[n.track];
            s.track = n.track;
            ++s.notes;
            if (n.removed) ++s.removed;
        }
        // Every track with notes in scope is reported (removed may be 0) so
        // the dialog's track list can show live per-track statistics.
        for (auto it = perTrack.begin(); it != perTrack.end(); ++it) {
            MidiTrack *t = file->track(it->track);
            it->name = t ? t->name() : QString();
            result.trackSummaries.append(it.value());
        }
    }

    // ---- Live run: one protocol action, bulk per-channel snapshots --------
    if (!opts.dryRun && result.removedCount > 0) {
        file->protocol()->startNewAction("Auto-fit voice load");
        QMap<int, QList<NoteOnEvent *>> byChannel;
        for (const FitNote &n : notes) {
            if (n.removed) byChannel[n.channel].append(n.on);
        }
        for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
            MidiChannel *channel = file->channel(it.key());
            if (!channel) continue;
            ProtocolEntry *snap = channel->copy();
            for (NoteOnEvent *on : it.value()) {
                if (on->offEvent()) channel->removeEvent(on->offEvent(), false);
                channel->removeEvent(on, false);
            }
            channel->protocol(snap, channel);
        }
        file->protocol()->endAction();
        result.victims.clear();
    }

    result.ok = true;
    return result;
}
