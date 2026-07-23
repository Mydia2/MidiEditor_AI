/*
 * test_auto_fit_voice_load
 *
 * Unit tests for src/converter/AutoFitVoiceLoadService — the v2.1.0
 * "Auto-fit voice load" headless thinning engine (roadmap Phase 35).
 *
 * Coverage:
 *   1. noOverflow_removesNothing        — a fitting file is untouched and a
 *      live run creates NO protocol step (startNewAction is never called
 *      when removedCount == 0).
 *   2. seventeenVoiceChord_removesOne_protectsOuter — 17-voice chord over a
 *      ceiling of 16 loses exactly the quietest INNER voice; the outermost
 *      pitches survive.
 *   3. drumExemption_voicePass          — channel 9 is never thinned by the
 *      voice-ceiling pass.
 *   4. rangeScoping                     — overflows outside the requested
 *      [startTick, endTick] window are left alone.
 *   5. dryRun_matchesLive_andDoesNotMutate — dryRun predicts the same victim
 *      the live run removes, mutates nothing, and the live run creates
 *      exactly one undo step.
 *   6. determinism                      — two identical files produce
 *      identical removed lists.
 *   7. rateDesaturation_thinsAndKeepsAccents — the PER-TRACK percent-based
 *      rate pass thins ratePercent% of a track's notes, densest passages
 *      first (keeping the loudest note of each 1-of-N group when
 *      preferLoudest is on); a control track opted out via
 *      ratePercentPerTrack {track: 0} is left alone.
 *   8. chordLimit_reducesChord          — the per-channel chord limit drops
 *      the quietest middles and keeps lowest + highest + loudest middle.
 *   9. trackSummaries_reportScopedCounts — totalNotesInScope counts every
 *      in-scope note; trackSummaries has one entry per track WITH in-scope
 *      notes (including removed == 0 tracks, for the dialog's per-track
 *      checkbox list), each carrying that track's in-scope note count.
 *  10. rateKeepOneOf_controlsAggressiveness — keep-1-of-3 removes more than
 *      keep-1-of-2 on identical fixtures.
 *  11. rateDefault_keepsUpperVoice — with preferLoudest OFF (default) the
 *      rate pass keeps the HIGHER pitch of each group (upper voice =
 *      melody; fixer-normalized files carry no velocity information).
 *  12. trackFilter_limitsVictims — with trackFilter = {1} only track 1's
 *      notes may be removed (track 0's equally dense run is untouched);
 *      an empty filter thins both dense runs.
 *  13. perTrackPercent_overridesGlobal — ratePercentPerTrack turns the rate
 *      pass on for one track while the global ratePercent is 0 (off);
 *      entries for tracks that do not exist change nothing (global
 *      fallback).
 *  14. percussionStreams_thinPerPitch — on the drum channel (9) density
 *      streams are per (track, pitch), because every drum pitch is a
 *      different instrument: a dense crash wall thins to its own quota
 *      while interleaved accents on another drum pitch survive untouched.
 *
 * Strategy
 * --------
 * Same ODR-shim harness as test_tempo_conversion_service (see the file
 * header there for the full rationale):
 *   - real .cpps:  AutoFitVoiceLoadService + MidiChannel +
 *                  ChannelVisibilityManager + NoteOnEvent / OffEvent /
 *                  OnEvent / ProgChangeEvent + the protocol/* TUs;
 *   - shimmed:    MidiFile, MidiTrack, MidiEvent base, GraphicObject,
 *                 EventWidget, Appearance.
 *
 * The remodeled service uses RAW concurrency for the voice pass (no
 * sample-tail extension; FfxivVoiceLoadCore is no longer referenced) and a
 * per-STREAM percent quota for rate desaturation: melodic tracks are one
 * stream per track, percussion (channel 9) one stream per (track, pitch).
 * 1-second-window densities feed an auto-tuned cutoff that thins the
 * densest passages first until about ratePercent% of the stream's
 * thinnable notes are removed (quota = n*pct/100, integer division;
 * streams with fewer than 3 notes are never thinned). The MidiFile shim
 * uses a LINEAR 1:1 tick<->ms model (timeMS(t) == t, tick(ms) == ms) so
 * window arithmetic is exact: N ticks apart == N ms apart. The service
 * resolves per-track names via MidiFile::track(n)->name(), so the MidiTrack
 * shim provides a name() returning an empty QString.
 * MidiChannel::reloadState calls _midiFile->calcMaxTime() for the tempo
 * channel, so the shim provides it as a no-op (same as test_midi_channel).
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QList>
#include <QMultiMap>
#include <QSet>

// ---- Forward decls used by ODR shims ------------------------------------
class MidiTrack;
class MidiFile;
class Protocol;
class MidiChannel;
class MidiEvent;
class TextEvent;

// ---- ODR shims: Appearance ----------------------------------------------
#include <QColor>
class Appearance {
public:
    static QColor *channelColor(int n);
    static QColor borderColor();
};
QColor *Appearance::channelColor(int /*n*/) {
    static QColor c(Qt::black);
    return &c;
}
QColor Appearance::borderColor() { return QColor(Qt::black); }

// (real ChannelVisibilityManager.cpp is linked into the test binary.)

// ---- ODR shim: MidiFile -------------------------------------------------
// Just enough to back the service (timeMS / tick / endTick / channel /
// protocol) plus the members the linked midi/protocol TUs reference
// (calcMaxTime, setSaved, channelEvents, ...). Linear 1:1 time model.
#include "../src/protocol/Protocol.h"
class MidiFile {
public:
    MidiFile();
    ~MidiFile();

    Protocol *protocol();
    MidiChannel *channel(int i);
    QMultiMap<int, MidiEvent *> *channelEvents(int channel);
    MidiTrack *track(int n);
    int endTick();
    int ticksPerQuarter();
    void calcMaxTime();
    void setMaxLengthMs(int ms);
    int msOfTick(int tick, QList<MidiEvent *> *events = nullptr,
                 int msOfFirstEventInList = 0);
    int timeMS(int midiTime);
    int tick(int ms);
    void setSaved(bool b);

    // test-only helpers
    void setProtocol(Protocol *p) { _protocol = p; }
    void addTrack(MidiTrack *t) { _tracks.append(t); }
    void setEndTick(int t) { _endTick = t; }

private:
    Protocol *_protocol;
    MidiChannel *_channels[19];
    QList<MidiTrack *> _tracks;
    int _endTick;
};

// ---- ODR shim: MidiTrack ------------------------------------------------
#include "../src/protocol/ProtocolEntry.h"
class MidiTrack : public ProtocolEntry {
public:
    explicit MidiTrack(int n) : _num(n), _hidden(false) {}
    ~MidiTrack() override = default;

    int number();
    QString name();
    void setNumber(int n) { _num = n; }
    void setNameEvent(TextEvent *nameEvent);
    TextEvent *nameEvent();
    bool hidden() const { return _hidden; }

    // ProtocolEntry interface
    ProtocolEntry *copy() override { return new MidiTrack(_num); }
    void reloadState(ProtocolEntry *) override {}
    QString typeString() { return QStringLiteral("MidiTrack"); }

private:
    int _num;
    bool _hidden;
};
int MidiTrack::number() { return _num; }
QString MidiTrack::name() { return QString(); }
void MidiTrack::setNameEvent(TextEvent *) {}
TextEvent *MidiTrack::nameEvent() { return nullptr; }

// ---- ODR shim: GraphicObject / EventWidget ------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shim: MidiEvent base -------------------------------------------
// setMidiTime re-keys events in their channel's QMultiMap (same as the
// tempo-conversion harness) so the channel map is a faithful event store
// for both insertion and MidiChannel::removeEvent.
#include "../src/MidiEvent/MidiEvent.h"

quint8 MidiEvent::_startByte = 0;
EventWidget *MidiEvent::_eventWidget = nullptr;

MidiEvent::MidiEvent(int channel, MidiTrack *track) {
    _track = track;
    numChannel = channel;
    timePos = 0;
    midiFile = nullptr;
    _tempID = -1;
}

MidiEvent::MidiEvent(MidiEvent &other)
    : ProtocolEntry(other), GraphicObject() {
    _track = other._track;
    numChannel = other.numChannel;
    timePos = other.timePos;
    midiFile = other.midiFile;
    _tempID = other._tempID;
}

MidiFile *MidiEvent::file() { return midiFile; }
void MidiEvent::setFile(MidiFile *f) { midiFile = f; }
int MidiEvent::line() { return UNKNOWN_LINE; }
QString MidiEvent::toMessage() { return QString(); }
QByteArray MidiEvent::save() { return QByteArray(); }
void MidiEvent::draw(QPainter *, QColor) {}
ProtocolEntry *MidiEvent::copy() { return new MidiEvent(*this); }
void MidiEvent::reloadState(ProtocolEntry *) {}
QString MidiEvent::typeString() { return QStringLiteral("MidiEvent"); }
bool MidiEvent::isOnEvent() { return false; }
void MidiEvent::moveToChannel(int channel, bool) { numChannel = channel; }
int MidiEvent::channel() {
    if (numChannel < 0 || numChannel > 18) return 0;
    return numChannel;
}
MidiTrack *MidiEvent::track() { return _track; }

void MidiEvent::setMidiTime(int t, bool /*toProtocol*/) {
    if (midiFile) {
        QMultiMap<int, MidiEvent *> *map =
            midiFile->channelEvents(numChannel);
        if (map) {
            map->remove(timePos, this);
            timePos = t;
            map->insert(t, this);
            return;
        }
    }
    timePos = t;
}
int MidiEvent::midiTime() { return timePos; }

// ---- Real headers (after shims so our shim names win) -------------------
#include "../src/midi/MidiChannel.h"
#include "../src/MidiEvent/NoteOnEvent.h"
#include "../src/MidiEvent/OffEvent.h"
#include "../src/MidiEvent/OnEvent.h"
#include "../src/MidiEvent/ProgChangeEvent.h"
#include "../src/converter/AutoFitVoiceLoadService.h"

// ---- MidiFile shim implementation (now that MidiChannel is fully known) -
MidiFile::MidiFile()
    : _protocol(nullptr), _endTick(200000) {
    for (int i = 0; i < 19; ++i) {
        _channels[i] = new MidiChannel(this, i);
    }
}
MidiFile::~MidiFile() {
    for (int i = 0; i < 19; ++i) delete _channels[i];
    qDeleteAll(_tracks);
}
Protocol *MidiFile::protocol() { return _protocol; }
void MidiFile::setSaved(bool) {}
void MidiFile::setMaxLengthMs(int) {}
MidiChannel *MidiFile::channel(int i) {
    if (i < 0 || i > 18) return nullptr;
    return _channels[i];
}
QMultiMap<int, MidiEvent *> *MidiFile::channelEvents(int c) {
    MidiChannel *ch = channel(c);
    return ch ? ch->eventMap() : nullptr;
}
MidiTrack *MidiFile::track(int n) {
    for (MidiTrack *t : _tracks) {
        if (t->number() == n) return t;
    }
    return _tracks.isEmpty() ? nullptr : _tracks.first();
}
int MidiFile::endTick() { return _endTick; }
int MidiFile::ticksPerQuarter() { return 480; }
void MidiFile::calcMaxTime() {
    // no-op: MidiChannel::reloadState calls this for the tempo channel; the
    // linear time model has no cached length to recompute.
}
int MidiFile::msOfTick(int tick, QList<MidiEvent *> * /*events*/,
                       int /*msOfFirstEventInList*/) {
    return tick; // linear 1:1 model
}
int MidiFile::timeMS(int midiTime) { return midiTime; } // 1 tick == 1 ms
int MidiFile::tick(int ms) { return ms; }               // inverse

// =========================================================================
// Test helpers
// =========================================================================
namespace {

struct ScopedFile {
    MidiFile *file;
    Protocol *protocol;
    MidiTrack *track0;
    MidiTrack *track1;

    ScopedFile() {
        file = new MidiFile();
        protocol = new Protocol(file);
        file->setProtocol(protocol);
        track0 = new MidiTrack(0);
        track1 = new MidiTrack(1);
        file->addTrack(track0);
        file->addTrack(track1);
    }
    ~ScopedFile() {
        delete file;
        delete protocol;
    }

    NoteOnEvent *addNote(int channel, int tick, int durationTicks,
                         int pitch = 60, int velocity = 100,
                         MidiTrack *trk = nullptr) {
        if (!trk) trk = track0;
        NoteOnEvent *on = new NoteOnEvent(pitch, velocity, channel, trk);
        OffEvent *off = new OffEvent(channel, 127 - pitch, trk);
        on->setFile(file);
        off->setFile(file);
        on->setMidiTime(tick, false);
        off->setMidiTime(tick + durationTicks, false);
        return on;
    }
};

int countNotesWithPitch(MidiFile *file, int channelNum, int pitch) {
    int n = 0;
    QMultiMap<int, MidiEvent *> *map = file->channel(channelNum)->eventMap();
    for (auto it = map->constBegin(); it != map->constEnd(); ++it) {
        NoteOnEvent *on = dynamic_cast<NoteOnEvent *>(it.value());
        if (on && on->note() == pitch) ++n;
    }
    return n;
}

const AutoFitTrackSummary *summaryFor(const AutoFitResult &r, int track) {
    for (const AutoFitTrackSummary &s : r.trackSummaries) {
        if (s.track == track) return &s;
    }
    return nullptr;
}

AutoFitOptions baseOptions() {
    AutoFitOptions o;
    o.desaturateRates = false; // rate pass off unless a test opts in
    o.chordLimit = 0;          // chord pass off unless a test opts in
    o.targetCeiling = 16;
    o.dryRun = true;
    return o;
}

} // namespace

// =========================================================================
class TestAutoFitVoiceLoadService : public QObject {
    Q_OBJECT
private slots:
    void noOverflow_removesNothing();
    void seventeenVoiceChord_removesOne_protectsOuter();
    void drumExemption_voicePass();
    void rangeScoping();
    void dryRun_matchesLive_andDoesNotMutate();
    void determinism();
    void rateDesaturation_thinsAndKeepsAccents();
    void chordLimit_reducesChord();
    void trackSummaries_reportScopedCounts();
    void rateKeepOneOf_controlsAggressiveness();
    void rateDefault_keepsUpperVoice();
    void trackFilter_limitsVictims();
    void perTrackPercent_overridesGlobal();
    void percussionStreams_thinPerPitch();
};

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::noOverflow_removesNothing() {
    ScopedFile f;
    // 8 concurrent long notes on channels 0-7: well below the 16 ceiling.
    for (int ch = 0; ch < 8; ++ch) {
        f.addNote(ch, 1000, 6000, 60, 100);
    }

    AutoFitOptions o = baseOptions();
    o.dryRun = false; // LIVE run — must still not open a protocol action

    const int stepsBefore = f.protocol->stepsBack();
    const int redoBefore = f.protocol->stepsForward();
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.removedCount, 0);
    QCOMPARE(r.removed.size(), 0);
    QCOMPARE(r.overflowRangeCount, 0);
    // No protocol step was created (startNewAction never called).
    QCOMPARE(f.protocol->stepsBack(), stepsBefore);
    QCOMPARE(f.protocol->stepsForward(), redoBefore);
    // File untouched: every channel still has its on+off pair.
    for (int ch = 0; ch < 8; ++ch) {
        QCOMPARE(int(f.file->channel(ch)->eventMap()->size()), 2);
    }
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::seventeenVoiceChord_removesOne_protectsOuter() {
    ScopedFile f;
    // 17-voice chord on ONE channel, pitches 40..56. The inner voice at
    // pitch 48 is the quietest (velocity 1); everything else is velocity 100.
    for (int i = 0; i < 17; ++i) {
        const int pitch = 40 + i;
        f.addNote(0, 1000, 6000, pitch, (pitch == 48) ? 1 : 100);
    }

    AutoFitOptions o = baseOptions(); // ceiling 16, chordLimit off, dryRun
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QVERIFY(r.overflowRangeCount >= 1);
    QCOMPARE(r.removedCount, 1);
    QCOMPARE(r.ceilingRemoved, 1);
    QCOMPARE(r.removed.size(), 1);
    // The quietest INNER voice goes; the outermost pitches are protected.
    QCOMPARE(r.removed[0].pitch, 48);
    QCOMPARE(r.removed[0].channel, 0);
    QCOMPARE(r.removed[0].track, 0);
    QCOMPARE(r.removed[0].tick, 1000);
    QCOMPARE(r.removed[0].velocity, 1);
    QCOMPARE(r.removed[0].reason, QStringLiteral("voice-ceiling"));
    for (const AutoFitRemovedNote &rn : r.removed) {
        QVERIFY(rn.pitch != 40); // lowest survives
        QVERIFY(rn.pitch != 56); // highest survives
    }
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::drumExemption_voicePass() {
    ScopedFile f;
    // 17 concurrent notes on the percussion channel: over the ceiling, but
    // channel 9 is exempt from the voice-ceiling pass and the rate pass is
    // off, so nothing may be removed.
    for (int i = 0; i < 17; ++i) {
        f.addNote(9, 1000, 6000, 30 + i, 100);
    }

    AutoFitOptions o = baseOptions();
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.peakBefore, 17); // the overflow IS visible in the metrics...
    QCOMPARE(r.removedCount, 0); // ...but percussion is never thinned
    QCOMPARE(r.removed.size(), 0);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::rangeScoping() {
    ScopedFile f;
    // Massive overflow at ticks 1000-7000 (20 voices)...
    for (int i = 0; i < 20; ++i) {
        f.addNote(0, 1000, 6000, 40 + i, 100);
    }

    // ...but the requested range lies elsewhere.
    AutoFitOptions o = baseOptions();
    o.startTick = 10000;
    o.endTick = 20000;
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.removedCount, 0);
    QCOMPARE(r.removed.size(), 0);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::dryRun_matchesLive_andDoesNotMutate() {
    ScopedFile f;
    for (int i = 0; i < 17; ++i) {
        const int pitch = 40 + i;
        f.addNote(0, 1000, 6000, pitch, (pitch == 48) ? 1 : 100);
    }
    QMultiMap<int, MidiEvent *> *map0 = f.file->channel(0)->eventMap();
    QCOMPARE(int(map0->size()), 34); // 17 on + 17 off

    // Dry run: predicts one victim, mutates nothing, no protocol action.
    AutoFitOptions dry = baseOptions();
    AutoFitResult rd = AutoFitVoiceLoadService::apply(f.file, dry);
    QVERIFY(rd.ok);
    QCOMPARE(rd.removedCount, 1);
    QCOMPARE(rd.removed[0].pitch, 48);
    QCOMPARE(rd.victims.size(), 1); // preview pointers stay valid on dryRun
    QCOMPARE(int(map0->size()), 34);
    QCOMPARE(countNotesWithPitch(f.file, 0, 48), 1);
    QCOMPARE(f.protocol->stepsBack(), 0);

    // Live run with the same options: removes the SAME note (on + off pair)
    // in exactly one undo step.
    AutoFitOptions live = dry;
    live.dryRun = false;
    AutoFitResult rl = AutoFitVoiceLoadService::apply(f.file, live);
    QVERIFY(rl.ok);
    QCOMPARE(rl.removedCount, 1);
    QCOMPARE(rl.removed[0].pitch, rd.removed[0].pitch);
    QCOMPARE(rl.removed[0].tick, rd.removed[0].tick);
    QCOMPARE(rl.removed[0].channel, rd.removed[0].channel);
    QVERIFY(rl.victims.isEmpty()); // cleared after a live run
    QCOMPARE(int(map0->size()), 32); // on + off removed
    QCOMPARE(countNotesWithPitch(f.file, 0, 48), 0);
    QCOMPARE(f.protocol->stepsBack(), 1); // exactly ONE protocol step
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::determinism() {
    auto build = [](ScopedFile &f) {
        for (int i = 0; i < 20; ++i) {
            f.addNote(0, 1000, 6000, 40 + i, 20 + ((i * 13) % 90));
        }
    };
    ScopedFile a;
    ScopedFile b;
    build(a);
    build(b);

    AutoFitOptions o = baseOptions();
    AutoFitResult ra = AutoFitVoiceLoadService::apply(a.file, o);
    AutoFitResult rb = AutoFitVoiceLoadService::apply(b.file, o);

    QVERIFY(ra.ok);
    QVERIFY(rb.ok);
    QVERIFY(ra.removedCount > 0);
    QCOMPARE(ra.removedCount, rb.removedCount);
    QCOMPARE(ra.removed.size(), rb.removed.size());
    for (int i = 0; i < ra.removed.size(); ++i) {
        QCOMPARE(ra.removed[i].tick, rb.removed[i].tick);
        QCOMPARE(ra.removed[i].channel, rb.removed[i].channel);
        QCOMPARE(ra.removed[i].pitch, rb.removed[i].pitch);
        QCOMPARE(ra.removed[i].velocity, rb.removed[i].velocity);
        QCOMPARE(ra.removed[i].reason, rb.removed[i].reason);
    }
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::rateDesaturation_thinsAndKeepsAccents() {
    ScopedFile f;
    // Dense run on TRACK 0: 30 staccato notes (4 ticks each) every 8 ticks.
    // With the 1:1 model all 30 NoteOns sit inside ONE 1-second window, so
    // every density cutoff marks the whole run hot. ratePercent = 50 ->
    // quota = 30*50/100 = 15; keep-1-of-2 over the fully-hot run removes
    // exactly 15, so the highest cutoff (winLimit 29) already reaches the
    // quota. Raw concurrency never exceeds 1 (4-tick notes, 8-tick gaps),
    // so the voice pass is idle and every removal must come from the
    // per-track rate pass.
    const int accentTick = 1000 + 8 * 15;
    for (int i = 0; i < 30; ++i) {
        f.addNote(0, 1000 + 8 * i, 4, 60, (i == 15) ? 127 : 100, f.track0);
    }
    // Sparse control run on TRACK 1: 5 notes 1000 ticks (= 1000 ms) apart.
    // The percent quota applies per track REGARDLESS of absolute density
    // (50% of 5 notes would remove 2), so the control track opts out via a
    // per-track 0% override — it must then lose nothing.
    for (int i = 0; i < 5; ++i) {
        f.addNote(1, 20000 + 1000 * i, 4, 72, 100, f.track1);
    }

    AutoFitOptions o = baseOptions();
    o.desaturateRates = true;
    o.ratePercent = 50;                 // 50% of track 0's 30 notes = 15
    o.ratePercentPerTrack.insert(1, 0); // control track: rate pass off
    // The default keep rule prefers the higher pitch (see test 11); opt in
    // to accent preservation so the velocity-127 assertion is meaningful.
    o.preferLoudest = true;
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QVERIFY(r.rateRemoved > 0);
    QCOMPARE(r.rateRemoved, r.removedCount); // nothing from other passes
    // Quota 15 over the fully-hot 30-note run: 15 pairs, one removal each.
    QCOMPARE(r.rateRemoved, 15);
    for (const AutoFitRemovedNote &rn : r.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QCOMPARE(rn.track, 0);           // the opted-out track is untouched
        // The accented note survives (loudest of its group is kept).
        QVERIFY(rn.tick != accentTick);
        QVERIFY(rn.velocity != 127);
    }
    const AutoFitTrackSummary *sparse = summaryFor(r, 1);
    QVERIFY(sparse);
    QCOMPARE(sparse->removed, 0);        // per-track 0% override respected
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::chordLimit_reducesChord() {
    ScopedFile f;
    // 6-note chord on channel 0 (pitches 60..65; the middle voice 62 is the
    // loudest at velocity 120) + 13 overlapping single notes on channels
    // 1..13 = 19 voices against a ceiling of 16. The three removals must all
    // come from the chord-limit pass: the quietest middles (61, 63, 64) go;
    // the outer voices (60, 65) and the loudest middle (62) survive.
    //
    // NOTE: the fixture uses 13 singles (19 voices -> 3 removals), not the
    // 14/20 the original spec sketched: with 20 voices a 4th removal is
    // forced, and since the singles are each their channel's outer voice,
    // the ceiling pass would then have to take the last remaining inner
    // chord voice — pitch 62 — by design. 19 voices exercises the same
    // chord-limit policy without that arithmetic dead-end.
    for (int p = 60; p <= 65; ++p) {
        f.addNote(0, 1000, 6000, p, (p == 62) ? 120 : 100);
    }
    for (int ch = 1; ch <= 13; ++ch) {
        // Staggered starts keep the sweep order deterministic; all overlap
        // the chord (durations 6000).
        f.addNote(ch, 1000 + 100 * ch, 6000, 50, 100);
    }

    AutoFitOptions o = baseOptions();
    o.chordLimit = 3;
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QVERIFY(r.chordRemoved >= 3);
    QCOMPARE(r.removedCount, 3);
    QSet<int> removedChordPitches;
    for (const AutoFitRemovedNote &rn : r.removed) {
        QCOMPARE(rn.reason, QStringLiteral("chord-limit"));
        QCOMPARE(rn.channel, 0); // singles are never touched
        removedChordPitches.insert(rn.pitch);
    }
    QVERIFY(!removedChordPitches.contains(60)); // lowest survives
    QVERIFY(!removedChordPitches.contains(62)); // loudest middle survives
    QVERIFY(!removedChordPitches.contains(65)); // highest survives
    QCOMPARE(removedChordPitches, QSet<int>({61, 63, 64}));
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::trackSummaries_reportScopedCounts() {
    ScopedFile f;
    // Same shape as the rate test: dense 30-note run on track 0, sparse
    // 5-note run on track 1 (opted out via its per-track 0% so only track 0
    // gets removals — the percent quota ignores absolute density).
    for (int i = 0; i < 30; ++i) {
        f.addNote(0, 1000 + 8 * i, 4, 60, 100, f.track0);
    }
    for (int i = 0; i < 5; ++i) {
        f.addNote(1, 20000 + 1000 * i, 4, 72, 100, f.track1);
    }

    AutoFitOptions o = baseOptions();
    o.desaturateRates = true;
    o.ratePercent = 50;                 // quota: 50% of 30 = 15 on track 0
    o.ratePercentPerTrack.insert(1, 0); // sparse control: rate pass off
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.totalNotesInScope, 35); // every in-scope note, both tracks
    QCOMPARE(r.rateRemoved, 15);       // exactly track 0's 50% quota
    // One entry per track with in-scope notes — including the untouched
    // sparse track (removed == 0), for the dialog's per-track stats list.
    QCOMPARE(r.trackSummaries.size(), 2);
    const AutoFitTrackSummary *dense = summaryFor(r, 0);
    QVERIFY(dense);
    QCOMPARE(dense->notes, 30);        // the dense track's in-scope count
    QCOMPARE(dense->removed, 15);      // 30*50/100 pair-removals
    QCOMPARE(dense->removed, r.rateRemoved);
    QVERIFY(dense->removed < dense->notes); // thinning, not wholesale deletion
    const AutoFitTrackSummary *sparse = summaryFor(r, 1);
    QVERIFY(sparse);
    QCOMPARE(sparse->notes, 5);        // listed with its in-scope count...
    QCOMPARE(sparse->removed, 0);      // ...but nothing removed
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::rateKeepOneOf_controlsAggressiveness() {
    auto build = [](ScopedFile &f) {
        for (int i = 0; i < 30; ++i) {
            f.addNote(0, 1000 + 8 * i, 4, 60, 100, f.track0);
        }
    };
    ScopedFile a;
    ScopedFile b;
    build(a);
    build(b);

    AutoFitOptions o2 = baseOptions();
    o2.desaturateRates = true;
    o2.ratePercent = 80;  // quota = 30*80/100 = 24 — more than keep-1-of-N
                          // can ever remove, so the cutoff walks to the
                          // floor and the group rule caps the removals
    o2.rateKeepOneOf = 2; // halve: remove 1 of every 2
    AutoFitOptions o3 = o2;
    o3.rateKeepOneOf = 3; // third: remove 2 of every 3

    AutoFitResult r2 = AutoFitVoiceLoadService::apply(a.file, o2);
    AutoFitResult r3 = AutoFitVoiceLoadService::apply(b.file, o3);

    QVERIFY(r2.ok);
    QVERIFY(r3.ok);
    QVERIFY(r2.rateRemoved > 0);
    QVERIFY(r3.rateRemoved > r2.rateRemoved);
    // Exact model: quota 24 is unreachable for either N, so the whole run
    // is thinned at cutoff 1: keep-1-of-2 -> 15 pair-removals; keep-1-of-3
    // -> 10 triplet-groups removing 2 each = 20.
    QCOMPARE(r2.rateRemoved, 15);
    QCOMPARE(r3.rateRemoved, 20);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::rateDefault_keepsUpperVoice() {
    ScopedFile f;
    // Two-voice passage on ONE track: 20 simultaneous (lower 60, upper 72)
    // pairs, all velocity 100, pairs 8 ticks apart, short notes. Sorted by
    // (startMs, pitch) each keep-1-of-2 group is exactly one pair, so with
    // preferLoudest OFF (default) every removal must be the LOWER voice.
    for (int i = 0; i < 20; ++i) {
        f.addNote(0, 1000 + 8 * i, 4, 60, 100, f.track0);
        f.addNote(0, 1000 + 8 * i, 4, 72, 100, f.track0);
    }

    AutoFitOptions o = baseOptions();
    o.desaturateRates = true; // keep 1 of 2, preferLoudest = false (defaults)
    o.ratePercent = 50;       // quota = 40*50/100 = 20 = one removal per
                              // pair (the default 10% would stop after 4)
    o.targetCeiling = 32;     // voice pass idle (raw concurrency is only 2)
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.rateRemoved, 20); // one removal per pair
    QCOMPARE(r.rateRemoved, r.removedCount);
    for (const AutoFitRemovedNote &rn : r.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QCOMPARE(rn.pitch, 60); // the lower voice goes, the melody survives
    }
    // Every upper-voice note is still in the file's candidate set: none of
    // the 20 pitch-72 notes appear in the removed list (checked above via
    // pitch == 60) and the channel map is untouched on a dry run.
    QCOMPARE(countNotesWithPitch(f.file, 0, 72), 20);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::trackFilter_limitsVictims() {
    ScopedFile f;
    // Dense 30-note run on TRACK 0 (ticks 1000..1232, channel 0).
    for (int i = 0; i < 30; ++i) {
        f.addNote(0, 1000 + 8 * i, 4, 60, 100, f.track0);
    }
    // Equally dense 30-note run on TRACK 1 (ticks 5000..5232, channel 1) —
    // offset so the two runs do not overlap.
    for (int i = 0; i < 30; ++i) {
        f.addNote(1, 5000 + 8 * i, 4, 60, 100, f.track1);
    }
    // Sparse control on its OWN track 2: the percent quota is per track, so
    // sharing track 1 would fold these 5 notes into that track's quota math
    // (quota 17 of 35 instead of 15 of 30). Its per-track 0% override keeps
    // it out of the rate pass in both runs below.
    MidiTrack *track2 = new MidiTrack(2);
    f.file->addTrack(track2);
    for (int i = 0; i < 5; ++i) {
        f.addNote(1, 20000 + 1000 * i, 4, 72, 100, track2);
    }

    // --- Filtered run: only track 1 may lose notes. ----------------------
    AutoFitOptions o = baseOptions();
    o.desaturateRates = true;
    o.ratePercent = 50;                 // 50% of each dense 30-note run = 15
    o.ratePercentPerTrack.insert(2, 0); // sparse control: rate pass off
    o.trackFilter = QSet<int>({1});
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.rateRemoved, 15); // exactly track 1's 50%-of-30 quota
    QCOMPARE(r.rateRemoved, r.removedCount);
    for (const AutoFitRemovedNote &rn : r.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QCOMPARE(rn.track, 1);            // never a track-0 victim
        QVERIFY(rn.tick >= 5000);         // from the dense run...
        QVERIFY(rn.tick <= 5232);         // ...not from the sparse control
    }
    // All three tracks are summarised (per-track stats list); track 0's
    // equally dense run lost NOTHING, track 1 carries all 15 removals.
    QCOMPARE(r.trackSummaries.size(), 3);
    const AutoFitTrackSummary *t0 = summaryFor(r, 0);
    QVERIFY(t0);
    QCOMPARE(t0->removed, 0);          // filtered out: never a victim
    QCOMPARE(t0->notes, 30);
    const AutoFitTrackSummary *t1 = summaryFor(r, 1);
    QVERIFY(t1);
    QCOMPARE(t1->removed, 15);
    QCOMPARE(t1->notes, 30);
    const AutoFitTrackSummary *t2 = summaryFor(r, 2);
    QVERIFY(t2);
    QCOMPARE(t2->removed, 0);          // 0% override: never thinned
    QCOMPARE(t2->notes, 5);

    // --- Empty filter on the same (unmutated, dryRun) file: both dense ----
    // runs are thinned to their 50% quota; the 0%-override control still
    // loses nothing.
    AutoFitOptions all = o;
    all.trackFilter.clear();
    AutoFitResult ra = AutoFitVoiceLoadService::apply(f.file, all);

    QVERIFY(ra.ok);
    QCOMPARE(ra.rateRemoved, 30); // 15 per dense run
    QCOMPARE(ra.rateRemoved, ra.removedCount);
    QCOMPARE(ra.trackSummaries.size(), 3);
    const AutoFitTrackSummary *a0 = summaryFor(ra, 0);
    const AutoFitTrackSummary *a1 = summaryFor(ra, 1);
    const AutoFitTrackSummary *a2 = summaryFor(ra, 2);
    QVERIFY(a0);
    QVERIFY(a1);
    QVERIFY(a2);
    QCOMPARE(a0->removed, 15);         // both dense runs thinned now
    QCOMPARE(a1->removed, 15);
    QCOMPARE(a2->removed, 0);          // control still exempt
    int track0Removed = 0, track1Removed = 0;
    for (const AutoFitRemovedNote &rn : ra.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QVERIFY(rn.tick < 20000); // the sparse control is still untouched
        if (rn.track == 0) ++track0Removed;
        if (rn.track == 1) ++track1Removed;
    }
    QCOMPARE(track0Removed, 15);
    QCOMPARE(track1Removed, 15);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::perTrackPercent_overridesGlobal() {
    ScopedFile f;
    // Identical dense 30-note runs on BOTH tracks (channels 0/1, offset so
    // they never overlap; raw concurrency stays 1, voice pass idle).
    // Whether a run is thinned is decided purely by the percent that
    // applies to its track.
    for (int i = 0; i < 30; ++i) {
        f.addNote(0, 1000 + 8 * i, 4, 60, 100, f.track0);
    }
    for (int i = 0; i < 30; ++i) {
        f.addNote(1, 5000 + 8 * i, 4, 60, 100, f.track1);
    }

    // --- Global 0% (off) + 50% override for track 0 only. -----------------
    AutoFitOptions o = baseOptions();
    o.desaturateRates = true;
    o.ratePercent = 0;                   // globally OFF
    o.ratePercentPerTrack.insert(0, 50); // ...but track 0 thins 50%
    AutoFitResult r1 = AutoFitVoiceLoadService::apply(f.file, o);
    QVERIFY(r1.ok);
    // 50% of track 0's 30 notes = quota 15; keep-1-of-2 over the fully-hot
    // run removes exactly 15 — all on track 0.
    QCOMPARE(r1.rateRemoved, 15);
    QCOMPARE(r1.rateRemoved, r1.removedCount);
    for (const AutoFitRemovedNote &rn : r1.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QCOMPARE(rn.track, 0);
    }
    const AutoFitTrackSummary *s0 = summaryFor(r1, 0);
    QVERIFY(s0);
    QCOMPARE(s0->removed, 15);
    QCOMPARE(s0->notes, 30);
    const AutoFitTrackSummary *s1 = summaryFor(r1, 1);
    QVERIFY(s1);
    QCOMPARE(s1->removed, 0); // track 1 keeps the global 0% (off)
    QCOMPARE(s1->notes, 30);

    // --- Fallback: an override for a NONEXISTENT track changes nothing. ---
    AutoFitOptions ghost = baseOptions();
    ghost.desaturateRates = true;
    ghost.ratePercent = 0;                   // globally OFF
    ghost.ratePercentPerTrack.insert(7, 50); // no track 7 in the file
    AutoFitResult r2 = AutoFitVoiceLoadService::apply(f.file, ghost);
    QVERIFY(r2.ok);
    QCOMPARE(r2.rateRemoved, 0);
    QCOMPARE(r2.removedCount, 0);
}

// -------------------------------------------------------------------------
void TestAutoFitVoiceLoadService::percussionStreams_thinPerPitch() {
    ScopedFile f;
    // PERCUSSION (channel 9) density streams are per (track, pitch): every
    // drum pitch is a different instrument. ONE track, two interleaved
    // streams in the SAME time range:
    //  - pitch-84 "crash wall": 30 notes every 8 ticks from tick 1000
    //    (n = 30, quota = 30*50/100 = 15 -> exactly 15 pair-removals, the
    //    highest cutoff already reaches the quota);
    //  - pitch-72 "china accents": 2 notes at ticks 1050/1120, temporally
    //    INSIDE the wall. As their own stream n = 2 falls under the n < 3
    //    guard, so the accents survive untouched. NOTE: 3 accents would NOT
    //    be safe — n == 3 passes the guard and quota = 3*50/100 = 1 removes
    //    one of them; 2 keeps the survival assertion exact.
    // Were percussion lumped per track (the melodic rule), the stream would
    // be n = 32 with quota 16 and the accents inside the hot run would be
    // fair game — the exact 15 / pitch-84-only assertions discriminate.
    for (int i = 0; i < 30; ++i) {
        f.addNote(9, 1000 + 8 * i, 4, 84, 100, f.track0);
    }
    f.addNote(9, 1050, 4, 72, 100, f.track0);
    f.addNote(9, 1120, 4, 72, 100, f.track0);

    AutoFitOptions o = baseOptions();
    o.desaturateRates = true;
    o.ratePercent = 50;
    o.rateKeepOneOf = 2;  // halve (the default, explicit for the math above)
    o.targetCeiling = 32; // voice pass idle (drums are exempt from it anyway)
    AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, o);

    QVERIFY(r.ok);
    QCOMPARE(r.rateRemoved, 15); // the crash wall's own quota, nothing more
    QCOMPARE(r.rateRemoved, r.removedCount); // nothing from other passes
    for (const AutoFitRemovedNote &rn : r.removed) {
        QCOMPARE(rn.reason, QStringLiteral("note-rate"));
        QCOMPARE(rn.track, 0);
        QCOMPARE(rn.channel, 9);
        QCOMPARE(rn.pitch, 84); // never a china accent
    }
}

QTEST_MAIN(TestAutoFitVoiceLoadService)
#include "test_auto_fit_voice_load.moc"
