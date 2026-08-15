#include "FfxivPlayabilityDialog.h"

#include "../MidiEvent/NoteOnEvent.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

// Child items carry their issue's index in the report so a click can look
// up the events without duplicating the list into the item.
constexpr int kIssueIndexRole = Qt::UserRole;

QString groupTitle(FfxivPlayabilityIssue::Type t, int count) {
    switch (t) {
    case FfxivPlayabilityIssue::Type::Overlap:
        return FfxivPlayabilityDialog::tr("Simultaneous notes (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::DuplicateNote:
        return FfxivPlayabilityDialog::tr("Stacked duplicates (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::OutOfRange:
        return FfxivPlayabilityDialog::tr("Notes outside C3-C6 (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::VoiceCeiling:
        return FfxivPlayabilityDialog::tr("Voice limit (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::NoteRate:
        return FfxivPlayabilityDialog::tr("Note-rate hotspots (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::ChannelSpread:
        return FfxivPlayabilityDialog::tr("Channel spread - editor playback only (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::TrackName:
        return FfxivPlayabilityDialog::tr("Track names (%1)").arg(count);
    case FfxivPlayabilityIssue::Type::EmptyTrack:
        return FfxivPlayabilityDialog::tr("Empty instrument tracks (%1)").arg(count);
    }
    return QString();
}

} // namespace

FfxivPlayabilityDialog::FfxivPlayabilityDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("FFXIV Playability Check"));
    setMinimumSize(640, 520);

    auto *layout = new QVBoxLayout(this);

    // --- Check row: every category is individually re-runnable ------------
    auto *checksRow = new QHBoxLayout();
    _checkSimultaneous = new QCheckBox(tr("Chords"), this);
    _checkSimultaneous->setToolTip(tr("Several different pitches starting on the same "
                                      "tick on one performer. In game a performer plays "
                                      "one note at a time, so these play as a fast "
                                      "arpeggio. Often intentional - switch this off to "
                                      "hide them and look for real defects."));
    _checkDuplicates = new QCheckBox(tr("Stacked notes"), this);
    _checkDuplicates->setToolTip(tr("The same pitch starting twice on the same tick on "
                                    "one performer. Almost always a MIDI defect (a "
                                    "doubled note) - the game plays it once and the "
                                    "duplicate is wasted."));
    _checkRange = new QCheckBox(tr("Range"), this);
    _checkRange->setToolTip(tr("Notes outside C3-C6"));
    _checkNames = new QCheckBox(tr("Track names"), this);
    _checkNames->setToolTip(tr("Names matching no FFXIV instrument (in game the "
                               "track name selects the instrument)"));
    _checkChannels = new QCheckBox(tr("Channels"), this);
    _checkChannels->setToolTip(tr("Non-guitar tracks with notes spread over several "
                                  "channels - affects editor playback only"));
    _checkEmpty = new QCheckBox(tr("Empty tracks"), this);
    _checkEmpty->setToolTip(tr("Instrument-named tracks without any notes"));
    _checkVoiceLoad = new QCheckBox(tr("Voice limit"), this);
    _checkVoiceLoad->setToolTip(tr("Raw voice peak vs the 16-voice ceiling and "
                                   "notes/sec hotspots"));
    for (QCheckBox *cb : {_checkSimultaneous, _checkDuplicates, _checkRange,
                          _checkNames, _checkChannels, _checkEmpty,
                          _checkVoiceLoad}) {
        cb->setChecked(true);
        checksRow->addWidget(cb);
    }
    checksRow->addStretch();
    auto *runButton = new QPushButton(tr("Run checks"), this);
    connect(runButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::runChecksRequested);
    checksRow->addWidget(runButton);
    layout->addLayout(checksRow);

    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    layout->addWidget(_summaryLabel);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(1);
    _tree->setHeaderHidden(true);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(_tree, &QTreeWidget::itemClicked,
            this, &FfxivPlayabilityDialog::onItemClicked);
    connect(_tree, &QTreeWidget::itemDoubleClicked,
            this, &FfxivPlayabilityDialog::onItemDoubleClicked);
    layout->addWidget(_tree, 1);

    // --- AI analysis pane (hidden until an answer arrives) -----------------
    _analysisView = new QTextBrowser(this);
    _analysisView->setOpenExternalLinks(false);
    _analysisView->setMaximumHeight(180);
    _analysisView->hide();
    layout->addWidget(_analysisView);

    auto *hint = new QLabel(
        tr("Click an issue to select its notes and move the cursor there; "
           "double-click to jump to the spot with only the affected track "
           "shown (the previous visibility returns when this window closes). "
           "The buttons below offer the matching repair for what was found."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // --- Fix row: contextual repairs + AI + close --------------------------
    auto *buttons = new QHBoxLayout();
    _selectAllButton = new QPushButton(tr("Select all offending notes"), this);
    connect(_selectAllButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onSelectAllClicked);
    buttons->addWidget(_selectAllButton);

    _fixOverlapsButton = new QPushButton(tr("Delete colliding notes"), this);
    _fixOverlapsButton->setToolTip(tr("Deletes the surplus notes of every collision "
                                      "in one undo step: duplicates keep one copy, "
                                      "simultaneous chords keep the highest note"));
    connect(_fixOverlapsButton, &QPushButton::clicked, this, [this]() {
        // The workbench knows exactly which notes are surplus - no mode
        // dialog (second QA: the button must DELETE, not just mark).
        // Survivor per collision group: highest pitch (the melody rule
        // Auto-Fit documents), among equal pitches the louder note. A note
        // may survive one issue and be a victim of another (C+C+E: the
        // chord keeps E, the duplicate pair contributes both Cs) - the
        // victim UNION handles that correctly.
        QList<MidiEvent *> victims;
        QSet<MidiEvent *> seen;
        for (const FfxivPlayabilityIssue &i : _report.issues) {
            if (i.type != FfxivPlayabilityIssue::Type::Overlap
                && i.type != FfxivPlayabilityIssue::Type::DuplicateNote)
                continue;
            NoteOnEvent *survivor = nullptr;
            for (MidiEvent *ev : i.events) {
                auto *on = dynamic_cast<NoteOnEvent *>(ev);
                if (!on) continue;
                if (!survivor || on->note() > survivor->note()
                    || (on->note() == survivor->note()
                        && on->velocity() > survivor->velocity())) {
                    survivor = on;
                }
            }
            for (MidiEvent *ev : i.events) {
                if (ev && ev != survivor && !seen.contains(ev)) {
                    seen.insert(ev);
                    victims.append(ev);
                }
            }
        }
        if (!victims.isEmpty())
            emit deleteEventsRequested(victims);
    });
    buttons->addWidget(_fixOverlapsButton);

    _fixChannelsButton = new QPushButton(tr("Channel Fixer"), this);
    _fixChannelsButton->setToolTip(tr("Runs Fix X|V Channels - repairs channel "
                                      "spread and tidies track naming/programs"));
    connect(_fixChannelsButton, &QPushButton::clicked, this, [this]() {
        emit fixRequested(QStringLiteral("fix_ffxiv_channels"));
    });
    buttons->addWidget(_fixChannelsButton);

    _fixVoiceButton = new QPushButton(tr("Auto-Fit..."), this);
    _fixVoiceButton->setToolTip(tr("Opens Auto-Fit Voice Load to thin the "
                                   "overloaded passages"));
    connect(_fixVoiceButton, &QPushButton::clicked, this, [this]() {
        emit fixRequested(QStringLiteral("auto_fit_voice_load"));
    });
    buttons->addWidget(_fixVoiceButton);

    buttons->addStretch();

    _analyzeButton = new QPushButton(tr("Analyze with MidiPilot"), this);
    _analyzeButton->setToolTip(tr("Sends the findings to MidiPilot and shows its "
                                  "assessment here"));
    connect(_analyzeButton, &QPushButton::clicked,
            this, &FfxivPlayabilityDialog::onAnalyzeClicked);
    buttons->addWidget(_analyzeButton);

    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    rebuildTree();
}

FfxivPlayabilityChecks FfxivPlayabilityDialog::selectedChecks() const {
    FfxivPlayabilityChecks c;
    c.simultaneousNotes = _checkSimultaneous->isChecked();
    c.stackedDuplicates = _checkDuplicates->isChecked();
    c.range = _checkRange->isChecked();
    c.trackNames = _checkNames->isChecked();
    c.channelSpread = _checkChannels->isChecked();
    c.emptyTracks = _checkEmpty->isChecked();
    return c;
}

bool FfxivPlayabilityDialog::voiceLoadEnabled() const {
    return _checkVoiceLoad->isChecked();
}

void FfxivPlayabilityDialog::refresh(const FfxivPlayabilityReport &report) {
    _report = report;
    rebuildTree();
}

void FfxivPlayabilityDialog::showAnalysis(const QString &text) {
    _awaitingAnalysis = false;
    _analyzeButton->setEnabled(_midiPilotAvailable);
    _analyzeButton->setText(tr("Analyze with MidiPilot"));
    _analysisView->setMarkdown(text);
    _analysisView->show();
}

void FfxivPlayabilityDialog::setMidiPilotAvailable(bool available) {
    _midiPilotAvailable = available;
    _analyzeButton->setVisible(available);
    _analyzeButton->setEnabled(available && !_awaitingAnalysis);
}

void FfxivPlayabilityDialog::rebuildTree() {
    _tree->clear();

    const int problemCount = _report.issues.size();
    if (_report.valid()) {
        _summaryLabel->setText(
            tr("<b>File is FFXIV-playable.</b> %1 note(s) on %2 track(s) "
               "checked with the selected checks - nothing found.")
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    } else {
        _summaryLabel->setText(
            tr("<b>%1 finding(s)</b> in %2 note(s) on %3 track(s). "
               "A performer plays one note at a time: chords are rolled as "
               "a fast arpeggio, stacked notes waste the duplicate.")
                .arg(problemCount)
                .arg(_report.checkedNotes)
                .arg(_report.checkedTracks));
    }

    // Fixed presentation order: what breaks playback first.
    const FfxivPlayabilityIssue::Type order[] = {
        FfxivPlayabilityIssue::Type::Overlap,
        FfxivPlayabilityIssue::Type::DuplicateNote,
        FfxivPlayabilityIssue::Type::VoiceCeiling,
        FfxivPlayabilityIssue::Type::NoteRate,
        FfxivPlayabilityIssue::Type::OutOfRange,
        FfxivPlayabilityIssue::Type::TrackName,
        FfxivPlayabilityIssue::Type::ChannelSpread,
        FfxivPlayabilityIssue::Type::EmptyTrack,
    };
    for (FfxivPlayabilityIssue::Type type : order) {
        const int count = _report.countOf(type);
        if (count == 0) continue;
        auto *group = new QTreeWidgetItem(_tree);
        group->setText(0, groupTitle(type, count));
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        for (int idx = 0; idx < _report.issues.size(); ++idx) {
            const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
            if (issue.type != type) continue;
            auto *item = new QTreeWidgetItem(group);
            // File-level findings (voice limit, rate hotspots) carry track -1.
            item->setText(0, issue.track >= 0
                                 ? tr("Track %1: %2").arg(issue.track)
                                                     .arg(issue.details)
                                 : issue.details);
            item->setData(0, kIssueIndexRole, idx);
        }
        group->setExpanded(count <= 20);
    }

    rebuildFixRow();
}

void FfxivPlayabilityDialog::rebuildFixRow() {
    // Offer only the repairs matching what was FOUND - a wall of disabled
    // buttons is a console, contextual tools are a workbench.
    const bool hasCollisions =
        _report.countOf(FfxivPlayabilityIssue::Type::Overlap) > 0
        || _report.countOf(FfxivPlayabilityIssue::Type::DuplicateNote) > 0;
    const bool hasChannelIssues =
        _report.countOf(FfxivPlayabilityIssue::Type::ChannelSpread) > 0
        || _report.countOf(FfxivPlayabilityIssue::Type::TrackName) > 0;
    const bool hasVoiceIssues =
        _report.countOf(FfxivPlayabilityIssue::Type::VoiceCeiling) > 0
        || _report.countOf(FfxivPlayabilityIssue::Type::NoteRate) > 0;

    _fixOverlapsButton->setVisible(hasCollisions);
    _fixChannelsButton->setVisible(hasChannelIssues);
    _fixVoiceButton->setVisible(hasVoiceIssues);
    _selectAllButton->setEnabled(!_report.offendingNotes().isEmpty());
}

QString FfxivPlayabilityDialog::buildAnalysisPrompt() const {
    QStringList lines;
    lines << QStringLiteral(
        "Please assess this FFXIV playability report for the current file "
        "and give a short, prioritized verdict: what MUST be fixed before "
        "playing in game, what is cosmetic, and in which order you would "
        "fix it (mention the matching tools). Be concise.");
    lines << QStringLiteral("Report: %1 finding(s), %2 notes on %3 tracks checked.")
                 .arg(_report.issues.size())
                 .arg(_report.checkedNotes)
                 .arg(_report.checkedTracks);
    // Compact findings list, capped so the prompt stays small - the agent
    // can always run validate_ffxiv itself for the full picture.
    const int cap = 40;
    for (int i = 0; i < _report.issues.size() && i < cap; ++i) {
        const FfxivPlayabilityIssue &issue = _report.issues.at(i);
        lines << QStringLiteral("- Track %1: %2").arg(issue.track)
                                                 .arg(issue.details);
    }
    if (_report.issues.size() > cap)
        lines << QStringLiteral("(%1 more findings omitted)")
                     .arg(_report.issues.size() - cap);
    return lines.join(QStringLiteral("\n"));
}

void FfxivPlayabilityDialog::onAnalyzeClicked() {
    if (!_midiPilotAvailable || _awaitingAnalysis) return;
    _awaitingAnalysis = true;
    _analyzeButton->setEnabled(false);
    _analyzeButton->setText(tr("Waiting for MidiPilot..."));
    _analysisView->setMarkdown(tr("*Asking MidiPilot...*"));
    _analysisView->show();
    emit analyzeRequested(buildAnalysisPrompt());
}

void FfxivPlayabilityDialog::onItemClicked(QTreeWidgetItem *item, int) {
    if (!item) return;
    const QVariant idxVar = item->data(0, kIssueIndexRole);
    if (!idxVar.isValid()) return;
    const int idx = idxVar.toInt();
    if (idx < 0 || idx >= _report.issues.size()) return;
    const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
    if (!issue.events.isEmpty())
        emit selectEventsRequested(issue.events);
    emit jumpToTickRequested(issue.tick);
}

void FfxivPlayabilityDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column) {
    // A double-click always arrives after the single-click handler already
    // selected the notes and set the cursor - add view scroll + track focus.
    onItemClicked(item, column);
    if (!item) return;
    const QVariant idxVar = item->data(0, kIssueIndexRole);
    if (!idxVar.isValid()) return;
    const int idx = idxVar.toInt();
    if (idx < 0 || idx >= _report.issues.size()) return;
    const FfxivPlayabilityIssue &issue = _report.issues.at(idx);
    if (issue.track >= 0)
        emit focusTrackRequested(issue.track);
    emit revealTickRequested(issue.tick);
}

void FfxivPlayabilityDialog::onSelectAllClicked() {
    const QList<MidiEvent *> events = _report.offendingNotes();
    if (!events.isEmpty())
        emit selectEventsRequested(events);
}
