#include "AutoFitVoiceLoadDialog.h"
#include "Appearance.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
// Slider position (0..26, right = stronger thinning) <-> sustained density
// threshold (30..4 notes/sec over a 1-second window). The low end reaches
// steady cymbal walls (~6.7 notes/sec at 200 BPM eighths).
int thresholdForSlider(int sliderValue) { return 30 - sliderValue; }
} // namespace

AutoFitVoiceLoadDialog::AutoFitVoiceLoadDialog(MidiFile *file, int startTick,
                                               int endTick, QWidget *parent)
    : QDialog(parent),
      _file(file),
      _startTick(startTick),
      _endTick(endTick),
      _trackScopeCombo(nullptr),
      _ceilingSpin(nullptr),
      _chordCheck(nullptr),
      _chordLimitSpin(nullptr),
      _rateCheck(nullptr),
      _intensitySlider(nullptr),
      _intensityLabel(nullptr),
      _rateKeepCombo(nullptr),
      _preferLoudestCheck(nullptr),
      _livePreviewCheck(nullptr),
      _summaryLabel(nullptr),
      _previewButton(nullptr),
      _applyButton(nullptr) {

    setWindowTitle(tr("Auto-Fit Voice Load"));
    setModal(true);
    setMinimumWidth(520);
    setWindowIcon(Appearance::adjustIconForDarkMode(":/run_environment/graphics/icon.png"));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *info = new QLabel(
        (_startTick >= 0 || _endTick >= 0)
            ? tr("Thin the selected range so it fits the FFXIV mixer limits. "
                 "Voices count notes that really sound at the same time.")
            : tr("Thin the song so it fits the FFXIV mixer limits. "
                 "Voices count notes that really sound at the same time."),
        this);
    info->setWordWrap(true);
    mainLayout->addWidget(info);

    // --- Track scope -------------------------------------------------------
    // Notes outside the scope still COUNT toward voices and density, they
    // just never become victims - so a single overloaded track (e.g. the
    // Cymbal track) can be thinned without touching the rest.
    QHBoxLayout *scopeRow = new QHBoxLayout();
    scopeRow->addWidget(new QLabel(tr("Apply to:"), this));
    _trackScopeCombo = new QComboBox(this);
    _trackScopeCombo->addItem(tr("All tracks"), -1);
    _trackScopeCombo->addItem(tr("Visible tracks only"), -2);
    if (_file && _file->tracks()) {
        for (MidiTrack *t : *_file->tracks()) {
            if (!t) continue;
            const QString name = t->name().isEmpty()
                ? tr("(unnamed)") : t->name();
            _trackScopeCombo->addItem(
                tr("Track %1: %2").arg(t->number()).arg(name), t->number());
        }
    }
    _trackScopeCombo->setCurrentIndex(0);
    scopeRow->addWidget(_trackScopeCombo, 1);
    mainLayout->addLayout(scopeRow);

    // --- Density desaturation (the main control) ---------------------------
    QGroupBox *rateBox = new QGroupBox(tr("Density desaturation (per track)"), this);
    QVBoxLayout *rateLayout = new QVBoxLayout(rateBox);

    _rateCheck = new QCheckBox(tr("Thin passages that are too dense for one FFXIV performer"), this);
    _rateCheck->setChecked(true);
    rateLayout->addWidget(_rateCheck);

    QHBoxLayout *sliderRow = new QHBoxLayout();
    sliderRow->addWidget(new QLabel(tr("mild"), this));
    _intensitySlider = new QSlider(Qt::Horizontal, this);
    _intensitySlider->setRange(0, 26);          // threshold 30 .. 4 notes/sec
    _intensitySlider->setValue(14);             // threshold 16 notes/sec
    _intensitySlider->setTracking(true);        // live recompute while dragging
    sliderRow->addWidget(_intensitySlider, 1);
    sliderRow->addWidget(new QLabel(tr("aggressive"), this));
    rateLayout->addLayout(sliderRow);

    _intensityLabel = new QLabel(this);
    _intensityLabel->setStyleSheet("QLabel { color: gray; }");
    rateLayout->addWidget(_intensityLabel);

    QHBoxLayout *keepRow = new QHBoxLayout();
    keepRow->addWidget(new QLabel(tr("In dense runs:"), this));
    _rateKeepCombo = new QComboBox(this);
    _rateKeepCombo->addItem(tr("keep every 2nd note (halve)"));
    _rateKeepCombo->addItem(tr("keep every 3rd note (third)"));
    _rateKeepCombo->addItem(tr("keep every 4th note"));
    _rateKeepCombo->setCurrentIndex(0);
    keepRow->addWidget(_rateKeepCombo);
    keepRow->addStretch();
    rateLayout->addLayout(keepRow);

    QLabel *rateHint = new QLabel(
        tr("The higher voice of each group survives (in two-voice passages the "
           "upper voice carries the melody)."), this);
    rateHint->setWordWrap(true);
    rateHint->setStyleSheet("QLabel { color: gray; font-size: 10px; }");
    rateLayout->addWidget(rateHint);

    mainLayout->addWidget(rateBox);

    // --- Advanced options --------------------------------------------------
    QGroupBox *optionsBox = new QGroupBox(tr("Advanced"), this);
    QVBoxLayout *optionsLayout = new QVBoxLayout(optionsBox);

    QHBoxLayout *ceilingRow = new QHBoxLayout();
    ceilingRow->addWidget(new QLabel(tr("Voice ceiling:"), this));
    _ceilingSpin = new QSpinBox(this);
    _ceilingSpin->setRange(8, 32);
    _ceilingSpin->setValue(16);
    _ceilingSpin->setToolTip(tr("Maximum simultaneously sounding notes. 16 is the "
                                "documented in-game limit."));
    ceilingRow->addWidget(_ceilingSpin);
    ceilingRow->addWidget(new QLabel(tr("simultaneous voices"), this));
    ceilingRow->addStretch();
    optionsLayout->addLayout(ceilingRow);

    QHBoxLayout *chordRow = new QHBoxLayout();
    _chordCheck = new QCheckBox(tr("Limit chords to"), this);
    _chordCheck->setChecked(true);
    _chordLimitSpin = new QSpinBox(this);
    _chordLimitSpin->setRange(2, 8);
    _chordLimitSpin->setValue(3);
    QLabel *chordSuffix = new QLabel(tr("voices per channel (only in overflows)"), this);
    chordRow->addWidget(_chordCheck);
    chordRow->addWidget(_chordLimitSpin);
    chordRow->addWidget(chordSuffix);
    chordRow->addStretch();
    optionsLayout->addLayout(chordRow);

    _preferLoudestCheck = new QCheckBox(
        tr("Prefer louder notes (accents) - for files WITHOUT normalized velocity"), this);
    _preferLoudestCheck->setChecked(false);
    _preferLoudestCheck->setToolTip(
        tr("After Fix X|V Channels all velocities are 100, so loudness carries no "
           "information and the higher voice wins. Enable this only for raw files "
           "where accents should survive the thinning."));
    optionsLayout->addWidget(_preferLoudestCheck);

    _livePreviewCheck = new QCheckBox(
        tr("Live preview: highlight affected notes in the editor"), this);
    _livePreviewCheck->setChecked(true);
    optionsLayout->addWidget(_livePreviewCheck);

    mainLayout->addWidget(optionsBox);

    // --- Dry-run summary ---------------------------------------------------
    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    _summaryLabel->setStyleSheet("QLabel { background-color: #404040; color: white; padding: 8px; }");
    mainLayout->addWidget(_summaryLabel);

    // --- Buttons -----------------------------------------------------------
    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    _previewButton = buttons->addButton(tr("Preview as selection"),
                                        QDialogButtonBox::ActionRole);
    _applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    Q_UNUSED(cancel);
    connect(_previewButton, &QPushButton::clicked,
            this, &AutoFitVoiceLoadDialog::onPreviewClicked);
    connect(_applyButton, &QPushButton::clicked,
            this, &AutoFitVoiceLoadDialog::onApplyClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(_trackScopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_intensitySlider, &QSlider::valueChanged,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_ceilingSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_chordCheck, &QCheckBox::toggled, _chordLimitSpin, &QSpinBox::setEnabled);
    connect(_chordCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_chordLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_rateCheck, &QCheckBox::toggled, _intensitySlider, &QSlider::setEnabled);
    connect(_rateCheck, &QCheckBox::toggled, _rateKeepCombo, &QComboBox::setEnabled);
    connect(_rateCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_rateKeepCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_preferLoudestCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);
    connect(_livePreviewCheck, &QCheckBox::toggled,
            this, &AutoFitVoiceLoadDialog::refreshPreview);

    setLayout(mainLayout);
    refreshPreview();
}

AutoFitOptions AutoFitVoiceLoadDialog::currentOptions(bool dryRun) const {
    AutoFitOptions opts;
    opts.startTick = _startTick;
    opts.endTick = _endTick;
    const int scope = _trackScopeCombo->currentData().toInt();
    if (scope == -2) {
        // Visible tracks only.
        if (_file && _file->tracks()) {
            for (MidiTrack *t : *_file->tracks()) {
                if (t && !t->hidden()) opts.trackFilter.insert(t->number());
            }
        }
    } else if (scope >= 0) {
        opts.trackFilter.insert(scope);
    } // -1 = all tracks -> empty filter
    opts.targetCeiling = _ceilingSpin->value();
    opts.chordLimit = _chordCheck->isChecked() ? _chordLimitSpin->value() : 0;
    opts.desaturateRates = _rateCheck->isChecked();
    opts.rateThresholdPerSec = thresholdForSlider(_intensitySlider->value());
    opts.rateKeepOneOf = _rateKeepCombo->currentIndex() + 2;
    opts.preferLoudest = _preferLoudestCheck->isChecked();
    opts.dryRun = dryRun;
    return opts;
}

void AutoFitVoiceLoadDialog::refreshPreview() {
    _lastDry = AutoFitVoiceLoadService::apply(_file, currentOptions(true));
    if (!_lastDry.ok) {
        _summaryLabel->setText(tr("Analysis failed: %1").arg(_lastDry.error));
        _previewButton->setEnabled(false);
        _applyButton->setEnabled(false);
        return;
    }

    const double pct = (_lastDry.totalNotesInScope > 0)
        ? (100.0 * _lastDry.removedCount / _lastDry.totalNotesInScope) : 0.0;
    _intensityLabel->setText(
        tr("Threshold: %1 notes/sec per track - removes %2% of the notes")
            .arg(thresholdForSlider(_intensitySlider->value()))
            .arg(QString::number(pct, 'f', 1)));

    QString text;
    text += tr("Peak %1/%2 sounding voices, %3 overflow range(s).")
                .arg(_lastDry.peakBefore)
                .arg(_ceilingSpin->value())
                .arg(_lastDry.overflowRangeCount);
    text += QStringLiteral("\n");
    if (_lastDry.removedCount == 0) {
        text += tr("Nothing to remove - the range already fits.");
    } else {
        text += tr("Will remove %1 of %2 notes (%3%): %4 duplicates, %5 chord limit, "
                   "%6 voice ceiling, %7 note rate.")
                    .arg(_lastDry.removedCount)
                    .arg(_lastDry.totalNotesInScope)
                    .arg(QString::number(pct, 'f', 1))
                    .arg(_lastDry.duplicateRemoved)
                    .arg(_lastDry.chordRemoved)
                    .arg(_lastDry.ceilingRemoved)
                    .arg(_lastDry.rateRemoved);
        for (const AutoFitTrackSummary &s : _lastDry.trackSummaries) {
            const double tp = s.notes > 0 ? 100.0 * s.removed / s.notes : 0.0;
            text += QStringLiteral("\n");
            text += tr("    Track %1 %2: -%3 of %4 (%5%)")
                        .arg(s.track)
                        .arg(s.name.isEmpty() ? QStringLiteral("(unnamed)") : s.name)
                        .arg(s.removed)
                        .arg(s.notes)
                        .arg(QString::number(tp, 'f', 1));
        }
        text += QStringLiteral("\n");
        text += tr("Peak afterwards: %1. One Ctrl+Z restores everything.")
                    .arg(_lastDry.remainingPeak);
    }
    _summaryLabel->setText(text);
    _previewButton->setEnabled(_lastDry.removedCount > 0);
    _applyButton->setEnabled(_lastDry.removedCount > 0);

    // Live preview: mirror the current victim set into the editor selection
    // while the user drags the slider (empty set clears the highlight).
    if (_livePreviewCheck->isChecked()) {
        emit previewSelectionRequested(_lastDry.victims);
    }
}

void AutoFitVoiceLoadDialog::onPreviewClicked() {
    if (_lastDry.ok) {
        emit previewSelectionRequested(_lastDry.victims);
    }
}

void AutoFitVoiceLoadDialog::onApplyClicked() {
    const AutoFitResult r = AutoFitVoiceLoadService::apply(_file, currentOptions(false));
    if (!r.ok) {
        _summaryLabel->setText(tr("Apply failed: %1").arg(r.error));
        return;
    }
    _resultSummary = tr("Auto-fit voice load: removed %1 note(s), peak %2 -> %3 voices")
                         .arg(r.removedCount)
                         .arg(r.peakBefore)
                         .arg(r.remainingPeak);
    accept();
}
