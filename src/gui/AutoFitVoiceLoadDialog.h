/*
 * MidiEditor AI
 *
 * AutoFitVoiceLoadDialog — v2.1.0 feature #1 (roadmap Phase 35).
 *
 * Confirmation dialog for the Auto-Fit Voice Load action. Runs a DRY RUN of
 * AutoFitVoiceLoadService on open and after every option change; the
 * thinning-intensity slider recomputes live and (with live preview on)
 * highlights the would-be victims in the editor while dragging. Only the
 * explicit Apply button mutates the file (single undoable protocol step).
 */

#ifndef AUTOFITVOICELOADDIALOG_H
#define AUTOFITVOICELOADDIALOG_H

#include <QDialog>
#include <QList>
#include <QMap>

#include "../converter/AutoFitVoiceLoadService.h"

class MidiFile;
class MidiEvent;
class QLabel;
class QCheckBox;
class QSpinBox;
class QSlider;
class QComboBox;
class QPushButton;

class AutoFitVoiceLoadDialog : public QDialog {
    Q_OBJECT

public:
    /// startTick/endTick scope the fit; pass -1/-1 for the whole file.
    AutoFitVoiceLoadDialog(MidiFile *file, int startTick, int endTick,
                           QWidget *parent = nullptr);

    /// Status-bar text describing what Apply did (valid after accept()).
    QString resultSummary() const { return _resultSummary; }

signals:
    /// Emitted with the current would-be victims (live preview + button).
    void previewSelectionRequested(const QList<MidiEvent *> &events);

private slots:
    void refreshPreview();
    void onPreviewClicked();
    void onApplyClicked();

private:
    AutoFitOptions currentOptions(bool dryRun) const;

    MidiFile *_file;
    int _startTick;
    int _endTick;

    /// Track list (right column): checkbox per track for multi-selection, an
    /// eye button mirroring the Tracks panel's visibility toggle (the modal
    /// dialog blocks the panel, and hiding tracks drives the lane's
    /// track-share display), and a live stats label. Parallel lists.
    QList<QCheckBox *> _trackChecks;
    QList<class QToolButton *> _trackEyes;
    QList<QLabel *> _trackStatLabels;
    QList<int> _trackNumbers;
    QList<class MidiTrack *> _tracks;

    /// Per-track thinning threshold (notes/sec). The slider edits the
    /// CHECKED tracks; values survive check/uncheck cycles, so each track
    /// can be tuned individually and all settings apply together on Apply.
    QMap<int, int> _trackThresholds;
    bool _sliderGuard = false; ///< true while the code moves the slider

    QSpinBox *_ceilingSpin;
    QCheckBox *_chordCheck;
    QSpinBox *_chordLimitSpin;
    QCheckBox *_rateCheck;
    QSlider *_intensitySlider;
    QLabel *_intensityLabel;
    QSlider *_rateKeepSlider;
    QLabel *_rateKeepLabel;
    QCheckBox *_preferLoudestCheck;
    QCheckBox *_livePreviewCheck;
    QLabel *_summaryLabel;
    QPushButton *_previewButton;
    QPushButton *_applyButton;

    AutoFitResult _lastDry;
    QString _resultSummary;
};

#endif // AUTOFITVOICELOADDIALOG_H
