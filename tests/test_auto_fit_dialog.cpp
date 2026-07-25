/*
 * test_auto_fit_dialog
 *
 * GUI-logic unit tests for src/gui/AutoFitVoiceLoadDialog - the confirmation
 * dialog of the v2.1.0 "Auto-Fit Voice Load" action (roadmap Phase 35).
 *
 * WHY this file exists: the dialog's LOGIC layer (option mapping, the
 * selection-scope pre-check, the per-track percent bookkeeping and the
 * modeless structure guard) had zero automated coverage, while three of the
 * eight bugs the pre-push review found lived exactly there. Every case below
 * pins one of those bug classes, not the widget cosmetics.
 *
 * Coverage:
 *   1. trackFilter_threeWayMapping - the subtle ALL / SUBSET / NONE mapping
 *      of the track checkboxes onto AutoFitOptions::trackFilter: all checked
 *      must produce an EMPTY filter (empty == "all tracks" in the service),
 *      a subset the exact set of checked track numbers, and none the
 *      impossible sentinel {-1} so nothing is removable. Getting ALL wrong
 *      (a full explicit set) still works; getting NONE wrong (empty filter)
 *      silently thins the WHOLE file. Driven through the real All / None
 *      quick buttons plus direct checkbox toggles.
 *   2. selectionScope_preChecksOnlyScopedTracks - just-fixed bug: with a
 *      non-empty selectionScope ONLY the tracks whose notes are in the scope
 *      start checked (the dialog must mirror what the user selected), and
 *      opts.selectionScope carries every scoped note pointer through to the
 *      service.
 *   3. selectionScope_empty_checksEveryTrack - the negative of case 2: with
 *      no scope every track starts checked, the filter stays empty and the
 *      scope set is empty. Also: a scope that covers ALL tracks must still
 *      collapse to the empty ("all") filter, not a full explicit set.
 *   4. perTrackPercents_surviveCheckCycles - _trackPercents semantics: the
 *      intensity slider edits only the CHECKED tracks; unchecked tracks keep
 *      their previously stored percent, and a later check/uncheck cycle does
 *      not lose it. Verified through currentOptions().ratePercentPerTrack -
 *      a regression here silently thins tracks the user tuned to a different
 *      target. Includes the slider MIRROR rule (a checked group sharing one
 *      stored value shows it on the slider).
 *  5a. structureGuard_closesOnTrackCountChange - just-fixed bug: the dialog
 *      is MODELESS, so tracks can be added / removed / reordered behind it.
 *      Live renumbering would misattribute every row (labels, checkboxes,
 *      stored percents) and Apply would thin the wrong track, so
 *      refreshPreview() must CLOSE instead of guessing. Covers add, remove
 *      and the empty-track-list case, i.e. the COUNT half of the guard.
 *  5b. structureGuard_closesOnTrackReorder - the pointer-identity half:
 *      reordering keeps the count intact, so only the per-index walk can
 *      catch it. Split from 5a on purpose - QtTest stops a function at its
 *      first failure, so one function per half keeps a regression in either
 *      half attributable.
 *   6. structureGuard_keepsOpenWhenUnchanged - the negative case that keeps
 *      the guard honest: an unchanged track list must NOT close the dialog
 *      (a guard that always fires would pass 5a / 5b while making the tool
 *      unusable).
 *   7. advancedOptions_reachOptions - ceiling spin, chord checkbox + limit
 *      (unchecked -> chordLimit == 0, the service's "pass off" encoding),
 *      rate checkbox (unchecked -> desaturateRates == false), preferLoudest,
 *      the off-by-one rateKeepOneOf == slider + 1, the tick range handed to
 *      the constructor, and the dryRun flag.
 *   8. previewSignal_emittedWithVictims_suppressedWhenOff - the live preview
 *      contract: with the checkbox on, refreshPreview() emits
 *      previewSelectionRequested with exactly the dry run's victims; with it
 *      off nothing is emitted, while the explicit "Preview as selection"
 *      button still works. The button is enabled ONLY while live preview is
 *      off - with the stream running it would re-emit an identical selection,
 *      i.e. look active while doing nothing (which is how it read in
 *      testing). Apply must stay unaffected by that gating.
 *
 * Strategy
 * --------
 * ODR-shim harness, same as test_auto_fit_voice_load / test_tempo_conversion
 * _service (see those headers for the full rationale), because the dialog
 * needs a WORKING MidiFile (tracks(), protocol(), channels) and the real
 * service behind it:
 *   - real .cpps: AutoFitVoiceLoadDialog + AutoFitVoiceLoadService +
 *     FfxivVoiceAnalyzer (+ FfxivVoiceLoadCore) + MidiChannel +
 *     ChannelVisibilityManager + NoteOnEvent / OffEvent / OnEvent /
 *     ProgChangeEvent + the protocol/* TUs. The analyzer is linked rather
 *     than shimmed because the dialog calls the real singleton
 *     (instance()->isEnabled()) for its wording and faking a Q_OBJECT
 *     singleton costs more than linking two dependency-free .cpps.
 *   - shimmed: MidiFile (linear 1:1 tick<->ms model + a mutable track list
 *     so the structure guard can be provoked), MidiTrack, the MidiEvent
 *     base, GraphicObject, EventWidget, Appearance.
 *
 * The MidiFile shim keeps the LINEAR time model of test_auto_fit_voice_load
 * (timeMS(t) == t, tick(ms) == ms) so the rate pass's 1-second density
 * windows are exact: a 30-note run 8 ticks apart is one fully hot window,
 * quota = notes * percent / 100.
 *
 * NOT covered here: the FFXIV-vs-generic wording branch and the per-track
 * eye buttons. Both would need FfxivVoiceAnalyzer.h / real Appearance in
 * THIS TU, and that header holds a QPointer<MidiFile>, which requires the
 * real QObject-derived MidiFile the shim harness deliberately replaces.
 * The wording is cosmetic; the eye is a thin wrapper around
 * MidiTrack::setHidden, which test_midi_track already covers.
 */

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QMultiMap>
#include <QObject>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>

// ---- Forward decls used by ODR shims ------------------------------------
class MidiTrack;
class MidiFile;
class Protocol;
class MidiChannel;
class MidiEvent;
class TextEvent;

// ---- ODR shims: Appearance ----------------------------------------------
// channelColor is referenced by MidiChannel.cpp, adjustIconForDarkMode by the
// dialog's window icon. Linking the real Appearance.cpp would drag in the
// whole settings / font / theme stack for two calls.
#include <QColor>
class Appearance {
public:
    static QColor *channelColor(int n);
    static QIcon adjustIconForDarkMode(const QString &iconPath);
};
QColor *Appearance::channelColor(int /*n*/) {
    static QColor c(Qt::black);
    return &c;
}
QIcon Appearance::adjustIconForDarkMode(const QString & /*iconPath*/) {
    return QIcon();
}

// (real ChannelVisibilityManager.cpp is linked into the test binary.)

// ---- ODR shim: MidiFile -------------------------------------------------
// Backs the service (timeMS / tick / endTick / channel / protocol / track)
// AND the dialog (tracks / protocol). Linear 1:1 time model. The track list
// is a real, MUTABLE QList so the structure guard can be provoked by adding,
// removing and reordering tracks behind the open dialog.
#include "../src/protocol/Protocol.h"
class MidiFile {
public:
    MidiFile();
    ~MidiFile();

    Protocol *protocol();
    MidiChannel *channel(int i);
    QMultiMap<int, MidiEvent *> *channelEvents(int channel);
    QList<MidiTrack *> *tracks();
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
    MidiTrack *takeTrackAt(int i) { return _tracks.takeAt(i); }
    void swapTracksAt(int a, int b) { _tracks.swapItemsAt(a, b); }

private:
    Protocol *_protocol;
    MidiChannel *_channels[19];
    QList<MidiTrack *> _tracks;
    int _endTick;
};

// ---- ODR shim: MidiTrack ------------------------------------------------
// number/name/hidden/setHidden are all called from TUs compiled against the
// REAL MidiTrack.h (dialog + analyzer), so they must be out-of-line with the
// real signatures - hidden() is NOT const in production.
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
    bool hidden();
    void setHidden(bool hidden);

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
bool MidiTrack::hidden() { return _hidden; }
void MidiTrack::setHidden(bool hidden) { _hidden = hidden; }

// ---- ODR shim: GraphicObject / EventWidget ------------------------------
#include "../src/gui/GraphicObject.h"
GraphicObject::GraphicObject() {}
void GraphicObject::draw(QPainter *, QColor) {}
bool GraphicObject::shown() { return false; }

#include "../src/gui/EventWidget.h"
void EventWidget::setEvents(QList<MidiEvent *>) {}
void EventWidget::reload() {}

// ---- ODR shim: MidiEvent base -------------------------------------------
// setMidiTime re-keys events in their channel's QMultiMap so the channel map
// is a faithful event store (same as the tempo-conversion harness).
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
#include "../src/gui/AutoFitVoiceLoadDialog.h"

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
QList<MidiTrack *> *MidiFile::tracks() { return &_tracks; }
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
    QList<MidiTrack *> tracks;   ///< parallel to *file->tracks()
    QList<MidiTrack *> detached; ///< tracks pulled OUT of the file (owned here)

    explicit ScopedFile(int trackCount = 3) {
        file = new MidiFile();
        protocol = new Protocol(file);
        file->setProtocol(protocol);
        for (int i = 0; i < trackCount; ++i) {
            MidiTrack *t = new MidiTrack(i);
            file->addTrack(t);
            tracks.append(t);
        }
    }
    ~ScopedFile() {
        delete file;
        delete protocol;
        qDeleteAll(detached);
    }

    NoteOnEvent *addNote(int channel, int tick, int durationTicks, int pitch,
                         int velocity, MidiTrack *trk) {
        NoteOnEvent *on = new NoteOnEvent(pitch, velocity, channel, trk);
        OffEvent *off = new OffEvent(channel, 127 - pitch, trk);
        on->setFile(file);
        off->setFile(file);
        on->setMidiTime(tick, false);
        off->setMidiTime(tick + durationTicks, false);
        return on;
    }

    /// A run dense enough for the rate pass: `count` staccato notes 8 ticks
    /// apart, all inside ONE 1-second window of the linear 1:1 time model,
    /// so the whole run is "hot" and the quota is count * percent / 100.
    /// Raw concurrency stays 1, so the voice-ceiling pass stays idle.
    QList<NoteOnEvent *> addDenseRun(MidiTrack *trk, int channel,
                                     int startTick, int count = 30) {
        QList<NoteOnEvent *> notes;
        for (int i = 0; i < count; ++i) {
            notes.append(addNote(channel, startTick + 8 * i, 4, 60, 100, trk));
        }
        return notes;
    }
};

/// One dense run per track, runs spaced far apart so they never overlap.
void fillOneRunPerTrack(ScopedFile &f) {
    for (int i = 0; i < f.tracks.size(); ++i) {
        f.addDenseRun(f.tracks[i], i, 1000 + 4000 * i);
    }
}

QPushButton *findButton(AutoFitVoiceLoadDialog *dlg, const QString &text) {
    for (QPushButton *b : dlg->findChildren<QPushButton *>()) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

} // namespace

// =========================================================================
// The dialog declares this class a friend so the tests can assert on the
// PRIVATE option mapping (currentOptions) and drive the private widgets
// directly - see the note in AutoFitVoiceLoadDialog.h.
class TestAutoFitDialog : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void trackFilter_threeWayMapping();
    void selectionScope_preChecksOnlyScopedTracks();
    void selectionScope_empty_checksEveryTrack();
    void perTrackPercents_surviveCheckCycles();
    void structureGuard_closesOnTrackCountChange();
    void structureGuard_closesOnTrackReorder();
    void structureGuard_keepsOpenWhenUnchanged();
    void advancedOptions_reachOptions();
    void previewSignal_emittedWithVictims_suppressedWhenOff();
};

// -------------------------------------------------------------------------
void TestAutoFitDialog::initTestCase() {
    // previewSelectionRequested carries a QList<MidiEvent *>; register it so
    // QSignalSpy can copy the payload out of the emission.
    qRegisterMetaType<QList<MidiEvent *>>();
}

// -------------------------------------------------------------------------
// 1. ALL checked -> EMPTY filter, SUBSET -> exactly those track numbers,
//    NONE -> the impossible sentinel {-1}. The empty filter means "all
//    tracks" to the service, so mapping NONE to an empty set would thin the
//    whole file instead of nothing.
void TestAutoFitDialog::trackFilter_threeWayMapping() {
    ScopedFile f(3);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);

    // --- ALL (the default with no selection scope) ------------------------
    QCOMPARE(dlg._trackChecks.size(), 3);
    for (QCheckBox *cb : dlg._trackChecks) QVERIFY(cb->isChecked());
    QVERIFY(dlg.currentOptions(true).trackFilter.isEmpty());

    // --- SUBSET ----------------------------------------------------------
    dlg._trackChecks[1]->setChecked(false);
    QCOMPARE(dlg.currentOptions(true).trackFilter, QSet<int>({0, 2}));
    dlg._trackChecks[0]->setChecked(false);
    QCOMPARE(dlg.currentOptions(true).trackFilter, QSet<int>({2}));

    // --- NONE, via the real "None" quick button --------------------------
    QPushButton *none = findButton(&dlg, QStringLiteral("None"));
    QVERIFY(none);
    none->click();
    QCOMPARE(dlg.currentOptions(true).trackFilter, QSet<int>({-1}));
    // The sentinel really makes the run a no-op: with nothing removable the
    // Apply / Preview buttons must be dead.
    QVERIFY(!dlg._applyButton->isEnabled());
    QVERIFY(!dlg._previewButton->isEnabled());

    // --- back to ALL, via the real "All" quick button ---------------------
    QPushButton *all = findButton(&dlg, QStringLiteral("All"));
    QVERIFY(all);
    all->click();
    for (QCheckBox *cb : dlg._trackChecks) QVERIFY(cb->isChecked());
    QVERIFY(dlg.currentOptions(true).trackFilter.isEmpty());
    QVERIFY(dlg._applyButton->isEnabled()); // removable again
}

// -------------------------------------------------------------------------
// 2. Just-fixed bug: a non-empty selectionScope must pre-check ONLY the
//    tracks that actually carry scoped notes. Checking everything would let
//    Apply thin material the user never selected.
void TestAutoFitDialog::selectionScope_preChecksOnlyScopedTracks() {
    ScopedFile f(3);
    QList<NoteOnEvent *> onTrack0 = f.addDenseRun(f.tracks[0], 0, 1000);
    QList<NoteOnEvent *> onTrack1 = f.addDenseRun(f.tracks[1], 1, 5000);
    f.addDenseRun(f.tracks[2], 2, 9000);

    QList<MidiEvent *> scope;
    for (NoteOnEvent *on : onTrack1) scope.append(static_cast<MidiEvent *>(on));

    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, scope, nullptr);

    // Only track 1's checkbox starts checked.
    QCOMPARE(dlg._trackChecks.size(), 3);
    QVERIFY(!dlg._trackChecks[0]->isChecked());
    QVERIFY(dlg._trackChecks[1]->isChecked());
    QVERIFY(!dlg._trackChecks[2]->isChecked());

    const AutoFitOptions opts = dlg.currentOptions(true);
    // ... which is a SUBSET, so the filter names exactly that track.
    QCOMPARE(opts.trackFilter, QSet<int>({1}));
    // The scope itself reaches the service as pointers (compared, never
    // dereferenced) - one entry per selected note.
    QCOMPARE(opts.selectionScope.size(), onTrack1.size());
    for (NoteOnEvent *on : onTrack1) {
        QVERIFY(opts.selectionScope.contains(reinterpret_cast<quintptr>(
            static_cast<MidiEvent *>(on))));
    }
    for (NoteOnEvent *on : onTrack0) {
        QVERIFY(!opts.selectionScope.contains(reinterpret_cast<quintptr>(
            static_cast<MidiEvent *>(on))));
    }

    // End to end: the scoped dry run can only ever hit track 1.
    const AutoFitResult r = AutoFitVoiceLoadService::apply(f.file, opts);
    QVERIFY(r.ok);
    QVERIFY(r.removedCount > 0);
    for (const AutoFitRemovedNote &rn : r.removed) QCOMPARE(rn.track, 1);
}

// -------------------------------------------------------------------------
// 3. The negatives that keep case 2 honest: an EMPTY scope checks every
//    track (and carries an empty scope set), and a scope covering ALL tracks
//    still collapses to the empty "all tracks" filter.
void TestAutoFitDialog::selectionScope_empty_checksEveryTrack() {
    ScopedFile f(3);
    QList<QList<NoteOnEvent *>> perTrack;
    for (int i = 0; i < 3; ++i) {
        perTrack.append(f.addDenseRun(f.tracks[i], i, 1000 + 4000 * i));
    }

    {
        AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
        for (QCheckBox *cb : dlg._trackChecks) QVERIFY(cb->isChecked());
        const AutoFitOptions opts = dlg.currentOptions(true);
        QVERIFY(opts.trackFilter.isEmpty());
        QVERIFY(opts.selectionScope.isEmpty());
    }
    {
        QList<MidiEvent *> scope;
        for (const QList<NoteOnEvent *> &run : perTrack) {
            for (NoteOnEvent *on : run) {
                scope.append(static_cast<MidiEvent *>(on));
            }
        }
        AutoFitVoiceLoadDialog dlg(f.file, -1, -1, scope, nullptr);
        for (QCheckBox *cb : dlg._trackChecks) QVERIFY(cb->isChecked());
        const AutoFitOptions opts = dlg.currentOptions(true);
        // All checked -> empty filter even though a scope is active; the
        // scope alone narrows the material.
        QVERIFY(opts.trackFilter.isEmpty());
        QCOMPARE(opts.selectionScope.size(), scope.size());
    }
}

// -------------------------------------------------------------------------
// 4. _trackPercents semantics: the slider edits the CHECKED tracks only, and
//    an unchecked track's stored target survives both the edit and a later
//    check/uncheck cycle. Losing it would silently re-thin a track the user
//    had tuned down.
void TestAutoFitDialog::perTrackPercents_surviveCheckCycles() {
    ScopedFile f(3);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);

    // Every track is seeded with the slider default.
    const int seeded = dlg._intensitySlider->value();
    QMap<int, int> expected{{0, seeded}, {1, seeded}, {2, seeded}};
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);

    // All checked: the slider moves every track.
    dlg._intensitySlider->setValue(40);
    expected = QMap<int, int>{{0, 40}, {1, 40}, {2, 40}};
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);

    // Uncheck track 2, then move the slider: only the CHECKED tracks follow,
    // track 2 keeps its 40.
    dlg._trackChecks[2]->setChecked(false);
    dlg._intensitySlider->setValue(70);
    expected = QMap<int, int>{{0, 70}, {1, 70}, {2, 40}};
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);

    // Re-checking track 2 must not overwrite its stored value with the
    // slider position (the checked group no longer shares one value).
    dlg._trackChecks[2]->setChecked(true);
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);
    QCOMPARE(dlg._intensitySlider->value(), 70);

    // A full check/uncheck cycle on another track loses nothing either.
    dlg._trackChecks[0]->setChecked(false);
    dlg._trackChecks[0]->setChecked(true);
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);

    // Mirror rule: when the checked group DOES share one stored value, the
    // slider shows it (and the global ratePercent follows) - here that is
    // track 2 alone with its preserved 40.
    dlg._trackChecks[0]->setChecked(false);
    dlg._trackChecks[1]->setChecked(false);
    QCOMPARE(dlg._intensitySlider->value(), 40);
    QCOMPARE(dlg.currentOptions(true).ratePercent, 40);
    QCOMPARE(dlg.currentOptions(true).ratePercentPerTrack, expected);
}

// -------------------------------------------------------------------------
// 5a. Just-fixed bug: the dialog is modeless, so the file's track list can
//     change behind it. A track add / remove renumbers the live tracks and
//     would misattribute every row, so refreshPreview() must close. This is
//     the COUNT half of the guard - split from the reorder case below so a
//     regression in either half is attributed on its own (QtTest stops a
//     test function at its first failure).
void TestAutoFitDialog::structureGuard_closesOnTrackCountChange() {
    // --- a track was ADDED ----------------------------------------------
    {
        ScopedFile f(3);
        fillOneRunPerTrack(f);
        AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
        QSignalSpy closed(&dlg, &QDialog::finished);
        dlg.show();
        QVERIFY(dlg.isVisible());

        MidiTrack *added = new MidiTrack(3);
        f.file->addTrack(added);
        dlg.refreshPreview();

        QVERIFY(!dlg.isVisible());
        QCOMPARE(closed.count(), 1);
    }
    // --- a track was REMOVED --------------------------------------------
    {
        ScopedFile f(3);
        fillOneRunPerTrack(f);
        AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
        QSignalSpy closed(&dlg, &QDialog::finished);
        dlg.show();
        QVERIFY(dlg.isVisible());

        f.detached.append(f.file->takeTrackAt(1));
        dlg.refreshPreview();

        QVERIFY(!dlg.isVisible());
        QCOMPARE(closed.count(), 1);
    }
    // --- every track gone ------------------------------------------------
    {
        ScopedFile f(2);
        fillOneRunPerTrack(f);
        AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
        dlg.show();
        QVERIFY(dlg.isVisible());

        while (!f.file->tracks()->isEmpty()) {
            f.detached.append(f.file->takeTrackAt(0));
        }
        dlg.refreshPreview();

        QVERIFY(!dlg.isVisible());
    }
}

// -------------------------------------------------------------------------
// 5b. The nastiest structure change: tracks REORDERED. The count is
//     unchanged, so only the per-index pointer-identity walk can catch it -
//     without it every row would silently keep pointing at the wrong track
//     and Apply would thin material the user never picked.
void TestAutoFitDialog::structureGuard_closesOnTrackReorder() {
    ScopedFile f(3);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
    QSignalSpy closed(&dlg, &QDialog::finished);
    dlg.show();
    QVERIFY(dlg.isVisible());

    f.file->swapTracksAt(0, 2);
    QCOMPARE(f.file->tracks()->size(), 3); // count unchanged on purpose
    dlg.refreshPreview();

    QVERIFY(!dlg.isVisible());
    QCOMPARE(closed.count(), 1);
}

// -------------------------------------------------------------------------
// 6. The negative case: an UNCHANGED track list must never close the dialog.
//    Without this, a guard that always fires would pass cases 5a / 5b while
//    making the tool unusable.
void TestAutoFitDialog::structureGuard_keepsOpenWhenUnchanged() {
    ScopedFile f(3);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
    QSignalSpy closed(&dlg, &QDialog::finished);
    dlg.show();
    QVERIFY(dlg.isVisible());

    // Repeated refreshes, plus the option changes that trigger them, all
    // leave the structure alone.
    dlg.refreshPreview();
    dlg._intensitySlider->setValue(55);
    dlg._trackChecks[1]->setChecked(false);
    dlg._ceilingSpin->setValue(20);
    dlg.refreshPreview();

    QVERIFY(dlg.isVisible());
    QCOMPARE(closed.count(), 0);

    // A protocol action on the file re-runs the dry run through the
    // actionFinished hook - still no close, still a valid preview.
    f.protocol->startNewAction(QStringLiteral("unrelated edit"));
    f.protocol->endAction();
    QVERIFY(dlg.isVisible());
    QCOMPARE(closed.count(), 0);
}

// -------------------------------------------------------------------------
// 7. Advanced options -> AutoFitOptions. The two encodings that carry real
//    risk: an unchecked chord box must send chordLimit == 0 (the service's
//    "pass off"), not the spin's value, and rateKeepOneOf is slider + 1
//    ("skip N" vs "keep 1 of N+1") - an off-by-one here doubles or halves
//    how much the rate pass removes.
void TestAutoFitDialog::advancedOptions_reachOptions() {
    ScopedFile f(2);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, 500, 9000, {}, nullptr);

    AutoFitOptions o = dlg.currentOptions(true);
    QCOMPARE(o.startTick, 500);   // the constructor's range reaches the service
    QCOMPARE(o.endTick, 9000);
    QVERIFY(o.dryRun);
    QVERIFY(!dlg.currentOptions(false).dryRun);
    QCOMPARE(o.targetCeiling, dlg._ceilingSpin->value());
    QCOMPARE(o.chordLimit, dlg._chordLimitSpin->value()); // box checked
    QVERIFY(o.desaturateRates);
    QCOMPARE(o.rateKeepOneOf, dlg._rateKeepSlider->value() + 1);
    QVERIFY(!o.preferLoudest);

    dlg._ceilingSpin->setValue(24);
    dlg._chordLimitSpin->setValue(5);
    dlg._rateKeepSlider->setValue(4);
    dlg._preferLoudestCheck->setChecked(true);
    o = dlg.currentOptions(true);
    QCOMPARE(o.targetCeiling, 24);
    QCOMPARE(o.chordLimit, 5);
    QCOMPARE(o.rateKeepOneOf, 5); // slider 4 -> keep 1 of 5
    QVERIFY(o.preferLoudest);

    // Chord pass off: limit 0, and the spin keeps (but greys out) its value
    // so re-enabling restores the user's setting.
    dlg._chordCheck->setChecked(false);
    o = dlg.currentOptions(true);
    QCOMPARE(o.chordLimit, 0);
    QCOMPARE(dlg._chordLimitSpin->value(), 5);
    QVERIFY(!dlg._chordLimitSpin->isEnabled());
    dlg._chordCheck->setChecked(true);
    QCOMPARE(dlg.currentOptions(true).chordLimit, 5);
    QVERIFY(dlg._chordLimitSpin->isEnabled());

    // Rate pass off: the flag, and both sliders go dead.
    dlg._rateCheck->setChecked(false);
    QVERIFY(!dlg.currentOptions(true).desaturateRates);
    QVERIFY(!dlg._intensitySlider->isEnabled());
    QVERIFY(!dlg._rateKeepSlider->isEnabled());
    dlg._rateCheck->setChecked(true);
    QVERIFY(dlg.currentOptions(true).desaturateRates);
}

// -------------------------------------------------------------------------
// 8. Live preview contract: the emitted list must BE the dry run's victims
//    (the editor highlights exactly what Apply would delete), and the
//    checkbox must actually suppress the stream - while the explicit
//    "Preview as selection" button keeps working with it off.
void TestAutoFitDialog::previewSignal_emittedWithVictims_suppressedWhenOff() {
    ScopedFile f(2);
    fillOneRunPerTrack(f);
    AutoFitVoiceLoadDialog dlg(f.file, -1, -1, {}, nullptr);
    dlg._intensitySlider->setValue(50); // quota 15 of each 30-note run

    QVERIFY(dlg._livePreviewCheck->isChecked()); // on by default
    QSignalSpy spy(&dlg, &AutoFitVoiceLoadDialog::previewSelectionRequested);
    QVERIFY(spy.isValid());

    const AutoFitResult expect =
        AutoFitVoiceLoadService::apply(f.file, dlg.currentOptions(true));
    QVERIFY(expect.ok);
    QVERIFY(!expect.victims.isEmpty());

    dlg.refreshPreview();
    QCOMPARE(spy.count(), 1);
    const QList<MidiEvent *> emitted =
        spy.at(0).at(0).value<QList<MidiEvent *>>();
    QCOMPARE(emitted.size(), expect.victims.size());
    QCOMPARE(QSet<MidiEvent *>(emitted.begin(), emitted.end()),
             QSet<MidiEvent *>(expect.victims.begin(), expect.victims.end()));

    // Switching the checkbox off triggers one refresh; that refresh must
    // already be silent, and so must every later one.
    spy.clear();
    dlg._livePreviewCheck->setChecked(false);
    dlg.refreshPreview();
    dlg._intensitySlider->setValue(60);
    QCOMPARE(spy.count(), 0);

    // With the stream off the button is the manual way to push the current
    // victims into the editor selection, so it must be available.
    QVERIFY(dlg._previewButton->isEnabled());
    dlg._previewButton->click();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).value<QList<MidiEvent *>>().isEmpty());

    // ... and it is greyed out again as soon as live preview takes over:
    // pressing it then would re-emit an identical selection, i.e. present an
    // active control that visibly does nothing.
    dlg._livePreviewCheck->setChecked(true);
    QVERIFY(!dlg._previewButton->isEnabled());
    QVERIFY(dlg._applyButton->isEnabled()); // Apply is NOT affected
}

QTEST_MAIN(TestAutoFitDialog)
#include "test_auto_fit_dialog.moc"
