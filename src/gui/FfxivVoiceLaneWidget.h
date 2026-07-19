/*
 * MidiEditor — FFXIV Voice Load Lane (Phase 32.3)
 *
 * Read-only graph beneath the velocity lane that visualises the simultaneous
 * voice count vs the FFXIV 16-voice ceiling. Shares horizontal scroll and
 * zoom with MatrixWidget the same way LyricTimelineWidget does.
 */

#ifndef FFXIVVOICELANEWIDGET_H
#define FFXIVVOICELANEWIDGET_H

#include "PaintWidget.h"

#include <QSet>
#include <QVector>

#include "../ai/FfxivVoiceLoadCore.h"

class MatrixWidget;
class MidiFile;

class FfxivVoiceLaneWidget : public PaintWidget {
    Q_OBJECT

public:
    explicit FfxivVoiceLaneWidget(MatrixWidget *matrixWidget, QWidget *parent = nullptr);

    void setFile(MidiFile *file);

    /// v2.1.0 #1: Auto-Fit live preview. While active, the current voice
    /// curve is drawn grey ("before") and \a samples - the predicted curve
    /// with the victims removed - is drawn on top in the normal colors, so
    /// the grey overhang IS the material being cut away.
    void setPreviewSamples(const QVector<FfxivVoiceLoad::VoiceSample> &samples);
    void clearPreview();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    /// v2.1.0 #1: right-click offers "Auto-fit" for the overflow range or
    /// rate hotspot under the mouse (and always for the whole file).
    void contextMenuEvent(QContextMenuEvent *event) override;

signals:
    /// Forwarded to MainWindow::autoFitVoiceLoadRange (-1/-1 = whole file).
    void autoFitRangeRequested(int startTick, int endTick);

public slots:
    /// 1.6.1 (UX-VOICE-LANE-001): mirror the MatrixWidget playback cursor
    /// inside the FFXIV Voices lane so the user can see where playback is
    /// against the voice-load chart. Wired from MainWindow::play()/record()
    /// the same way `_lyricTimeline` is, because PlayerThread is recreated
    /// per `MidiPlayer::play()` call on Windows.
    void onPlaybackPositionChanged(int /*ms*/) { update(); }

private slots:
    void onAnalysisUpdated(MidiFile *file);

private:
    /// Matches LyricTimelineWidget / MiscWidget — piano key column width.
    static constexpr int LEFT_BORDER = 110;

    MatrixWidget *_matrixWidget;
    MidiFile *_file;

    // Auto-Fit live preview overlay (empty = inactive).
    QVector<FfxivVoiceLoad::VoiceSample> _previewSamples;
    bool _previewActive = false;

    // Track-share display: while any track is hidden, the full curve turns
    // grey and the visible tracks' share is painted in color on top. Cached
    // and recomputed only when the hidden set or the analysis changes.
    QVector<FfxivVoiceLoad::VoiceSample> _visibleShareSamples;
    QSet<int> _hiddenTracksCache;
    bool _visibleShareValid = false;

    int xPosOfTick(int tick) const;
    int tickOfXPos(int x) const;
};

#endif // FFXIVVOICELANEWIDGET_H
